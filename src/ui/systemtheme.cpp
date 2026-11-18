#include "systemtheme.h"

#include <QGuiApplication>
#include <QStyleHints>

#ifdef Q_OS_WIN
#include <QSettings>
#endif

namespace
{
	/// 优先用 Qt 的 colorScheme，取不到时在 Windows 上回退到注册表
	bool queryDarkFromSystem()
	{
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
		const Qt::ColorScheme scheme = QGuiApplication::styleHints()->colorScheme();
		if (scheme != Qt::ColorScheme::Unknown)
			return scheme == Qt::ColorScheme::Dark;
#endif

#ifdef Q_OS_WIN
		QSettings personalize(
			QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"),
			QSettings::NativeFormat);
		return personalize.value(QStringLiteral("AppsUseLightTheme"), 1).toInt() == 0;
#else
		return false;
#endif
	}
}

/// 构造函数：读取当前系统主题，并监听系统的主题切换
SystemTheme::SystemTheme(QObject* parent)
	: QObject(parent)
	, m_dark(queryDarkFromSystem())
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
	// 系统主题切换时实时刷新
	connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged,
		this, [this](Qt::ColorScheme) { refresh(); });
#endif
}

/// 重新读取系统主题，发生变化时发出 darkChanged 信号
void SystemTheme::refresh()
{
	const bool dark = queryDarkFromSystem();
	if (dark == m_dark)
		return;

	m_dark = dark;
	emit darkChanged();
}
