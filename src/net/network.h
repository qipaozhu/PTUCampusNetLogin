#pragma once

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

/// 网络状态检测：
/// 1. 本地是否接入网络（WLAN / 有线）
/// 2. 是否能访问互联网（msftconnecttest）
/// 后台每 30 秒自动检测一次（与界面无关，窗口隐藏 / 关闭到托盘时照常运行），
/// QML 只负责消费 lanConnected / online 这些属性来显示。
class Network : public QObject
{
	Q_OBJECT
	Q_PROPERTY(bool lanConnected READ lanConnected NOTIFY lanChanged)
	Q_PROPERTY(QString lanDescription READ lanDescription NOTIFY lanChanged)
	Q_PROPERTY(bool online READ online NOTIFY onlineChanged)

public:
	explicit Network(QObject* parent = nullptr);
	~Network();

	bool lanConnected() const { return m_lanConnected; }
	QString lanDescription() const { return m_lanDescription; }
	bool online() const { return m_online; }

	/// 立即重新检测本地网络和互联网（手动刷新时用，会打断正在进行的探测）
	Q_INVOKABLE void refresh();

signals:
	void lanChanged();
	void onlineChanged();
	/// 每次互联网检测结束都会发出，不论状态是否变化
	void onlineChecked(bool online);

private:
	void checkLan();
	void checkInternet();
	/// 后台定时检测：本地网络每次都查，互联网探测在途时跳过本次
	void poll();

	bool m_lanConnected = false;
	QString m_lanDescription;
	bool m_online = false;

	QNetworkAccessManager* m_manager = nullptr;
	QNetworkReply* m_reply = nullptr;
	QTimer* m_timeout = nullptr;
	QTimer* m_pollTimer = nullptr;
};
