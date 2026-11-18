#include "portalclient.h"

#include "core/controller.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkCookieJar>
#include <QNetworkProxy>
#include <QNetworkRequest>
#include <QUrlQuery>

#include <utility>

namespace
{
	/// 校园网 Portal 用的浏览器 UA
	const char* const kUserAgent =
		"Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
		"AppleWebKit/537.36 (KHTML, like Gecko) "
		"Chrome/120.0.0.0 Safari/537.36";

	const int kRequestTimeoutMs = 8000;

	/// 构建请求：固定 UA、强制走网络、带上超时
	QNetworkRequest buildRequest(const QUrl& url)
	{
		QNetworkRequest request(url);
		request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
			QNetworkRequest::AlwaysNetwork);
		request.setHeader(QNetworkRequest::UserAgentHeader,
			QString::fromLatin1(kUserAgent));
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
		request.setTransferTimeout(kRequestTimeoutMs);
#endif
		return request;
	}

	/// 解析 JSONP / JSON：result == "1" 视为成功，msga / msg 为错误信息
	bool parseJsonp(const QByteArray& body, QString* message)
	{
		const QString text = QString::fromUtf8(body).trimmed();
		if (text.isEmpty())
			return false;

		const int start = text.indexOf(QLatin1Char('{'));
		const int end = text.lastIndexOf(QLatin1Char('}'));
		if (start < 0 || end <= start)
			return false;

		QJsonParseError error{};
		const QJsonDocument document = QJsonDocument::fromJson(
			text.mid(start, end - start + 1).toUtf8(), &error);
		if (error.error != QJsonParseError::NoError || !document.isObject())
			return false;

		const QJsonObject object = document.object();
		if (message)
		{
			*message = object.value(QStringLiteral("msga")).toString();
			if (message->isEmpty())
				*message = object.value(QStringLiteral("msg")).toString();
		}
		return object.value(QStringLiteral("result")).toVariant().toString()
			== QLatin1String("1");
	}

	/// 把接口返回体解析成业务结果
	PortalClient::Result parseResult(const PortalClient::Response& response)
	{
		PortalClient::Result result;
		if (response.ok())
			result.ok = parseJsonp(response.body, &result.message);
		return result;
	}
}

/// 构造函数：门户在校内网，必须直连（不走系统代理）；
/// eportal 用 Cookie 关联“取码 / 校验 / 登录”的会话，所以要自己带 CookieJar
PortalClient::PortalClient(Controller* controller, QObject* parent)
	: QObject(parent)
	, m_controller(controller)
	, m_manager(new QNetworkAccessManager(this))
{
	m_manager->setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
	m_manager->setCookieJar(new QNetworkCookieJar(m_manager));
}

/// Portal 根页面是否可访问
void PortalClient::probePortal(std::function<void(bool)> callback)
{
	get(eportalUrl(QStringLiteral("/")),
		[callback = std::move(callback)](const Response& response)
		{
			callback(response.reachable());
		});
}

/// 取验证码图片：<eportal>/eportal/?c=main&a=getCode&v=<ms>
void PortalClient::fetchCaptcha(std::function<void(const QImage&, const QString&)> callback)
{
	QUrl url = eportalUrl(QStringLiteral("/eportal/"));

	QUrlQuery query;
	query.addQueryItem(QStringLiteral("c"), QStringLiteral("main"));
	query.addQueryItem(QStringLiteral("a"), QStringLiteral("getCode"));
	query.addQueryItem(QStringLiteral("v"),
		QString::number(QDateTime::currentMSecsSinceEpoch()));
	url.setQuery(query);

	get(url, [callback = std::move(callback)](const Response& response)
		{
			if (!response.ok()
				|| !response.contentType.startsWith(QLatin1String("image/"),
					Qt::CaseInsensitive)
				|| response.body.isEmpty())
			{
				callback(QImage(), QStringLiteral("获取验证码失败，需要手动登录"));
				return;
			}

			const QImage image = QImage::fromData(response.body);
			if (image.isNull())
			{
				callback(QImage(), QStringLiteral("验证码图片无法解析，需要手动登录"));
				return;
			}

			callback(image, QString());
		});
}

