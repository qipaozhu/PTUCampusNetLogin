#pragma once

#include <QImage>
#include <QNetworkReply>
#include <QObject>
#include <QString>
#include <QUrl>

#include <functional>

class Controller;
class QNetworkAccessManager;

/// 校园网 Portal 的协议层：拼 URL、发请求、解析返回。
/// 它不认识登录状态机，也不改任何界面状态；流程控制全部在 PortalLogin 里。
/// 好处是协议细节（哪个路径、哪个参数、JSONP 怎么解析）只出现在这一处，
/// 联调时也只需要看这一个文件。
class PortalClient : public QObject
{
	Q_OBJECT
public:
	/// 一次请求的结果：网络层细节都在这里收敛
	struct Response
	{
		QNetworkReply::NetworkError error = QNetworkReply::NoError;
		int status = 0;
		QByteArray body;
		QString contentType;

		/// 请求本身成功（不看 HTTP 状态码）
		bool transportOk() const { return error == QNetworkReply::NoError; }
		/// 传输成功且状态码为 200
		bool ok() const { return transportOk() && status == 200; }
		/// 收到了任意 HTTP 响应就算连通
		bool reachable() const { return transportOk() || status > 0; }
	};

	/// 业务结果：ok 表示接口返回 result == 1，message 是接口给的原因
	struct Result
	{
		bool ok = false;
		QString message;
	};

	explicit PortalClient(Controller* controller, QObject* parent = nullptr);

	/// Portal 根页面是否可访问（只看连通性，不看返回内容）
	void probePortal(std::function<void(bool reachable)> callback);

	/// 取验证码图片；失败时 image 为空，error 是给用户看的原因
	void fetchCaptcha(std::function<void(const QImage& image, const QString& error)> callback);

	/// 校验验证码
	void checkCaptcha(const QString& text, std::function<void(const Result& result)> callback);

	/// 提交账号密码登录
	void login(const QString& username, const QString& password,
		std::function<void(const Result& result)> callback);

	/// 注销,暂时不可用
	void logout(std::function<void(const Response& response)> callback);

	/// 取消尚未完成的请求，并丢弃它的回调
	void abort();

private:
	/// 统一的 GET：自动取消上一个请求，过滤掉被取代的响应
	void get(const QUrl& url, std::function<void(const Response&)> handler);

	/// 登录接口所在地址（host:80）
	QUrl loginUrl(const QString& path) const;
	/// 取码 / 校验验证码所在地址（host:<eportal 端口>）
	QUrl eportalUrl(const QString& path) const;

	Controller* m_controller = nullptr;
	QNetworkAccessManager* m_manager = nullptr;
	QNetworkReply* m_reply = nullptr;
};
