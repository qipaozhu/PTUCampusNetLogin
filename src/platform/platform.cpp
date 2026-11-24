#include "platform.h"

#include "winregistry.h"

#include <QDir>
#include <QGuiApplication>
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
#include <QStyleHints>
#endif

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
// winsock2.h 必须排在 windows.h 之前，否则会和 winsock.h 打架
#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>
#include <shellapi.h>
#include <wininet.h>

#include <QByteArray>
#endif

namespace Platform
{
	/// 各平台实现里用到的常量，集中放这里
	namespace Config
	{
		/// NCSI 的组策略键（注意名字里没有空格）：NoWebBrowserLaunch = 1 时，
		/// NCSI 不会再自动拉起浏览器打开认证页。
		/// 早先流传的 "Network Connectivity Status Indicator"（带空格）是错误写法，Windows 不读
		inline constexpr const char* kNcsiPolicyKey =
			"SOFTWARE\\Policies\\Microsoft\\Windows\\NetworkConnectivityStatusIndicator";
		inline constexpr const char* kNoWebBrowserLaunch = "NoWebBrowserLaunch";

		/// 开机自启动登记的根键和子键
		inline constexpr const char* kAutostartKey =
			"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run";

		/// 读取系统深浅色主题的注册表位置
		inline constexpr const char* kPersonalizeKey =
			"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
		inline constexpr const char* kAppsUseLightTheme = "AppsUseLightTheme";

		/// 根键字符串，取用方便
		inline QString machineRoot() { return QString::fromLatin1(WinRegistry::kLocalMachine); }
		inline QString userRoot() { return QString::fromLatin1(WinRegistry::kCurrentUser); }
	}

	// ============================ 权限 ============================

	/// 当前进程是否以管理员 / root 权限运行
	bool isElevated()
	{
#ifdef Q_OS_WIN
		HANDLE token = nullptr;
		if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
			return false;

		TOKEN_ELEVATION elevation = {};
		DWORD size = static_cast<DWORD>(sizeof(elevation));
		const BOOL ok = GetTokenInformation(token, TokenElevation, &elevation,
			sizeof(elevation), &size);
		CloseHandle(token);
		return ok && elevation.TokenIsElevated != 0;
#else
		// Linux 下用 geteuid() == 0
		return false;
#endif
	}

	// ============================ 网络 ============================

	/// 查询 IPv4 路由表，挑出默认路由所在的网卡索引
	quint32 defaultRouteInterfaceIndex()
	{
#ifdef Q_OS_WIN
		// 第一次调用只用来问需要多大的缓冲区
		DWORD size = 0;
		if (::GetIpForwardTable(nullptr, &size, FALSE) != ERROR_INSUFFICIENT_BUFFER || size == 0)
			return 0;

		QByteArray buffer(static_cast<int>(size), Qt::Uninitialized);
		auto* table = reinterpret_cast<MIB_IPFORWARDTABLE*>(buffer.data());

		if (::GetIpForwardTable(table, &size, FALSE) != NO_ERROR)
			return 0;

		quint32 bestIndex = 0;
		DWORD bestMetric = MAXDWORD;

		for (DWORD i = 0; i < table->dwNumEntries; ++i)
		{
			const MIB_IPFORWARDROW& row = table->table[i];

			// 只要目的地址和掩码都为 0 的默认路由
			if (row.dwForwardDest != 0 || row.dwForwardMask != 0)
				continue;
			if (row.dwForwardType == MIB_IPROUTE_TYPE_INVALID)
				continue;

			// 正常情况下只会有一条默认路由；多条时取度量最小的那条
			if (row.dwForwardMetric1 < bestMetric)
			{
				bestMetric = row.dwForwardMetric1;
				bestIndex = row.dwForwardIfIndex;
			}
		}

		return bestIndex;
#else
		// Linux 下解析 /proc/net/route 找 Destination == 00000000 的那一条
		return 0;
#endif
	}

	/// 当前是否已禁止系统在需要认证时自动拉起浏览器
	bool isBrowserLaunchBlocked()
	{
		quint32 value = 0;
		const bool found = WinRegistry::readDword(Config::machineRoot(),
			QString::fromLatin1(Config::kNcsiPolicyKey),
			QString::fromLatin1(Config::kNoWebBrowserLaunch), &value);
		return found && value != 0;
	}

	/// 直接写策略键；返回写入后实际状态是否达到期望
	bool setBrowserLaunchBlocked(bool blocked)
	{
		const QString root = Config::machineRoot();
		const QString key = QString::fromLatin1(Config::kNcsiPolicyKey);
		const QString value = QString::fromLatin1(Config::kNoWebBrowserLaunch);

		// 只切“是否拉起浏览器”，不碰 EnableActiveProbing / NoActiveProbe
		if (blocked)
			WinRegistry::writeDword(root, key, value, 1);
		else
			WinRegistry::removeValue(root, key, value);

		// 以注册表实际结果为准：没权限时写 HKLM 会失败，这里就会返回 false
		return isBrowserLaunchBlocked() == blocked;
	}

