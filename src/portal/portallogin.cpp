#include "portallogin.h"

#include "captchaocr.h"
#include "core/controller.h"
#include "net/network.h"
#include "portalclient.h"

#include <QDebug>
#include <QDesktopServices>
#include <QTimer>

#include <utility>

namespace
{
	/// 验证码最多尝试次数（OCR 识别失败、校验不通过都算一次，超过后转手动登录）
	const int kMaxCaptchaAttempts = 30;
	/// 等 Network 检测结果的最长时间
	const int kOnlineCheckTimeoutMs = 10000;

	/// 探测不到 Portal 时用浏览器打开的地址（NCSI 的跳转页）
	const char* const kManualLoginUrl = "http://www.msftconnecttest.com/redirect";

	/// 验证码必须是 4 位纯数字
	bool isValidCaptchaText(const QString& text)
	{
		if (text.size() != 4)
			return false;

		for (const QChar ch : text)
		{
			if (!ch.isDigit())
				return false;
		}
		return true;
	}
}

/// 构造函数：网络请求交给 PortalClient，这里只管流程和状态
PortalLogin::PortalLogin(Controller* controller, Network* network,
	QObject* parent)
	: QObject(parent)
	, m_controller(controller)
	, m_network(network)
	, m_client(new PortalClient(controller, this))
	, m_onlineTimeout(new QTimer(this))
{
	m_onlineTimeout->setSingleShot(true);

	// 等 Network 超时：按“没能确认在线”回调，流程继续往下走
	connect(m_onlineTimeout, &QTimer::timeout, this, [this]()
		{
			QObject::disconnect(m_onlineConnection);

			auto callback = std::move(m_onlineCallback);
			m_onlineCallback = nullptr;
			if (callback)
				callback(OnlineResult::Timeout);
		});
}

/// 析构：取消还没完成的请求
PortalLogin::~PortalLogin()
{
	m_client->abort();
}

// ============================ 对外入口 ============================

/// 开始一轮自动登录（用户手动发起）
void PortalLogin::start()
{
	startInternal(false);
}

/// 开始一轮后台自动重连：和手动登录同一套流程，只是失败时不弹浏览器
void PortalLogin::startInBackground()
{
	startInternal(true);
}

/// 手动登录和后台重连共用的入口
void PortalLogin::startInternal(bool background)
{
	if (m_background != background)
	{
		m_background = background;
		emit backgroundChanged();
	}

	if (m_running)
		return;

	m_client->abort();

	if (m_controller->username().isEmpty() || m_controller->password().isEmpty())
	{
		// 账号都没填，重试也没意义：把这次算作“手动”失败，
		// 免得 AutoReconnect 一直按退避循环重试
		if (m_background)
		{
			m_background = false;
			emit backgroundChanged();
		}

		setMessage(QStringLiteral("请先在设置中填写校园网账号和密码"));
		m_controller->setLoginState(Controller::LoginFailed);
		setState(Failed);
		return;
	}

	m_running = true;
	emit runningChanged();

	m_captchaAttempts = 0;
	m_captchaText.clear();

	m_controller->setLoginState(Controller::LoggingIn);
	checkOnline();
}

/// 中止当前登录流程
void PortalLogin::stop()
{
	QObject::disconnect(m_onlineConnection);
	m_onlineCallback = nullptr;
	m_onlineTimeout->stop();
	m_client->abort();

	if (!m_running)
		return;

	m_running = false;
	emit runningChanged();
	setState(Idle);
	setMessage(QString());
	m_controller->setLoginState(Controller::NotLoggedIn);
}

/// 注销校园网登录（接口路径按参考流程推测，联调时可能需要调整）
void PortalLogin::logout()
{
	if (m_running)
		stop();
	else
		m_client->abort();

	setMessage(QStringLiteral("暂时不支持注销…请手动打开浏览器"));

	//m_client->logout([this](const PortalClient::Response& response)
	//	{
	//		// 不管接口返回什么，本地都恢复成未登录，避免界面卡在“登录成功”
	//		m_controller->setLoginState(Controller::NotLoggedIn);
	//		setState(Idle);

	//		if (response.transportOk() && response.status >= 200 && response.status < 400)
	//			setMessage(QStringLiteral("已注销校园网登录"));
	//		else
	//			setMessage(QStringLiteral("注销请求未成功，请确认当前网络状态"));
	//	});
}

