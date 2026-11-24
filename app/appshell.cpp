#include "appshell.h"

#include "startupdiag.h"

#include <QApplication>
#include <QDebug>
#include <QFont>
#include <QFontDatabase>
#include <QMessageBox>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QStringList>
#include <QStyle>

namespace AppShell
{
	/// 应用图标：优先用 qrc 里嵌入的 ico，缺失时退回系统图标
	QIcon loadAppIcon()
	{
		QIcon icon(QStringLiteral(":/app_icon.ico"));
		if (icon.isNull())
			icon = QApplication::style()->standardIcon(QStyle::SP_ComputerIcon);
		return icon;
	}

	/// 加载 qrc 里的字体并设为全局默认，返回它的 family 名
	QString applyApplicationFont(QApplication& app)
	{
		const int fontId = QFontDatabase::addApplicationFont(QStringLiteral(":/font.otf"));
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
		// 嵌入字体缺字时退到雅黑 / Segoe UI，避免回落到宋体
		font.setFamilies({ family,
			QStringLiteral("Microsoft YaHei UI"),
			QStringLiteral("Segoe UI") });
		app.setFont(font);

		// 样式内部写死了 "Segoe UI"，登记替换后连样式自己的文字也能用上嵌入字体
		QFont::insertSubstitution(QStringLiteral("Segoe UI"), family);
		QFont::insertSubstitution(QStringLiteral("Segoe UI Variable"), family);

		return family;
	}

	/// QML 没加载起来时把 Qt 的警告写进日志并弹窗
	int reportLoadFailure()
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
}

WindowAccess::WindowAccess(QQmlApplicationEngine* engine)
	: m_engine(engine)
{
}

QQuickWindow* WindowAccess::window() const
{
	if (!m_engine || m_engine->rootObjects().isEmpty())
		return nullptr;

	return qobject_cast<QQuickWindow*>(m_engine->rootObjects().constFirst());
}

void WindowAccess::show() const
{
	QQuickWindow* w = window();
	if (!w)
		return;

	if (w->visibility() == QWindow::Minimized)
		w->showNormal();
	else
		w->show();

	w->raise();
	w->requestActivate();
}

void WindowAccess::openSettings() const
{
	if (QQuickWindow* w = window())
		w->setProperty("currentPage", 1);

	show();
}