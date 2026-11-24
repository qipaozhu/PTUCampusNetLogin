#pragma once

#include <QString>
#include <QtGlobal>

/// 平台能力的统一入口。
///
/// 规则：系统头文件（windows.h / iphlpapi.h / shellapi.h / wininet.h …）
/// 只允许出现在 platform.cpp 和 winregistry.cpp 里，
/// 其它模块一律通过这里的函数访问平台能力。
///
/// 将来移植 Linux 时，只需要在 platform.cpp 里补一份 #else 分支
/// （路由表读 /proc/net/route、自启动写 ~/.config/autostart、提权用 pkexec……），
/// 上层代码一行都不用改。
namespace Platform
{
	// ---------------- 权限 ----------------
	/// 当前进程是否以管理员 / root 权限运行
	bool isElevated();

	// ---------------- 网络 ----------------
	/// 默认路由（0.0.0.0/0）所在网卡的索引；没有默认路由时返回 0
	quint32 defaultRouteInterfaceIndex();

	/// 当前是否已禁止系统在需要认证时自动拉起浏览器
	bool isBrowserLaunchBlocked();
	/// 直接写策略；返回写入后实际状态是否达到期望
	bool setBrowserLaunchBlocked(bool blocked);
	/// 通知系统网络设置已经变化
	void notifyNetworkSettingsChanged();
	/// 当前平台是否支持「禁止系统自动拉起浏览器」这套策略。
	/// 这是 NCSI 独有的概念，非 Windows 平台返回 false，界面据此隐藏对应开关
	bool supportsBrowserLaunchPolicy();

	/// 以管理员权限启动指定程序并等它退出。
	/// 成功拿到退出码返回 true；没起来 / 超时返回 false，原因写进 error
	bool runElevatedAndWait(const QString& program, const QString& arguments,
		int timeoutMs, int* exitCode, QString* error);

	/// 等待提权进程的默认超时（用户还要在 UAC 弹窗上点一下）
	constexpr int kElevationTimeoutMs = 60000;

	// ---------------- 开机自启动 ----------------
	/// 读取已登记的自启动命令；没登记返回空串
	QString autostartCommand(const QString& name);
	/// 登记开机自启动，失败时 error 里是原因
	bool setAutostart(const QString& name, const QString& command, QString* error);
	/// 取消开机自启动
	bool removeAutostart(const QString& name, QString* error);

	// ---------------- 系统主题 ----------------
	/// 系统当前是否使用深色主题
	bool systemPrefersDark();
}