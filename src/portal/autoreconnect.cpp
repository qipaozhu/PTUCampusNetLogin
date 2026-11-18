#include "autoreconnect.h"

#include "net/network.h"
#include "portallogin.h"

#include <QTimer>

namespace
{
	/// 掉线后先等一会儿：避免网络抖动时来回登录
	const int kFirstDelayMs = 8000;

	/// 连续失败后的退避间隔，最后一档一直重复
	const int kRetryDelaysMs[] = { 30000, 60000, 120000, 300000 };
	const int kRetryDelayCount = 4;
}

/// 构造函数：只连信号，不主动发起任何检测
AutoReconnect::AutoReconnect(Network* network, PortalLogin* portal, QObject* parent)
	: QObject(parent)
	, m_network(network)
	, m_portal(portal)
	, m_timer(new QTimer(this))
	, m_wasOnline(network->online())
{
	m_timer->setSingleShot(true);
	connect(m_timer, &QTimer::timeout, this, &AutoReconnect::attempt);

	connect(m_network, &Network::onlineChanged, this, &AutoReconnect::onOnlineChanged);
	connect(m_network, &Network::lanChanged, this, &AutoReconnect::onLanChanged);
	connect(m_portal, &PortalLogin::stateChanged, this, &AutoReconnect::onPortalStateChanged);
}

/// 在线状态变化：只有“本来在线、现在掉线”才触发自动重连
void AutoReconnect::onOnlineChanged()
{
	if (m_network->online())
	{
		m_wasOnline = true;
		m_failures = 0;
		m_timer->stop();
		return;
	}

	if (!m_wasOnline)
		return;

	m_wasOnline = false;
	m_failures = 0;
	scheduleReconnect(kFirstDelayMs);
}

/// 网卡重新连上（睡醒后 Wi-Fi 刚关联完成）：不用等退避，尽快试一次
void AutoReconnect::onLanChanged()
{
	if (m_network->lanConnected() && !m_network->online())
		scheduleReconnect(kFirstDelayMs);
}

/// 登录流程状态变化：成功就清零，后台失败就按退避再试
void AutoReconnect::onPortalStateChanged()
{
	// 正在登录：等它结束后再说
	if (m_portal->running())
	{
		m_timer->stop();
		return;
	}

	switch (m_portal->state())
	{
	case PortalLogin::Online:
		m_wasOnline = true;
		m_failures = 0;
		m_timer->stop();
		return;

	case PortalLogin::Failed:
		// 只有后台自动重连失败才继续退避重试；
		// 用户手动点「登录」失败，交给用户自己决定
		if (!m_portal->background())
			return;

		scheduleReconnect(kRetryDelaysMs[qMin(m_failures, kRetryDelayCount - 1)]);
		++m_failures;
		return;

	case PortalLogin::Idle:
		// 用户点了「停止」或「注销」：不再自动重连，等网络恢复在线再说
		m_timer->stop();
		return;

	default:
		return;
	}
}

/// 安排一次重连尝试
void AutoReconnect::scheduleReconnect(int delayMs)
{
	if (m_portal->running())
		return;

	m_timer->start(delayMs);
}

/// 定时器到点：还没恢复、还有网卡才去登录
void AutoReconnect::attempt()
{
	if (m_portal->running() || m_network->online())
		return;

	// 网卡都没有（Wi-Fi 关着 / 没插网线），等 lanChanged 再来
	if (!m_network->lanConnected())
		return;

	m_portal->startInBackground();
}