/// 校验验证码：<eportal>/eportal/?c=Portal&a=check_captcha&...
void PortalClient::checkCaptcha(const QString& text,
	std::function<void(const Result&)> callback)
{
	QUrl url = eportalUrl(QStringLiteral("/eportal/"));

	QUrlQuery query;
	query.addQueryItem(QStringLiteral("c"), QStringLiteral("Portal"));
	query.addQueryItem(QStringLiteral("a"), QStringLiteral("check_captcha"));
	query.addQueryItem(QStringLiteral("callback"), QStringLiteral("handle"));
	query.addQueryItem(QStringLiteral("captcha"), text);
	query.addQueryItem(QStringLiteral("_"),
		QString::number(QDateTime::currentMSecsSinceEpoch()));
	url.setQuery(query);

	get(url, [callback = std::move(callback)](const Response& response)
		{
			callback(parseResult(response));
		});
}

/// 登录：<login>/drcom/login?callback=...&DDDDD=...&upass=...&0MKKey=123456...
void PortalClient::login(const QString& username, const QString& password,
	std::function<void(const Result&)> callback)
{
	const qint64 now = QDateTime::currentMSecsSinceEpoch();

	QUrl url = loginUrl(QStringLiteral("/drcom/login"));

	QUrlQuery query;
	query.addQueryItem(QStringLiteral("callback"), QStringLiteral("dr%1").arg(now));
	query.addQueryItem(QStringLiteral("DDDDD"), username);
	query.addQueryItem(QStringLiteral("upass"), password);
	query.addQueryItem(QStringLiteral("_"), QString::number(now));
	query.addQueryItem(QStringLiteral("0MKKey"), QStringLiteral("123456"));
	query.addQueryItem(QStringLiteral("R1"), QStringLiteral("0"));
	query.addQueryItem(QStringLiteral("R3"), QStringLiteral("0"));
	query.addQueryItem(QStringLiteral("R6"), QStringLiteral("0"));
	query.addQueryItem(QStringLiteral("para"), QStringLiteral("00"));
	query.addQueryItem(QStringLiteral("v6ip"), QString());
	url.setQuery(query);

	// 注意：URL 里带密码，不能写日志
	get(url, [callback = std::move(callback)](const Response& response)
		{
			callback(parseResult(response));
		});
}

/// 注销：暂时不可用
void PortalClient::logout(std::function<void(const Response&)> callback)
{

}

/// 统一的 GET：先取消上一个请求，再发新的
void PortalClient::get(const QUrl& url, std::function<void(const Response&)> handler)
{
	abort();

	QNetworkReply* reply = m_manager->get(buildRequest(url));
	m_reply = reply;

	connect(reply, &QNetworkReply::finished, this,
		[this, reply, handler = std::move(handler)]()
		{
			Response response;
			response.error = reply->error();
			response.status =
				reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
			response.body = reply->readAll();
			response.contentType =
				reply->header(QNetworkRequest::ContentTypeHeader).toString();
			reply->deleteLater();

			// 请求已经被新的请求取代（或流程被中止），结果直接丢弃
			if (reply != m_reply)
				return;
			m_reply = nullptr;

			handler(response);
		});
}

/// 取消尚未完成的请求
void PortalClient::abort()
{
	if (!m_reply)
		return;

	QNetworkReply* reply = m_reply;
	m_reply = nullptr;
	reply->abort();
	reply->deleteLater();
}

/// 登录接口所在地址（host:80）
QUrl PortalClient::loginUrl(const QString& path) const
{
	QUrl url;
	url.setScheme(QStringLiteral("http"));
	// 地址已由 Controller 校验并解析好，这里直接用，不再重复解析
	url.setHost(m_controller->portalHost());
	url.setPath(path);
	return url;
}

/// 取码 / 校验验证码所在地址（host:<eportal 端口>）
QUrl PortalClient::eportalUrl(const QString& path) const
{
	QUrl url = loginUrl(path);
	url.setPort(m_controller->portalPort());
	return url;
}
