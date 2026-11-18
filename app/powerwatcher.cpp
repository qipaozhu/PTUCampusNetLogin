#include "powerwatcher.h"

#include <QCoreApplication>

#include <windows.h>

#include <utility>

PowerWatcher::PowerWatcher(std::function<void()> onResume, QObject* parent)
	: QObject(parent)
	, m_onResume(std::move(onResume))
{
	qApp->installNativeEventFilter(this);
}

PowerWatcher::~PowerWatcher()
{
	if (qApp)
		qApp->removeNativeEventFilter(this);
}

bool PowerWatcher::nativeEventFilter(const QByteArray& eventType, void* message,
	qintptr* result)
{
	Q_UNUSED(result);

	if (eventType != QByteArrayLiteral("windows_generic_MSG")
		&& eventType != QByteArrayLiteral("windows_dispatcher_MSG"))
		return false;

	const MSG* msg = static_cast<MSG*>(message);
	if (!msg || msg->message != WM_POWERBROADCAST)
		return false;

	if (msg->wParam != PBT_APMRESUMEAUTOMATIC && msg->wParam != PBT_APMRESUMESUSPEND)
		return false;

	if (m_onResume)
		m_onResume();

	// 不吞消息：其它监听者（包括 Qt 自己）还要继续处理
	return false;
}
