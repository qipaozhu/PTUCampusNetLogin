#pragma once

#include <QIcon>
#include <QString>

class QApplication;
class QQmlApplicationEngine;
class QQuickWindow;

/// 应用外壳的杂项：图标、字体、启动失败提示。
/// main.cpp 只负责组装，这些细节放在这里。
namespace AppShell
{
	/// 应用图标：优先用 qrc 里嵌入的 ico，缺失时退回系统图标
	QIcon loadAppIcon();

	/// 加载 qrc 里的字体并设为全局默认字体，返回它的 family 名。
	/// Quick Controls 的 Button / ComboBox 用的是样式字体，QML 里还要显式引用这个 family
	QString applyApplicationFont(QApplication& app);

	/// QML 没加载起来时把 Qt 的警告写进日志并弹窗，返回进程退出码
	int reportLoadFailure();
}

/// 主窗口可能被关闭到托盘，这里封装「取窗口 / 重新显示 / 跳到设置页」
class WindowAccess
{
public:
	explicit WindowAccess(QQmlApplicationEngine* engine);

	/// 取主窗口指针；QML 还没加载出根对象时返回 nullptr
	QQuickWindow* window() const;

	/// 显示并激活主窗口（最小化时先还原），供托盘和第二次启动使用
	void show() const;

	/// 切到设置页，再把主窗口显示出来
	void openSettings() const;

private:
	QQmlApplicationEngine* m_engine = nullptr;
};