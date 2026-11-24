#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStringList>
#include <QSystemTrayIcon>

#include "appshell.h"
#include "core/controller.h"
#include "core/singleinstance.h"
#include "core/startupcoordinator.h"
#include "net/network.h"
#include "platform/ncsiprobing.h"
#include "platform/powerwatcher.h"
#include "portal/autoreconnect.h"
#include "portal/portallogin.h"
#include "startupdiag.h"
#include "tray.h"
#include "ui/systemtheme.h"

/// 程序入口：只做组装——单实例、业务对象、QML 上下文、托盘和启动方式判断
int main(int argc, char* argv[])
{
	QApplication app(argc, argv);

	installStartupDiagnostics();

	QCoreApplication::setOrganizationName(QStringLiteral("Maplekilo"));
	QCoreApplication::setApplicationName(QStringLiteral("PTUCampusLogin"));

	// 提权小进程只写系统策略就退出，必须走在单实例判断之前，
	// 否则会被正在运行的主实例当成第二个实例踢掉
	const QStringList arguments = QCoreApplication::arguments();
	if (NcsiProbing::isHelperInvocation(arguments))
		return NcsiProbing::runHelper(arguments);

	SingleInstance instance(QStringLiteral("Maplekilo.PTUCampusLogin"));
	if (!instance.isPrimary())
		return 0;

	const QString appFontFamily = AppShell::applyApplicationFont(app);

	// FluentWinUI3 是 Qt 6.8 起才有的 Windows 专用样式；
	// 其它平台用 Material —— 它跟随系统深浅色，跨平台都能用
#if defined(Q_OS_WIN) && QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
	QQuickStyle::setStyle(QStringLiteral("FluentWinUI3"));
#else
	QQuickStyle::setStyle(QStringLiteral("Material"));
#endif

	const QIcon appIcon = AppShell::loadAppIcon();
	app.setWindowIcon(appIcon);

	Controller controller;
	Network network;
	SystemTheme systemTheme;
	PortalLogin portal(&controller, &network);
	AutoReconnect autoReconnect(&network, &portal);
	// 睡醒后立刻检测一次，不用干等 Network 的 30 秒轮询
	PowerWatcher powerWatcher([&network]() { network.refresh(); });
	// 启动决策（自启动登录 / 已在线则探测既有会话）统一交给 C++，QML 只管显示
	StartupCoordinator startup(&controller, &network, &portal);

	QQmlApplicationEngine engine;
	engine.rootContext()->setContextProperty("controller", &controller);
	engine.rootContext()->setContextProperty("network", &network);
	engine.rootContext()->setContextProperty("systemTheme", &systemTheme);
	engine.rootContext()->setContextProperty("portal", &portal);
	engine.rootContext()->setContextProperty("appFontFamily", appFontFamily);

	engine.load(QUrl("qrc:/mainui/qml/main.qml"));
	if (engine.rootObjects().isEmpty())
		return AppShell::reportLoadFailure();

	uninstallStartupDiagnostics();

	const WindowAccess window(&engine);

	QObject::connect(&instance, &SingleInstance::showRequested, &instance,
		[&window]() { window.show(); });

	AppTray tray(appIcon, &portal);
	tray.openWindow = [&window]() { window.show(); };
	tray.openSettings = [&window]() { window.openSettings(); };

	const bool trayAvailable = QSystemTrayIcon::isSystemTrayAvailable();
	if (trayAvailable)
	{
		tray.show();
		// 有托盘时常驻后台：关闭窗口只是隐藏，真正退出走托盘菜单
		app.setQuitOnLastWindowClosed(false);
	}
	else
	{
		qWarning() << "系统托盘不可用，关闭主窗口将直接退出程序";
	}

	// 开机自启动：窗口保持隐藏，登录由 C++ 直接驱动，用户只看得到 Toast
	const bool startedAtBoot = controller.startedAtBoot();
	const bool hideWindowAtBoot = startedAtBoot && trayAvailable;

	if (QQuickWindow* w = window.window())
	{
		if (!hideWindowAtBoot)
			w->show();
	}

	return app.exec();
}