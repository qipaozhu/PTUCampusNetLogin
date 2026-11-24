#pragma once

#include <QObject>

class Controller;
class Network;
class PortalLogin;
class QTimer;

/// 启动阶段的决策集中在这里，QML 不再参与。
///
/// 三种情况：
///   1. 由开机自启动拉起（--autostart）→ 直接开始自动登录；
///   2. 否则等第一次联网检测：已经能上网且没登录过 → 探测既有会话
///      （确认是校园网就直接标记为已登录，不用再登一遍）；
///   3. 等不到检测结果 → 什么都不做，保持未登录。
///
/// 之前这套判断一半在 C++（自启动登录）、一半在 QML（监听 onlineChecked），
/// 窗口没加载出来时行为就不一致；现在统一由 C++ 驱动，QML 只负责显示。
class StartupCoordinator : public QObject
{
	Q_OBJECT
public:
	StartupCoordinator(Controller* controller, Network* network, PortalLogin* portal,
		QObject* parent = nullptr);

private:
	/// 第一次联网检测结果到达：决定要不要探测既有会话
	void onOnlineChecked(bool online);
	/// 等太久还没结果：放弃这次决策，保持未登录
	void stopWaiting();

	Controller* m_controller = nullptr;
	Network* m_network = nullptr;
	PortalLogin* m_portal = nullptr;
	QTimer* m_timeout = nullptr;
	bool m_decided = false;
};