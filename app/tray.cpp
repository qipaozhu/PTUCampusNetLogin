#include "tray.h"

#include "portal/portallogin.h"

#include <QAction>
#include <QCoreApplication>
#include <QMenu>
#include <QSystemTrayIcon>

/// 构造函数：建菜单、接登录状态，并把「打开 / 设置 / 退出」接到回调上
AppTray::AppTray(const QIcon& icon, PortalLogin* portal, QObject* parent)
	: QObject(parent)
	, m_portal(portal)
	, m_menu(new QMenu)
	, m_tray(new QSystemTrayIcon)
{
	m_tray->setIcon(icon);

	// 顶部只读项：显示当前登录状态
	m_statusAction = m_menu->addAction(QStringLiteral("状态：未登录"));
	m_statusAction->setEnabled(false);
	m_menu->addSeparator();

	QAction* openAction = m_menu->addAction(QStringLiteral("打开主界面"));
	QAction* settingsAction = m_menu->addAction(QStringLiteral("设置"));
	m_menu->addSeparator();
	QAction* quitAction = m_menu->addAction(QStringLiteral("退出"));

	m_tray->setContextMenu(m_menu);

	refreshStatus();
	// 只更新状态，不弹托盘通知气泡：成功 / 失败由 QML 的 Toast 提示
	connect(m_portal, &PortalLogin::stateChanged, this, &AppTray::refreshStatus);

	connect(openAction, &QAction::triggered, this, [this]()
		{
			if (openWindow)
				openWindow();
		});
	connect(settingsAction, &QAction::triggered, this, [this]()
		{
			if (openSettings)
				openSettings();
		});
	connect(quitAction, &QAction::triggered, this, [this]()
		{
			m_tray->hide();
			QCoreApplication::quit();
		});

	// 单击 / 双击托盘图标都能打开主界面
	connect(m_tray, &QSystemTrayIcon::activated, this,
		[this](QSystemTrayIcon::ActivationReason reason)
		{
			if (reason != QSystemTrayIcon::DoubleClick && reason != QSystemTrayIcon::Trigger)
				return;
			if (openWindow)
				openWindow();
		});
}

/// 析构：托盘图标必须先于菜单销毁，所以两者都不挂父对象，这里按顺序删
AppTray::~AppTray()
{
	delete m_tray;
	delete m_menu;
}

void AppTray::show()
{
	m_tray->show();
}

/// 刷新菜单文字 + 悬浮提示
void AppTray::refreshStatus()
{
	const QString text = m_portal->statusText();
	m_statusAction->setText(QStringLiteral("状态：%1").arg(text));
	m_tray->setToolTip(QStringLiteral("莆田学院校园网自动登录\n当前状态：%1").arg(text));
}