/// 用户已手动完成认证（或程序启动前就已联网）：跳过登录流程，直接标记为已登录
void PortalLogin::markManuallyLoggedIn()
{
	if (m_running)
		stop();

	m_controller->setLoginState(Controller::LoggedIn);
	setState(Online);
	setMessage(QStringLiteral("已标记为已登录（手动登录或启动前已认证）"));
}

/// 启动时已经能上网：再探一下 Portal 是否可达，能到就认为处在校园网且已登录
void PortalLogin::detectExistingSession()
{
	if (m_running)
		return;

	m_client->probePortal([this](bool reachable)
		{
			if (reachable)
			{
				markManuallyLoggedIn();
				return;
			}

			// 能上网但访问不到 Portal，多半不在校园网，保持“未登录”不动
			setMessage(QStringLiteral("当前可上网，但未检测到校园网 Portal，未标记为已登录"));
		});
}

// ============================ 流程步骤 ============================

/// 第一步：先看是不是本来就能上网，能上网就不用登录了
void PortalLogin::checkOnline()
{
	setState(Checking);
	setMessage(QStringLiteral("正在检查网络状态…"));

	queryOnline([this](OnlineResult result)
		{
			if (!m_running)
				return;

			// 能上网说明已经认证过；超时和离线都继续去探 Portal
			if (result == OnlineResult::Online)
			{
				finishSuccess(QStringLiteral("当前已经可以正常上网"));
				return;
			}

			probePortal();
		});
}

/// 第二步：探测 Portal 是否可访问，不可访问就退化成手动登录
void PortalLogin::probePortal()
{
	setState(ProbingPortal);
	setMessage(QStringLiteral("正在检测 Portal 页面是否可访问…"));

	m_client->probePortal([this](bool reachable)
		{
			if (!m_running)
				return;

			if (reachable)
			{
				fetchCaptcha();
				return;
			}

			// 本地根本没连上网（典型场景：开机自启动时 Wi-Fi 还没关联完成），
			// 打开浏览器也只会是一个错误页，直接提示失败，等用户自己连网
			if (!m_network->lanConnected())
			{
				fail(QStringLiteral("没有检测到网络连接，请先连接校园网"));
				return;
			}

			// 后台自动重连时不能弹浏览器：否则每隔几分钟就弹一个窗口
			if (m_background)
			{
				fail(QStringLiteral("当前网络疑似不是校园网，稍后会自动重试"));
				return;
			}

			QDesktopServices::openUrl(QString::fromLatin1(kManualLoginUrl));
			fail(QStringLiteral("疑似非校园网，已打开浏览器，请手动登录"));
		});
}

/// 第三步：取验证码图片
void PortalLogin::fetchCaptcha()
{
	if (m_captchaAttempts >= kMaxCaptchaAttempts)
	{
		fail(QStringLiteral("验证码已尝试 %1 次仍未通过，需要手动登录")
			.arg(kMaxCaptchaAttempts));
		return;
	}

	setState(FetchingCaptcha);
	setMessage(QStringLiteral("正在获取验证码（第 %1/%2 次）…")
		.arg(m_captchaAttempts + 1)
		.arg(kMaxCaptchaAttempts));

	m_client->fetchCaptcha([this](const QImage& image, const QString& error)
		{
			if (!m_running)
				return;

			if (!error.isEmpty() || image.isNull())
			{
				fail(error.isEmpty()
					? QStringLiteral("获取验证码失败，需要手动登录")
					: error);
				return;
			}

			recognizeCaptcha(image);
		});
}

/// 第四步：OCR 识别，识别结果不合法就重新取码
void PortalLogin::recognizeCaptcha(const QImage& image)
{
	setState(Recognizing);
	setMessage(QStringLiteral("正在识别验证码…"));

	if (!CaptchaOcr::available())
	{
		fail(QStringLiteral("未集成验证码 OCR SDK（Tesseract），需要手动登录"));
		return;
	}

	const QString text = CaptchaOcr::recognize(image);
	if (!isValidCaptchaText(text))
	{
		qDebug() << "错误的验证码识别结果：" << text;
		if (retryCaptchaOrFail(QStringLiteral("识别结果不是 4 位数字")))
			fetchCaptcha();
		return;
	}

	m_captchaText = text;
	checkCaptcha();
}

