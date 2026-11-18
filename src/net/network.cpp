#include "network.h"

#include "defaultroute.h"

#include <QHostAddress>
#include <QNetworkAccessManager>
#include <QNetworkInterface>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

namespace
{
	/// Windows 系统用来判断是否有外网连接的探测地址，返回 "Microsoft Connect Test"
	const char* const kConnectTestUrl = "http://www.msftconnecttest.com/connecttest.txt";
	const char* const kConnectTestBody = "Microsoft Connect Test";

	/// 后台轮询间隔
	constexpr int kPollIntervalMs = 30000;
	/// 单次探测的超时
	constexpr int kProbeTimeoutMs = 20000;
}

/// 构造函数：初始化网络访问管理器、超时定时器和后台轮询定时器
Network::Network(QObject* parent)
	: QObject(parent)
	, m_manager(new QNetworkAccessManager(this))
	, m_timeout(new QTimer(this))
	, m_pollTimer(new QTimer(this))
{
	m_timeout->setSingleShot(true);

	// 超时则中断请求，按“无外网”处理
	connect(m_timeout, &QTimer::timeout, this, [this]()
		{
			if (m_reply)
				m_reply->abort();
		});

	// 后台定时检测：由 C++ 自己驱动，不依赖界面是否存在
	m_pollTimer->setInterval(kPollIntervalMs);
	connect(m_pollTimer, &QTimer::timeout, this, &Network::poll);
	m_pollTimer->start();

	// 启动后立刻做第一次检测（事件循环开始后再发，
	// 保证 QML 已经连好 onlineChecked / onlineChanged）
	QTimer::singleShot(0, this, &Network::refresh);
}

/// 析构函数：中断尚未完成的探测请求
Network::~Network()
{
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

/// 判断默认路由所在的网卡是否真的接入了网络（WLAN / 有线）
void Network::checkLan()
{
	bool connected = false;
	QString description;

	const quint32 index = defaultRouteInterfaceIndex();
	if (index != 0)
	{
		const QNetworkInterface iface = QNetworkInterface::interfaceFromIndex(static_cast<int>(index));
		const auto flags = iface.flags();

		if (iface.isValid()
			&& (flags & QNetworkInterface::IsUp)
			&& (flags & QNetworkInterface::IsRunning)
			&& !(flags & QNetworkInterface::IsLoopBack))
		{
			// 还要有可用的非回环、非链路本地地址，才算真正接入网络
			for (const QNetworkAddressEntry& entry : iface.addressEntries())
			{
				const QHostAddress ip = entry.ip();
				if (ip.isLoopback() || ip.isLinkLocal())
					continue;

				connected = true;
				description = iface.humanReadableName();
				break;
			}
		}
	}

	if (connected == m_lanConnected && description == m_lanDescription)
		return;

	m_lanConnected = connected;
	m_lanDescription = description;
	emit lanChanged();
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
	QNetworkRequest request(QUrl(QString::fromLatin1(kConnectTestUrl)));
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
		QNetworkRequest::NoLessSafeRedirectPolicy);
	request.setHeader(QNetworkRequest::UserAgentHeader,
		QStringLiteral("PTUCampusNetLogin/0.1 (Windows NT 10.0; Win64; x64)"));
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
	request.setTransferTimeout(kProbeTimeoutMs);
#endif

	QNetworkReply* reply = m_manager->get(request);
	m_reply = reply;
	m_timeout->start(kProbeTimeoutMs);

	connect(reply, &QNetworkReply::finished, this, [this, reply]()
		{
			// 已经被新的检测请求替换，忽略这次结果
			if (reply != m_reply)
				return;

			m_reply = nullptr;
			m_timeout->stop();

			const int status =
				reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
			const QByteArray body = reply->readAll();
			const bool online = reply->error() == QNetworkReply::NoError
				&& status >= 200 && status < 300
				&& body.contains(kConnectTestBody);
			reply->deleteLater();

			if (online == m_online)
			{
				emit onlineChecked(online);
				return;
			}

			m_online = online;
			emit onlineChanged();
			emit onlineChecked(online);
		});
}
