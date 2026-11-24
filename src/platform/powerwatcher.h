#pragma once

#include <QAbstractNativeEventFilter>
#include <QByteArray>
#include <QObject>

#include <functional>

/// 监听系统的「从睡眠恢复」事件。
/// 只用来在睡醒后立刻触发一次网络检测，省掉最多 30 秒的轮询等待；
/// 检测逻辑、判断逻辑都不在这里，所以就算这条消息没收到，功能也只是慢一点。
///
/// Windows 走 WM_POWERBROADCAST；其它平台目前只是空实现
/// （Linux 以后可以接 logind 的 PrepareForSleep D-Bus 信号）。
class PowerWatcher : public QObject, public QAbstractNativeEventFilter
{
	Q_OBJECT
public:
	explicit PowerWatcher(std::function<void()> onResume, QObject* parent = nullptr);
	~PowerWatcher() override;

	bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

private:
	std::function<void()> m_onResume;
};