/// 第五步：先校验验证码，通过了才提交登录
void PortalLogin::checkCaptcha()
{
	setState(CheckingCaptcha);
	setMessage(QStringLiteral("正在校验验证码（第 %1/%2 次）…")
		.arg(m_captchaAttempts + 1)
		.arg(kMaxCaptchaAttempts));

	m_client->checkCaptcha(m_captchaText, [this](const PortalClient::Result& result)
		{
			if (!m_running)
				return;

			if (result.ok)
			{
				login();
				return;
			}

			if (retryCaptchaOrFail(result.message))
				fetchCaptcha();
		});
}

/// 第六步：提交账号密码
void PortalLogin::login()
{
	setState(LoggingIn);
	setMessage(QStringLiteral("正在登录…"));

	m_client->login(m_controller->username(), m_controller->password(),
		[this](const PortalClient::Result& result)
		{
			if (!m_running)
				return;

			if (result.ok)
			{
				verifyOnline();
				return;
			}

			fail(result.message.isEmpty()
				? QStringLiteral("登录失败")
				: QStringLiteral("登录失败：%1").arg(result.message));
		});
}

/// 第七步：登录后再确认一次真的能上网
void PortalLogin::verifyOnline()
{
	setState(Checking);
	setMessage(QStringLiteral("正在确认登录结果…"));

	queryOnline([this](OnlineResult result)
		{
			if (!m_running)
				return;

			switch (result)
			{
			case OnlineResult::Online:
				finishSuccess(QStringLiteral("登录成功，已可正常上网"));
				break;
			case OnlineResult::Timeout:
				fail(QStringLiteral("网络状态检测超时"));
				break;
			case OnlineResult::Offline:
				fail(QStringLiteral("登录请求已提交，但仍无法访问互联网"));
				break;
			}
		});
}

// ============================ 内部工具 ============================

/// 让 Network 检测一次；成功 / 失败 / 超时都会回调，保证流程不会悬着
void PortalLogin::queryOnline(std::function<void(OnlineResult)> callback)
{
	QObject::disconnect(m_onlineConnection);
	m_onlineTimeout->stop();

	m_onlineCallback = std::move(callback);

	m_onlineConnection = connect(m_network, &Network::onlineChecked, this,
		[this](bool online)
		{
			QObject::disconnect(m_onlineConnection);
			m_onlineTimeout->stop();

			auto pending = std::move(m_onlineCallback);
			m_onlineCallback = nullptr;
			if (pending)
				pending(online ? OnlineResult::Online : OnlineResult::Offline);
		});

	m_onlineTimeout->start(kOnlineCheckTimeoutMs);
	m_network->refresh();
}

/// 验证码失败一次：还有次数继续重试，次数用尽就失败
bool PortalLogin::retryCaptchaOrFail(const QString& reason)
{
	++m_captchaAttempts;
	if (m_captchaAttempts < kMaxCaptchaAttempts)
		return true;

	fail(reason.isEmpty()
		? QStringLiteral("验证码已尝试 %1 次仍未通过，需要手动登录")
		.arg(kMaxCaptchaAttempts)
		: QStringLiteral("验证码已尝试 %1 次仍未通过：%2")
		.arg(kMaxCaptchaAttempts)
		.arg(reason));
	return false;
}

/// 设置登录状态并通知界面
void PortalLogin::setState(State state)
{
	if (state == m_state)
		return;

	m_state = state;
	emit stateChanged();
}

/// 更新提示文案并通知界面
void PortalLogin::setMessage(const QString& message)
{
	if (message == m_message)
		return;

	m_message = message;
	emit messageChanged();
}

/// 登录成功：更新状态和提示
void PortalLogin::finishSuccess(const QString& message)
{
	if (m_running)
	{
		m_running = false;
		emit runningChanged();
	}

	m_controller->setLoginState(Controller::LoggedIn);
	setState(Online);
	setMessage(message);
}

/// 登录失败：更新状态和提示
void PortalLogin::fail(const QString& message)
{
	if (m_running)
	{
		m_running = false;
		emit runningChanged();
	}

	m_controller->setLoginState(Controller::LoginFailed);
	setState(Failed);
	setMessage(message);
}
