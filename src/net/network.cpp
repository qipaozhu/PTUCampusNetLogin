#include "network.h"

#include "lanprobe.h"

#include <QMetaObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRunnable>
#include <QTimer>
#include <QUrl>

// 版本号由 CMake 注入；单独用命令行编译时退化成 "dev"
#ifndef PTU_APP_VERSION
#define PTU_APP_VERSION "dev"
#endif

namespace NetworkConfig
{
	/// 系统用来判断是否有外网连接的探测地址，返回 "Microsoft Connect Test"
	constexpr const char* kConnectTestUrl = "http://www.msftconnecttest.com/connecttest.txt";
	constexpr const char* kConnectTestBody = "Microsoft Connect Test";

	/// 后台轮询间隔
	constexpr int kPollIntervalMs = 30000;
	/// 连续多少次探测失败才认定真的掉线
	constexpr int kOfflineConfirmCount = 2;

	/// 探测请求用的 UA。版本号由 CMake 注入（见 CMakeLists 的 PTU_APP_VERSION），
	/// 和工程版本永远一致
	QString userAgent()
	{
#if defined(Q_OS_WIN)
		return QStringLiteral("PTUCampusNetLogin/" PTU_APP_VERSION
			" (Windows NT 10.0; Win64; x64)");
#else
		return QStringLiteral("PTUCampusNetLogin/" PTU_APP_VERSION);
#endif
	}
}

/// 构造函数：初始化网络访问管理器、超时定时器和后台轮询定时器
Network::Network(QObject* parent)
	: QObject(parent)
	, m_manager(new QNetworkAccessManager(this))
	, m_timeout(new QTimer(this))
	, m_pollTimer(new QTimer(this))
{
	// 本地网络探测串行执行，一个在跑就够了
	m_lanPool.setMaxThreadCount(1);

	m_timeout->setSingleShot(true);

	// 超时则中断请求，按"无外网"处理
	connect(m_timeout, &QTimer::timeout, this, [this]()
		{
			if (m_reply)
				m_reply->abort();
		});

	// 后台定时检测：由 C++ 自己驱动，不依赖界面是否存在
	m_pollTimer->setInterval(NetworkConfig::kPollIntervalMs);
	connect(m_pollTimer, &QTimer::timeout, this, &Network::poll);
	m_pollTimer->start();

	// 启动后立刻做第一次检测（事件循环开始后再发，
	// 保证 QML 已经连好 onlineChecked / onlineChanged）
	QTimer::singleShot(0, this, &Network::refresh);
}

/// 析构函数：先等本地网络探测结束，再中断尚未完成的互联网请求
Network::~Network()
{
	// 等线程池里的探测跑完，之后就不会再有回调打到已经析构的对象上
	m_lanPool.waitForDone();

	if (m_reply)
		m_reply->abort();
}

/// 刷新局域网与互联网的连接状态
void Network::refresh()
{
	checkLan();
	checkInternet();
}

/// 后台轮询：本地网络每次都要查，互联网探测在途时跳过，
/// 避免打断门户登录流程自己发起的那次探测
void Network::poll()
{
	checkLan();
	if (m_reply)
		return;
	checkInternet();
}

/// 把本地网络探测丢到线程池：阻塞的系统调用不能占着主线程
void Network::checkLan()
{
	if (m_lanProbeRunning)
		return;

	m_lanProbeRunning = true;
	updateChecking();

	m_lanPool.start(QRunnable::create([this]()
		{
			const LanProbe::Result result = LanProbe::probe();

			// 结果必须回到主线程再写成员变量，避免和工作线程抢同一份状态
			QMetaObject::invokeMethod(this, [this, result]()
				{
					m_lanProbeRunning = false;
					applyLanState(result.connected, result.description);
					updateChecking();
				}, Qt::QueuedConnection);
		}));
}

/// 应用本地网络状态：只在主线程调用，负责去重和发信号
void Network::applyLanState(bool connected, const QString& description)
{
	if (connected == m_lanConnected && description == m_lanDescription)
		return;

	m_lanConnected = connected;
	m_lanDescription = description;

	// 网卡都断了就肯定没有外网：顺手把在线状态归零，不必等下一次探测超时。
	// 这样唤醒后 Wi-Fi 重新关联时，AutoReconnect 能马上拿到"离线"去重连
	if (!connected && m_online)
	{
		m_online = false;
		m_offlineStreak = NetworkConfig::kOfflineConfirmCount;
		emit onlineChanged();
	}

	emit lanChanged();
}

/// 刷新「检测中」状态：本地探测和互联网探测任一在跑都算
void Network::updateChecking()
{
	const bool checking = m_lanProbeRunning || m_reply != nullptr;
	if (checking == m_checking)
		return;

	m_checking = checking;
	emit checkingChanged();
}

/// 请求 msftconnecttest，判断当前是否可以访问互联网
void Network::checkInternet()
{
	if (m_reply)
	{
		QNetworkReply* old = m_reply;
		m_reply = nullptr;
		old->abort();
		old->deleteLater();
	}

	// 发送网络请求到微软服务器
	QNetworkRequest request(QUrl(QString::fromLatin1(NetworkConfig::kConnectTestUrl)));
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
		QNetworkRequest::NoLessSafeRedirectPolicy);
	request.setHeader(QNetworkRequest::UserAgentHeader, NetworkConfig::userAgent());
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
	request.setTransferTimeout(kProbeTimeoutMs);
#endif

	QNetworkReply* reply = m_manager->get(request);
	m_reply = reply;
	m_timeout->start(kProbeTimeoutMs);
	updateChecking();

	connect(reply, &QNetworkReply::finished, this, [this, reply]()
		{
			// 已经被新的检测请求替换，忽略这次结果
			if (reply != m_reply)
				return;

			m_reply = nullptr;
			m_timeout->stop();
			updateChecking();

			const int status =
				reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
			const QByteArray body = reply->readAll();
			const bool online = reply->error() == QNetworkReply::NoError
				&& status >= 200 && status < 300
				&& body.contains(NetworkConfig::kConnectTestBody);
			reply->deleteLater();

			// 一次探测失败不代表真掉线：Portal 劫持探测地址、DNS 抖动、
			// 系统刚唤醒都会让这一次失败。所以只有连续失败才改成离线，
			// 而恢复在线立即生效——否则一次误判就会触发一次多余的后台登录
			bool confirmedOnline = m_online;
			if (online)
			{
				m_offlineStreak = 0;
				confirmedOnline = true;
			}
			else
			{
				if (m_offlineStreak < NetworkConfig::kOfflineConfirmCount)
					++m_offlineStreak;
				if (m_offlineStreak >= NetworkConfig::kOfflineConfirmCount)
					confirmedOnline = false;
			}

			if (confirmedOnline != m_online)
			{
				m_online = confirmedOnline;
				emit onlineChanged();
			}

			// onlineChecked 始终上报这次探测的原始结果，登录流程要用它做即时判断
			emit onlineChecked(online);
		});
}