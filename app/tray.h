#pragma once

#include <QIcon>
#include <QObject>

#include <functional>

class QAction;
class QMenu;
class QSystemTrayIcon;
class Controller;

/// 系统托盘：显示当前登录状态，提供「打开主界面 / 设置 / 退出」入口。
/// 托盘不认识界面，怎么显示主窗口由外部通过回调决定。
class AppTray : public QObject
{
	Q_OBJECT
public:
	explicit AppTray(const QIcon& icon, Controller* controller, QObject* parent = nullptr);
	~AppTray() override;

	/// 显示托盘图标
	void show();

	/// 用户要求打开主界面 / 设置页时调用，由外部填充
	std::function<void()> openWindow;
	std::function<void()> openSettings;

private:
	/// 同步菜单文字和悬浮提示里的登录状态
	void refreshStatus();

	Controller* m_controller = nullptr;
	QMenu* m_menu = nullptr;
	QSystemTrayIcon* m_tray = nullptr;
	QAction* m_statusAction = nullptr;
};
