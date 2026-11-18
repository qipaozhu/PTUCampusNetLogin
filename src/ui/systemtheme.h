#pragma once

#include <QColor>
#include <QObject>

/// 系统亮色 / 暗色主题，同时向 QML 提供整套配色。
/// QML 里不再各写一份 dark ? "#FF..." : "#FF..."，
/// 统一读这里的属性，系统切换主题时所有绑定会自动跟着变。
class SystemTheme : public QObject
{
	Q_OBJECT
		Q_PROPERTY(bool dark READ isDark NOTIFY darkChanged)

		Q_PROPERTY(QColor bgColor READ bgColor NOTIFY darkChanged)
		Q_PROPERTY(QColor sidebarColor READ sidebarColor NOTIFY darkChanged)
		Q_PROPERTY(QColor cardColor READ cardColor NOTIFY darkChanged)
		Q_PROPERTY(QColor textColor READ textColor NOTIFY darkChanged)
		Q_PROPERTY(QColor subTextColor READ subTextColor NOTIFY darkChanged)
		Q_PROPERTY(QColor accentColor READ accentColor NOTIFY darkChanged)
		Q_PROPERTY(QColor accentSoft READ accentSoft NOTIFY darkChanged)
		Q_PROPERTY(QColor hoverSoft READ hoverSoft NOTIFY darkChanged)

public:
	explicit SystemTheme(QObject* parent = nullptr);

	bool isDark() const { return m_dark; }

	/// 窗口底色
	QColor bgColor() const { return QColor::fromRgba(m_dark ? 0xFF1E1E1Eu : 0xFFF4F4F4u); }
	/// 左侧导航栏底色
	QColor sidebarColor() const { return QColor::fromRgba(m_dark ? 0xFF272727u : 0xFFEAEAEAu); }
	/// 卡片底色
	QColor cardColor() const { return QColor::fromRgba(m_dark ? 0xFF2D2D2Du : 0xFFFFFFFFu); }
	/// 正文
	QColor textColor() const { return QColor::fromRgba(m_dark ? 0xFFF2F2F2u : 0xFF1B1B1Bu); }
	/// 次要说明文字
	QColor subTextColor() const { return QColor::fromRgba(m_dark ? 0xA6FFFFFFu : 0x99000000u); }
	/// 强调色（按钮、链接、进度）
	QColor accentColor() const { return QColor::fromRgba(m_dark ? 0xFF4CC2FFu : 0xFF0067C0u); }
	/// 强调色的浅色底
	QColor accentSoft() const { return QColor::fromRgba(m_dark ? 0x334CC2FFu : 0x1A0067C0u); }
	/// 悬停底色
	QColor hoverSoft() const { return QColor::fromRgba(m_dark ? 0x1AFFFFFFu : 0x14000000u); }

signals:
	void darkChanged();

private:
	void refresh();

	bool m_dark = false;
};
