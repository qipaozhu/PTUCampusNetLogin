#include "ncsiprobing.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QString>
#include <QTextStream>
#include <QVariant>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <wininet.h>
#endif

namespace
{
	/// 提权小进程的启动参数，值 1 = 禁止拉起浏览器，0 = 恢复
	const char* const kHelperArg = "--apply-ncsi-policy";

	/// "--apply-ncsi-policy=" 前缀，提权小进程据此识别自己的启动参数
	QString helperPrefix()
	{
		return QString::fromLatin1(kHelperArg) + QLatin1Char('=');
	}

	/// 最近一次失败的原因，界面直接拿去显示
	QString& errorBuffer()
	{
		static QString text;
		return text;
	}

	/// 把提权过程记到 %LOCALAPPDATA%\Maplekilo\PTUCampusLogin\ncsi.log，
	/// GUI 程序没有控制台，出问题时只能靠这个文件排查
	void appendLog(const QString& message)
	{
		const QString dir = QStandardPaths::writableLocation(
			QStandardPaths::GenericConfigLocation) + QStringLiteral("/Maplekilo/PTUCampusLogin");
		QDir().mkpath(dir);

		QFile file(dir + QStringLiteral("/ncsi.log"));
		if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
			return;

		QTextStream stream(&file);
		stream << QDateTime::currentDateTime().toString(Qt::ISODate)
			<< " [" << QCoreApplication::applicationPid() << "] "
			<< message << '\n';
	}

#ifdef Q_OS_WIN
	// 注意：用 RegOpenKeyEx 之类的 Win32 接口时，子键路径不带 HKEY_LOCAL_MACHINE 前缀。
	// 这里直接用 64 位注册表视图（KEY_WOW64_64KEY），避免被重定向到 Wow6432Node。

	// NCSI 的组策略键（注意名字里没有空格）：NoWebBrowserLaunch = 1 时，
	// NCSI 不会再自动拉起浏览器打开认证页。
	// 早先流传的 "Network Connectivity Status Indicator"（带空格）是错误写法，Windows 不读。
	const wchar_t* const kNcsiPolicySubKey =
		L"SOFTWARE\\Policies\\Microsoft\\Windows\\NetworkConnectivityStatusIndicator";
	const wchar_t* const kNoWebBrowserLaunchValue = L"NoWebBrowserLaunch";

	/// 等提权小进程退出最多等这么久（用户还要在 UAC 弹窗上点一下）
	const DWORD kElevationTimeoutMs = 60000;

	/// 本进程是否已经提权
	bool isProcessElevated()
	{
		HANDLE token = nullptr;
		if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
			return false;

		TOKEN_ELEVATION elevation = {};
		DWORD size = static_cast<DWORD>(sizeof(elevation));
		const BOOL ok = GetTokenInformation(token, TokenElevation, &elevation,
			sizeof(elevation), &size);
		CloseHandle(token);
		return ok && elevation.TokenIsElevated != 0;
	}

	/// 读一个 DWORD，读不到（值不存在或类型不是 DWORD）返回 false
	bool readDword(HKEY root, const wchar_t* subKey, const wchar_t* valueName, DWORD* value)
	{
		DWORD type = 0;
		DWORD data = 0;
		DWORD size = static_cast<DWORD>(sizeof(data));
		const LSTATUS status = RegGetValueW(root, subKey, valueName,
			RRF_RT_REG_DWORD, &type, &data, &size);
		if (status != ERROR_SUCCESS)
			return false;

		*value = data;
		return true;
	}

	/// 写一个 DWORD，成功返回 true
	bool writeDword(HKEY root, const wchar_t* subKey, const wchar_t* valueName, DWORD value)
	{
		HKEY key = nullptr;
		if (RegCreateKeyExW(root, subKey, 0, nullptr, 0,
			KEY_SET_VALUE | KEY_WOW64_64KEY, nullptr, &key, nullptr) != ERROR_SUCCESS)
		{
			return false;
		}

		const LSTATUS status = RegSetValueExW(key, valueName, 0, REG_DWORD,
			reinterpret_cast<const BYTE*>(&value), static_cast<DWORD>(sizeof(value)));
		RegCloseKey(key);
		return status == ERROR_SUCCESS;
	}

	/// 删除一个值，值或键本来就不存在也算成功
	bool deleteValue(HKEY root, const wchar_t* subKey, const wchar_t* valueName)
	{
		HKEY key = nullptr;
		if (RegOpenKeyExW(root, subKey, 0, KEY_SET_VALUE | KEY_WOW64_64KEY, &key)
			!= ERROR_SUCCESS)
		{
			return true;
		}

		const LSTATUS status = RegDeleteValueW(key, valueName);
		RegCloseKey(key);
		return status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND;
	}
#endif
}

namespace NcsiProbing
{
	/// 本进程是不是被当作"提权写注册表"的小进程拉起
	bool isHelperInvocation(const QStringList& arguments)
	{
		const QString prefix = helperPrefix();
		for (const QString& argument : arguments)
		{
			if (argument.startsWith(prefix))
				return true;
		}
		return false;
	}

