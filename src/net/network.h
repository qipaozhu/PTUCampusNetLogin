#pragma once

#include <QObject>
#include <QString>
#include <QThreadPool>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

/// 网络状态检测：
/// 1. 本地是否接入网络（WLAN / 有线）—— 走 Win32 路由表和网卡枚举，
///    是阻塞调用，所以放在专属线程池里跑，不占主线程；
/// 2. 是否能访问互联网（msftconnecttest）—— QNetworkAccessManager 本身异步，不阻塞。
/// 后台每 30 秒自动检测一次（与界面无关，窗口隐藏 / 关闭到托盘时照常运行），
/// QML 只负责消费 lanConnected / online / checking 这些属性来显示。
class Network : public QObject
{
	Q_OBJECT
	Q_PROPERTY(bool lanConnected READ lanConnected NOTIFY lanChanged)
	Q_PROPERTY(QString lanDescription READ lanDescription NOTIFY lanChanged)
	Q_PROPERTY(bool online READ online NOTIFY onlineChanged)
	/// 是否正在进行一次检测：界面用它显示「检测中」并禁用按钮
	Q_PROPERTY(bool checking READ checking NOTIFY checkingChanged)

public:
	/// 单次互联网探测的超时。上层（登录流程）等结果的超时也直接用它，
	/// 保证"上层等多久"和"底层探多久"永远是同一个数
	static constexpr int kProbeTimeoutMs = 20000;

	explicit Network(QObject* parent = nullptr);
	~Network();

	bool lanConnected() const { return m_lanConnected; }
	QString lanDescription() const { return m_lanDescription; }
	bool online() const { return m_online; }
	bool checking() const { return m_checking; }

	/// 立即重新检测本地网络和互联网（手动刷新时用，会打断正在进行的互联网探测）
	Q_INVOKABLE void refresh();

signals:
	void lanChanged();
	void onlineChanged();
	void checkingChanged();
	/// 每次互联网检测结束都会发出，不论状态是否变化；参数是这一次探测的原始结果
	void onlineChecked(bool online);

private:
	/// 把本地网络探测丢到线程池里执行（阻塞的 Win32 调用都在那边）
	void checkLan();
	/// 请求 msftconnecttest，判断当前是否可以访问互联网
	void checkInternet();
	/// 后台定时检测：本地网络每次都查，互联网探测在途时跳过本次
	void poll();

	/// 应用工作线程算出来的本地网络状态（只在主线程调用）
	void applyLanState(bool connected, const QString& description);
	/// 根据两个探测的进行情况刷新 checking 属性
	void updateChecking();

	bool m_lanConnected = false;
	QString m_lanDescription;
	bool m_online = false;
	/// 连续探测失败的次数，用来过滤掉偶发的单次失败
	int m_offlineStreak = 0;

	/// 只跑本地网络探测，独立于全局线程池，方便析构时精确等待
	QThreadPool m_lanPool;
	bool m_lanProbeRunning = false;
	bool m_checking = false;

	QNetworkAccessManager* m_manager = nullptr;
	QNetworkReply* m_reply = nullptr;
	QTimer* m_timeout = nullptr;
	QTimer* m_pollTimer = nullptr;
};