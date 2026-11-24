#pragma once

#include <QString>
#include <QStringList>

/// NCSI（网络连接状态指示器）策略的读写。
///
/// Windows 连上"需要认证"的网络时会自动拉起浏览器打开认证页，这个行为由 NCSI 决定。
/// 这里用组策略值 NoWebBrowserLaunch 只关掉"拉浏览器"这一件事，**不动主动探测**：
///   HKLM\SOFTWARE\Policies\Microsoft\Windows\NetworkConnectivityStatusIndicator
///       NoWebBrowserLaunch = 1 (DWORD)
/// 主动探测保持开启，系统仍然能正常判断网络状态。
///
/// 键在 HKLM 下，普通权限写不进去，所以没有管理员权限时用 ShellExecuteEx(runas)
/// 起一个提权的小进程（还是本程序的 exe，带 --apply-ncsi-policy=0|1），
/// 由它写好注册表后立刻退出，主进程不创建第二套界面。
///
/// 这是 Windows 独有的功能：其它平台上所有函数都只是降级返回失败，不会崩。
namespace NcsiProbing
{
	/// 本进程是不是被当作"提权写注册表"的小进程拉起
	bool isHelperInvocation(const QStringList& arguments);

	/// 提权小进程的入口：写注册表并返回进程退出码（0 表示成功）
	int runHelper(const QStringList& arguments);

	/// 当前是否已禁止 NCSI 自动拉起浏览器（只读，任何权限都能读）
	bool isBrowserLaunchBlocked();

	/// 直接写注册表，返回写入后注册表是否达到期望状态。
	/// 没有管理员权限时写 HKLM 会失败，此时返回 false
	bool setBrowserLaunchBlocked(bool blocked);

	/// 弹 UAC 起提权小进程去写，等它结束后再读注册表确认，返回是否生效
	bool requestElevatedChange(bool blocked);

	/// 最近一次修改失败的原因（给界面显示）；成功后为空
	QString lastError();

	/// 通知系统网络设置已经变化
	void notifySettingsChanged();
}