	/// 通知系统网络设置已经变化
	void notifyNetworkSettingsChanged()
	{
#ifdef Q_OS_WIN
		// 让 WinINet 立即重新读取网络设置，并广播给其它程序
		InternetSetOptionW(nullptr, INTERNET_OPTION_SETTINGS_CHANGED, nullptr, 0);
		InternetSetOptionW(nullptr, INTERNET_OPTION_REFRESH, nullptr, 0);
		SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0,
			reinterpret_cast<LPARAM>(L"Internet Settings"),
			SMTO_ABORTIFHUNG, 1000, nullptr);
#endif
	}

	/// 当前平台是否支持「禁止系统自动拉起浏览器」这套策略
	bool supportsBrowserLaunchPolicy()
	{
#ifdef Q_OS_WIN
		// NCSI 是 Windows 独有的机制
		return true;
#else
		return false;
#endif
	}

	/// 以管理员权限启动指定程序并等它退出
	bool runElevatedAndWait(const QString& program, const QString& arguments,
		int timeoutMs, int* exitCode, QString* error)
	{
		if (exitCode)
			*exitCode = -1;
		if (error)
			error->clear();

#ifdef Q_OS_WIN
		const QString nativeProgram = QDir::toNativeSeparators(program);
		const QString parameters = arguments.trimmed();

		SHELLEXECUTEINFOW info = {};
		info.cbSize = sizeof(info);
		// SEE_MASK_NOASYNC 是必须的：本函数在后台线程里被调用，而线程池线程没有消息循环。
		// 不加这个标志，ShellExecuteEx 会把操作丢给后台线程后立即返回，等待语义就失效了
		info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC;
		info.lpVerb = L"runas";
		// lpFile / lpParameters 直接指向这两个 QString 的缓冲区，
		// 它们在本函数返回前一直有效
		info.lpFile = reinterpret_cast<LPCWSTR>(nativeProgram.utf16());
		info.lpParameters = parameters.isEmpty()
			? nullptr
			: reinterpret_cast<LPCWSTR>(parameters.utf16());
		info.nShow = SW_HIDE;

		if (!ShellExecuteExW(&info))
		{
			const DWORD code = GetLastError();
			if (error)
			{
				*error = (code == ERROR_CANCELLED)
					? QStringLiteral("已取消 UAC 提权")
					: QStringLiteral("启动提权进程失败（错误码 %1）")
					.arg(static_cast<qlonglong>(code));
			}
			return false;
		}

		if (!info.hProcess)
		{
			// 系统没真的起进程：交给调用方按注册表实际状态判断
			if (error)
				*error = QStringLiteral("提权进程没有返回句柄");
			return false;
		}

		const DWORD waitResult = WaitForSingleObject(info.hProcess,
			static_cast<DWORD>(timeoutMs));
		DWORD code = 1;
		GetExitCodeProcess(info.hProcess, &code);
		CloseHandle(info.hProcess);

		if (waitResult != WAIT_OBJECT_0)
		{
			if (error)
				*error = QStringLiteral("等待提权进程超时");
			return false;
		}

		if (exitCode)
			*exitCode = static_cast<int>(code);
		return true;
#else
		Q_UNUSED(program)
		Q_UNUSED(arguments)
		Q_UNUSED(timeoutMs)
		if (error)
			*error = QStringLiteral("当前平台不支持提权操作");
		return false;
#endif
	}

	// ============================ 开机自启动 ============================

	/// 读取已登记的自启动命令
	QString autostartCommand(const QString& name)
	{
		return WinRegistry::readString(Config::userRoot(),
			QString::fromLatin1(Config::kAutostartKey), name);
	}

	/// 登记开机自启动
	bool setAutostart(const QString& name, const QString& command, QString* error)
	{
		const bool ok = WinRegistry::writeString(Config::userRoot(),
			QString::fromLatin1(Config::kAutostartKey), name, command);
		if (!ok && error)
			*error = QStringLiteral("写入开机自启动注册表失败");
		return ok;
	}

	/// 取消开机自启动
	bool removeAutostart(const QString& name, QString* error)
	{
		const bool ok = WinRegistry::removeValue(Config::userRoot(),
			QString::fromLatin1(Config::kAutostartKey), name);
		if (!ok && error)
			*error = QStringLiteral("清除开机自启动注册表失败");
		return ok;
	}

	// ============================ 系统主题 ============================

	/// 优先用 Qt 的 colorScheme，取不到时才走各平台自己的实现
	bool systemPrefersDark()
	{
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
		if (QStyleHints* hints = QGuiApplication::styleHints())
		{
			const Qt::ColorScheme scheme = hints->colorScheme();
			if (scheme != Qt::ColorScheme::Unknown)
				return scheme == Qt::ColorScheme::Dark;
		}
#endif

#ifdef Q_OS_WIN
		// AppsUseLightTheme == 0 表示深色；读不到按浅色处理
		quint32 useLightTheme = 1;
		WinRegistry::readDword(Config::userRoot(),
			QString::fromLatin1(Config::kPersonalizeKey),
			QString::fromLatin1(Config::kAppsUseLightTheme), &useLightTheme);
		return useLightTheme == 0;
#else
		// Linux 下可以读 gsettings 的 color-scheme，或 xdg-desktop-portal
		return false;
#endif
	}
}