#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QFont>
#include <QFontDatabase>
#include <QIcon>
#include <QMessageBox>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStringList>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QTimer>

#include "core/controller.h"
#include "core/singleinstance.h"
#include "net/ncsiprobing.h"
#include "net/network.h"
#include "portal/autoreconnect.h"
#include "portal/portallogin.h"
#include "powerwatcher.h"
#include "startupdiag.h"
#include "tray.h"
#include "ui/systemtheme.h"

namespace
{
	/// 应用图标：优先用 qrc 里嵌入的 ico，缺失时退回系统图标
	QIcon loadAppIcon()
	{
		QIcon icon(QStringLiteral(":/app_icon.ico"));
		if (icon.isNull())
			icon = QApplication::style()->standardIcon(QStyle::SP_ComputerIcon);
		return icon;
	}

	/// 加载 qrc 里嵌入的 font.otf，并把它设为全局默认字体，
	/// 这样 QML 里没有显式指定 family 的文字、以及托盘菜单都会用这个字体，
	/// 不会再回退成系统默认的宋体。
	/// 返回值是字体实际的 family 名，QML 里再显式设置到控件上
	/// （Quick Controls 的 Button / ComboBox 等用的是样式字体，不吃应用字体）。
	QString applyApplicationFont(QApplication& app)
	{
		const int fontId =
			QFontDatabase::addApplicationFont(QStringLiteral(":/font.otf"));
		if (fontId < 0)
		{
			qWarning() << "字体加载失败：:/font.otf";
			return app.font().family();
		}

		// OTF 里的 family 名可能是中文或英文，直接取注册后的第一个名字
		const QStringList families = QFontDatabase::applicationFontFamilies(fontId);
		if (families.isEmpty())
		{
			qWarning() << "字体文件里没有可用的 family：:/font.otf";
			return app.font().family();
		}

		const QString family = families.constFirst();

		QFont font = app.font();
		// 首选嵌入字体；万一它缺字，再退到雅黑 / Segoe UI，避免回落到宋体
		font.setFamilies({ family,
			QStringLiteral("Microsoft YaHei UI"),
			QStringLiteral("Segoe UI") });
		app.setFont(font);

		// 样式里写死了 "Segoe UI"，登记替换后连样式内部的文字也能用上嵌入字体
		QFont::insertSubstitution(QStringLiteral("Segoe UI"), family);
		QFont::insertSubstitution(QStringLiteral("Segoe UI Variable"), family);

		return family;
	}
}

