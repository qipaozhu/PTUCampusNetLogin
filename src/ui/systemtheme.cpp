#include "systemtheme.h"

#include "platform/platform.h"

#include <QGuiApplication>
#include <QStyleHints>

/// 构造函数：读取当前系统主题，并监听系统的主题切换
SystemTheme::SystemTheme(QObject* parent)
	: QObject(parent)
	, m_dark(Platform::systemPrefersDark())
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
	const bool dark = Platform::systemPrefersDark();
	if (dark == m_dark)
		return;

	m_dark = dark;
	emit darkChanged();
}