	/// 提权小进程的入口：写注册表并返回进程退出码（0 成功 / 1 写入失败 / 2 没拿到管理员权限）
	int runHelper(const QStringList& arguments)
	{
		const QString prefix = helperPrefix();
		for (const QString& argument : arguments)
		{
			if (!argument.startsWith(prefix))
				continue;

			const QString value = argument.mid(prefix.size()).trimmed();
			const bool blocked = value == QLatin1String("1");

			appendLog(QStringLiteral("提权进程启动，参数=%1").arg(value));

#ifdef Q_OS_WIN
			if (!isProcessElevated())
			{
				appendLog(QStringLiteral("提权进程没有拿到管理员权限"));
				qWarning() << "提权进程没有拿到管理员权限";
				return 2;
			}
#endif

			const bool ok = setBrowserLaunchBlocked(blocked);
			if (ok)
				notifySettingsChanged();

			appendLog(QStringLiteral("提权进程写入%1，结果=%2")
				.arg(blocked ? QStringLiteral("禁止拉起浏览器") : QStringLiteral("恢复拉起浏览器"),
					ok ? QStringLiteral("成功") : QStringLiteral("失败")));

			qInfo() << "提权写注册表：" << (blocked ? "禁止拉起浏览器" : "恢复拉起浏览器")
				<< (ok ? "成功" : "失败");
			return ok ? 0 : 1;
		}

		return 1;
	}

	/// 当前是否已禁止 NCSI 自动拉起浏览器（只读，任何权限都能读）
	bool isBrowserLaunchBlocked()
	{
#ifdef Q_OS_WIN
		DWORD value = 0;
		return readDword(HKEY_LOCAL_MACHINE, kNcsiPolicySubKey, kNoWebBrowserLaunchValue, &value)
			&& value != 0;
#else
		return false;
#endif
	}

	/// 直接写注册表，返回写入后注册表是否达到期望状态
	bool setBrowserLaunchBlocked(bool blocked)
	{
#ifdef Q_OS_WIN
		// 只切"是否拉起浏览器"，不碰 EnableActiveProbing / NoActiveProbe
		if (blocked)
			writeDword(HKEY_LOCAL_MACHINE, kNcsiPolicySubKey, kNoWebBrowserLaunchValue, 1);
		else
			deleteValue(HKEY_LOCAL_MACHINE, kNcsiPolicySubKey, kNoWebBrowserLaunchValue);

		// 以注册表的实际结果为准：没权限时写 HKLM 会失败，这里就会返回 false
		return isBrowserLaunchBlocked() == blocked;
#else
		Q_UNUSED(blocked)
			return false;
#endif
	}

	/// 弹 UAC 起提权小进程去写，等它结束后再读注册表确认
	bool requestElevatedChange(bool blocked)
	{
		errorBuffer().clear();

#ifdef Q_OS_WIN
		const QString exePath = QDir::toNativeSeparators(
			QCoreApplication::applicationFilePath());
		const QString parameters = helperPrefix() + QLatin1String(blocked ? "1" : "0");
		appendLog(QStringLiteral("申请提权：blocked=%1，exe=%2")
			.arg(blocked ? QStringLiteral("1") : QStringLiteral("0"), exePath));

		// lpFile / lpParameters 直接指向这两个 QString 的缓冲区，
		// 它们在本函数返回前一直有效
		const ushort* fileBuffer = exePath.utf16();
		const ushort* paramBuffer = parameters.utf16();

		SHELLEXECUTEINFOW info = {};
		info.cbSize = sizeof(info);
		info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
		info.lpVerb = L"runas";
		info.lpFile = reinterpret_cast<LPCWSTR>(fileBuffer);
		info.lpParameters = reinterpret_cast<LPCWSTR>(paramBuffer);
		info.nShow = SW_HIDE;

		if (!ShellExecuteExW(&info))
		{
			const DWORD error = GetLastError();
			appendLog(QStringLiteral("ShellExecuteExW 失败，错误码=%1")
				.arg(static_cast<qlonglong>(error)));
			if (error == ERROR_CANCELLED)
			{
				errorBuffer() = QStringLiteral("已取消 UAC 提权，设置未修改");
			}
			else
			{
				errorBuffer() = QStringLiteral("启动提权进程失败（错误码 %1）")
					.arg(static_cast<qlonglong>(error));
			}
			return false;
		}

		if (!info.hProcess)
		{
			const bool ok = isBrowserLaunchBlocked() == blocked;
			if (!ok)
				errorBuffer() = QStringLiteral("提权进程没有返回句柄，注册表也未修改");
			return ok;
		}

		const DWORD waitResult = WaitForSingleObject(info.hProcess, kElevationTimeoutMs);
		DWORD exitCode = 1;
		GetExitCodeProcess(info.hProcess, &exitCode);
		CloseHandle(info.hProcess);

		appendLog(QStringLiteral("提权进程结束，等待结果=%1，退出码=%2，当前 blocked=%3")
			.arg(static_cast<qlonglong>(waitResult))
			.arg(static_cast<qlonglong>(exitCode))
			.arg(isBrowserLaunchBlocked() ? QStringLiteral("1") : QStringLiteral("0")));

		if (waitResult != WAIT_OBJECT_0)
		{
			// 超时也可能是写完了只是进程没退，先看注册表
			const bool ok = isBrowserLaunchBlocked() == blocked;
			if (!ok)
				errorBuffer() = QStringLiteral("等待提权进程超时，设置未修改");
			return ok;
		}

		if (exitCode == 2)
		{
			errorBuffer() = QStringLiteral("提权进程没有拿到管理员权限，设置未修改");
			return false;
		}
		if (exitCode != 0)
		{
			errorBuffer() = QStringLiteral("提权进程写注册表失败（退出码 %1）")
				.arg(static_cast<qlonglong>(exitCode));
			return false;
		}

		const bool ok = isBrowserLaunchBlocked() == blocked;
		if (!ok)
		{
			errorBuffer() = QStringLiteral(
				"提权进程执行成功，但注册表未生效（可能被组策略或安全软件锁定）");
		}
		return ok;
#else
		Q_UNUSED(blocked)
			errorBuffer() = QStringLiteral("当前平台不支持修改该设置");
		return false;
#endif
	}

	/// 最近一次修改失败的原因，成功时为空
	QString lastError()
	{
		return errorBuffer();
	}

	/// 通知系统网络设置已经变化
	void notifySettingsChanged()
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
}
