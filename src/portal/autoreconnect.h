#pragma once

#include <QObject>

class Network;
class PortalLogin;
class QTimer;

/// 掉线自动重连：只做“什么时候再试一次”的判断，不参与登录流程本身。
///
/// 触发时机：
///   - 网络从“能上网”变成“不能上网”（掉线、睡醒后会话失效）
///   - 网卡从断开变成连上（睡醒后 Wi-Fi 刚关联好）
/// 失败后按 30s / 60s / 2min / 5min 退避重试，5 分钟一档一直重复；
/// 任意一次成功、或者用户手动停止 / 注销，都会清空重试状态。
///
/// 例外：如果失败原因是「不是本校校园网」（PortalLogin::pendingManualAuth），
/// 说明这个网络根本不需要本校认证，重试没有意义，直接停止，
/// 等网络环境变化（换网 / 重新连上）再试。
class AutoReconnect : public QObject
{
	Q_OBJECT
public:
	AutoReconnect(Network* network, PortalLogin* portal, QObject* parent = nullptr);

private:
	void onOnlineChanged();
	void onLanChanged();
	void onPortalStateChanged();

	/// 安排一次重连尝试（已在登录中则忽略）
	void scheduleReconnect(int delayMs);
	/// 定时器到点：确认还没恢复、还有网卡，就后台跑一次登录
	void attempt();

	Network* m_network = nullptr;
	PortalLogin* m_portal = nullptr;
	QTimer* m_timer = nullptr;
	/// 上一次看到的在线状态，用来区分“掉线”和“一直没网”
	bool m_wasOnline = false;
	/// 连续失败次数，决定退避间隔
	int m_failures = 0;
};
