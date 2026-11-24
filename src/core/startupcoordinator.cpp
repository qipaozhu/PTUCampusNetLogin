#include "startupcoordinator.h"

#include "controller.h"
#include "net/network.h"
#include "portal/portallogin.h"

#include <QTimer>

namespace StartupConfig
{
	/// 等第一次联网检测结果的最长时间，和底层探测超时保持一致
	constexpr int kDecisionTimeoutMs = Network::kProbeTimeoutMs;
}

StartupCoordinator::StartupCoordinator(Controller* controller, Network* network,
	PortalLogin* portal, QObject* parent)
	: QObject(parent)
	, m_controller(controller)
	, m_network(network)
	, m_portal(portal)
	, m_timeout(new QTimer(this))
{
	m_timeout->setSingleShot(true);
	connect(m_timeout, &QTimer::timeout, this, &StartupCoordinator::stopWaiting);

	// 开机自启动：直接开始登录。延后到事件循环起来再发，QML 的 Toast 才接得住
	if (m_controller->startedAtBoot())
	{
		m_decided = true;
		QTimer::singleShot(0, m_portal, &PortalLogin::start);
		return;
	}

	connect(m_network, &Network::onlineChecked, this,
		&StartupCoordinator::onOnlineChecked);
	m_timeout->start(StartupConfig::kDecisionTimeoutMs);
}

/// 第一次联网检测结果到达
void StartupCoordinator::onOnlineChecked(bool online)
{
	if (m_decided)
		return;

	m_decided = true;
	m_timeout->stop();

	// 启动前就已经能上网：确认一下是不是校园网环境，是的话直接标记为已登录
	if (online && !m_portal->running() && m_portal->state() == PortalLogin::Idle)
		m_portal->detectExistingSession();
}

/// 等太久还没结果：放弃这次决策
void StartupCoordinator::stopWaiting()
{
	m_decided = true;
}