/// 程序入口：只做组装——单实例、业务对象、QML 上下文、托盘、启动方式判断
int main(int argc, char* argv[])
{
	QApplication app(argc, argv);

	// 先接管日志，QML 加载失败时收集到的警告会写进 startup-error.log 并弹窗提示
	installStartupDiagnostics();

	// 决定配置文件位置和窗口 / 任务栏显示名
	QCoreApplication::setOrganizationName(QStringLiteral("Maplekilo"));
	QCoreApplication::setApplicationName(QStringLiteral("PTUCampusLogin"));

	// 提权小进程：本程序被 ShellExecuteEx(runas) 起来时只负责写 NCSI 策略注册表，
	// 写完立刻退出，不建界面也不碰托盘。必须放在单实例判断之前，
	// 否则它会被正在运行的主实例当成"第二个实例"直接踢掉
	const QStringList arguments = QCoreApplication::arguments();
	if (NcsiProbing::isHelperInvocation(arguments))
		return NcsiProbing::runHelper(arguments);

	// 单实例：已有实例在运行时只通知它显示主界面，本进程直接退出
	SingleInstance instance(QStringLiteral("Maplekilo.PTUCampusLogin"));
	if (!instance.isPrimary())
		return 0;

	// 全局字体：必须在 QML 引擎和托盘菜单创建之前设置
	const QString appFontFamily = applyApplicationFont(app);

	QQuickStyle::setStyle(QStringLiteral("FluentWinUI3"));

	const QIcon appIcon = loadAppIcon();
	app.setWindowIcon(appIcon);

	Controller controller;
	Network network;
	SystemTheme systemTheme;
	PortalLogin portal(&controller, &network);

	// 掉线自动重连：网络从在线变离线、或睡醒后网卡重新连上时，后台补一次登录
	AutoReconnect autoReconnect(&network, &portal);

	// 睡醒后立刻检测一次网络状态，不用干等 Network 的 30 秒轮询
	PowerWatcher powerWatcher([&network]() { network.refresh(); });

	QQmlApplicationEngine engine;
	engine.rootContext()->setContextProperty("controller", &controller);
	engine.rootContext()->setContextProperty("network", &network);
	engine.rootContext()->setContextProperty("systemTheme", &systemTheme);
	engine.rootContext()->setContextProperty("portal", &portal);
	// Quick Controls 的 Button / ComboBox 用的是样式字体，QML 里逐个引用这个 family
	engine.rootContext()->setContextProperty("appFontFamily", appFontFamily);

	engine.load(QUrl("qrc:/mainui/qml/main.qml"));
	if (engine.rootObjects().isEmpty())
	{
		uninstallStartupDiagnostics();

		const QStringList messages = startupDiagnostics();
		const QString details = messages.isEmpty()
			? QStringLiteral("（Qt 没有输出更多信息）")
			: messages.join(QLatin1Char('\n'));

		const QString logPath = writeStartupLog(details);
		const QString logHint = logPath.isEmpty()
			? QStringLiteral("（日志文件写入失败）")
			: logPath;

		QMessageBox::critical(nullptr, QStringLiteral("主界面加载失败"),
			QStringLiteral("QML 没有加载成功。\n\n"
				"请及时找作者解决 Bug。\n\n"
				"日志：%1\n\n%2").arg(logHint, details.left(1500)));

		return -1;
	}

	uninstallStartupDiagnostics();

	// 主窗口可能被关闭到托盘，这里统一封装「取窗口 / 重新显示 / 跳到设置页」
	auto windowOf = [&engine]() -> QQuickWindow*
		{
			if (engine.rootObjects().isEmpty())
				return nullptr;
			return qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
		};

	auto showMainWindow = [windowOf]()
		{
			QQuickWindow* window = windowOf();
			if (!window)
				return;

			if (window->visibility() == QWindow::Minimized)
				window->showNormal();
			else
				window->show();

			window->raise();
			window->requestActivate();
		};

	auto openSettingsPage = [windowOf, showMainWindow]()
		{
			if (QQuickWindow* window = windowOf())
				window->setProperty("currentPage", 1);
			showMainWindow();
		};

	// 第二次启动本程序时，把已有窗口显示出来
	QObject::connect(&instance, &SingleInstance::showRequested, &instance, showMainWindow);

	// ---------------- 系统托盘 ----------------
	AppTray tray(appIcon, &controller);
	tray.openWindow = showMainWindow;
	tray.openSettings = openSettingsPage;

	const bool trayAvailable = QSystemTrayIcon::isSystemTrayAvailable();
	if (trayAvailable)
	{
		tray.show();

		// 有托盘时常驻后台：关闭主窗口只是隐藏，真正退出走托盘菜单
		app.setQuitOnLastWindowClosed(false);
	}
	else
	{
		qWarning() << "系统托盘不可用，关闭主窗口将直接退出程序";
	}

	// 开机自启动（--autostart）：主界面保持隐藏，自动登录由 C++ 直接驱动，
	// 用户只看得到右下角的 Toast；托盘不可用时退化为正常显示主界面
	const bool startedAtBoot = controller.startedAtBoot();
	const bool hideWindowAtBoot = startedAtBoot && trayAvailable;

	if (QQuickWindow* window = windowOf())
	{
		if (!hideWindowAtBoot)
			window->show();
	}

	if (startedAtBoot)
	{
		// 等事件循环起来、QML 的 portal 连接就绪后再开始，Toast 才弹得出来
		QTimer::singleShot(0, &portal, &PortalLogin::start);
	}

	return app.exec();
}
