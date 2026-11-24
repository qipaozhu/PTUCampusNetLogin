#include "portallogin.h"

#include "captchaocr.h"
#include "core/controller.h"
#include "net/network.h"
#include "portalclient.h"

#include <QDebug>
#include <QDesktopServices>
#include <QTimer>

#include <utility>

#include "utils/textutils.h"

// 流程参数：集中放在文件开头，改流程节奏只需要看这一处
/// 验证码最多尝试次数（OCR 识别失败、校验不通过都算一次，超过后转手动登录）
constexpr int kMaxCaptchaAttempts = 30;
/// 登录后复检最多重试次数：刚切换网络时首包失败很常见
constexpr int kMaxVerifyAttempts = 1;
/// 复检失败后等多久再试
constexpr int kVerifyRetryDelayMs = 1500;
/// 探测不到 Portal 时用浏览器打开的地址（NCSI 的跳转页）
constexpr const char* kManualLoginUrl = "http://www.msftconnecttest.com/redirect";

/// 构造：网络请求交给 PortalClient、OCR 交给工作线程，本类只管流程和状态
PortalLogin::PortalLogin(Controller* controller, Network* network, QObject* parent)
	: QObject(parent)
	, m_controller(controller)
	, m_network(network)
	, m_client(new PortalClient(controller, this))
	, m_ocr(new CaptchaOcr(this))
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

/// 析构：取消还没完成的网络请求（OCR 线程由 CaptchaOcr 自己回收）
PortalLogin::~PortalLogin()
{
	m_client->abort();
}

// ============================ 对外入口 ============================

/// 用户手动点「登录」：走完整流程，失败时允许弹浏览器引导手动认证
void PortalLogin::start()
{
	startInternal(false);
}

/// 后台自动重连入口：和 start() 同一套流程，但失败既不弹浏览器也不刷 Toast
void PortalLogin::startInBackground()
{
	startInternal(true);
}

/// 两个入口的公共实现：清掉上一次的残留、校验账号、把流程推进到第一步
void PortalLogin::startInternal(bool background)
{
	// 先判断在途流程：否则后台重连会把正在跑的手动登录改成“后台任务”，
	// 失败后又被 AutoReconnect 当成后台失败继续退避重试
	if (m_running)
		return;

	if (m_background != background)
	{
		m_background = background;
		emit backgroundChanged();
	}

	m_client->abort();
	m_ocr->cancel();

	if (m_controller->username().isEmpty() || m_controller->password().isEmpty())
	{
		// 账号都没填，重试也没意义：把这次算作“手动”失败，
		// 免得 AutoReconnect 一直按退避循环重试
		if (m_background)
		{
			m_background = false;
			emit backgroundChanged();
		}

		setMessage(QStringLiteral("请先在设置里填写账号和密码"));
		setState(Failed);
		return;
	}

	m_running = true;
	emit runningChanged();

	m_captchaAttempts = 0;
	m_verifyAttempts = 0;
	m_captchaText.clear();
	m_captchaImage = QImage();

	// 新一轮流程：上一次「需要手动认证」的结论作废，让这次重新判断
	setPendingManualAuth(false);

	enterPhase(Phase::CheckOnline);
}

/// 中止当前流程：撤掉所有在途的异步操作，界面回到「未登录」
void PortalLogin::stop()
{
	QObject::disconnect(m_onlineConnection);
	m_onlineCallback = nullptr;
	m_onlineTimeout->stop();
	m_client->abort();
	m_ocr->cancel();

	if (!m_running)
		return;

	m_running = false;
	emit runningChanged();

	m_phase = Phase::Idle;
	setState(Idle);
	setMessage(QString());
}

/// 注销：注销接口暂不可用，这里只停掉流程并提示用户去浏览器手动操作
void PortalLogin::logout()
{
	if (m_running)
		stop();
	else
		m_client->abort();

	// 注销接口暂不可用，这里只给提示，不动本地登录状态
	setMessage(QStringLiteral("暂不支持注销，请手动打开浏览器"));
}

/// 用户已在浏览器里完成认证（或启动前就联网）：跳过流程，直接标记为已登录
void PortalLogin::markManuallyLoggedIn()
{
	if (m_running)
		stop();

	m_phase = Phase::Done;
	setState(Online);
	setMessage(QStringLiteral("已标记为已登录（手动确认）"));
}

/// 启动时已经能上网：先确认 Portal 是否可达，是校园网才标记为已登录
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
			setMessage(QStringLiteral("可上网，但未检测到校园网 Portal"));
		});
}

// ============================ 流程中枢 ============================

/// 全流程唯一的状态迁移表：读这一个函数就知道“什么情况下去哪一步”
void PortalLogin::dispatch(Event event, const QString& detail)
{
	// 唯一的中止守卫：流程被 stop() / 失败终止后，所有迟到的回调都在这里丢掉
	if (!m_running)
		return;

	switch (m_phase)
	{
	case Phase::CheckOnline:
		switch (event)
		{
		case Event::OnlineOk:
			// 本来就能上网，说明已经认证过
			finishSuccess(QStringLiteral("当前已经可以正常上网"));
			return;
		case Event::OnlineOffline:
		case Event::OnlineTimeout:
			// 连不上外网不代表不在校园网，继续去探 Portal
			enterPhase(Phase::ProbePortal);
			return;
		default:
			break;
		}
		break;

	case Phase::ProbePortal:
		switch (event)
		{
		case Event::PortalUp: enterPhase(Phase::FetchCaptcha); return;
		case Event::PortalDown: handlePortalUnreachable(); return;
		default: break;
		}
		break;

	case Phase::FetchCaptcha:
		switch (event)
		{
		case Event::CaptchaFetched:
			enterPhase(Phase::Recognize);
			return;
		case Event::CaptchaFetchFailed:
			fail(detail.isEmpty() ? QStringLiteral("获取验证码失败，请手动登录") : detail);
			return;
		default:
			break;
		}
		break;

	case Phase::Recognize:
		switch (event)
		{
		case Event::OcrAccepted:
			m_captchaText = detail;
			enterPhase(Phase::CheckCaptcha);
			return;
		case Event::OcrRejected:
			retryCaptcha(detail.isEmpty()
				? QStringLiteral("识别结果不是 4 位数字")
				: detail);
			return;
		default:
			break;
		}
		break;

	case Phase::CheckCaptcha:
		switch (event)
		{
		case Event::CaptchaAccepted:
			enterPhase(Phase::Login);
			return;
		case Event::CaptchaRejected:
			retryCaptcha(detail);
			return;
		default:
			break;
		}
		break;

	case Phase::Login:
		switch (event)
		{
		case Event::LoginOk:
			enterPhase(Phase::VerifyOnline);
			return;
		case Event::LoginFailed:
			fail(detail.isEmpty()
				? QStringLiteral("登录失败")
				: QStringLiteral("登录失败：%1").arg(TextUtils::abbreviate(detail, 28)));
			return;
		default:
			break;
		}
		break;

	case Phase::VerifyOnline:
		switch (event)
		{
		case Event::OnlineOk:
			finishSuccess(QStringLiteral("登录成功，已可正常上网"));
			return;
		case Event::OnlineTimeout:
			fail(QStringLiteral("网络状态检测超时"));
			return;
		case Event::OnlineOffline:
			fail(QStringLiteral("登录已提交，但仍无法上网"));
			return;
		default:
			break;
		}
		break;

	default:
		break;
	}

	// 走到这里说明阶段和事件对不上（漏了分支，或某一步发了不该发的事件）
	qWarning() << "PortalLogin: 未处理的流程事件" << static_cast<int>(event)
		<< "于阶段" << static_cast<int>(m_phase);
}

/// 进入某个阶段：只负责切换到该阶段对应的那一步
void PortalLogin::enterPhase(Phase phase)
{
	m_phase = phase;

	switch (phase)
	{
	case Phase::CheckOnline: beginCheckOnline(); break;
	case Phase::ProbePortal: beginProbePortal(); break;
	case Phase::FetchCaptcha: beginFetchCaptcha(); break;
	case Phase::Recognize: beginRecognizeCaptcha(); break;
	case Phase::CheckCaptcha: beginCheckCaptcha(); break;
	case Phase::Login: beginLogin(); break;
	case Phase::VerifyOnline: beginVerifyOnline(); break;
	case Phase::Idle:
	case Phase::Done:
		break;
	}
}

// ============================ 各阶段 ============================

/// 第一步：先看是不是本来就能上网，能上网就不用登录了
void PortalLogin::beginCheckOnline()
{
	// 后台重连不走 “检查中” 这个展示状态：一次误判就会在界面上闪一次
	// “登录中” Toast，看起来就像重复登录
	if (!m_background)
	{
		setState(Checking);
		setMessage(QStringLiteral("正在检查网络状态…"));
	}

	requestOnlineCheck();
}

/// 第二步：探测 Portal 是否可访问，不可访问就退化成手动登录
void PortalLogin::beginProbePortal()
{
	setState(ProbingPortal);
	setMessage(QStringLiteral("正在检测校园网 Portal…"));

	m_client->probePortal([this](bool reachable)
		{
			dispatch(reachable ? Event::PortalUp : Event::PortalDown);
		});
}

/// 第三步：取验证码图片
void PortalLogin::beginFetchCaptcha()
{
	if (m_captchaAttempts >= kMaxCaptchaAttempts)
	{
		fail(QStringLiteral("验证码连续 %1 次未通过，请手动登录")
			.arg(kMaxCaptchaAttempts));
		return;
	}

	setState(FetchingCaptcha);
	setMessage(QStringLiteral("正在获取验证码（第 %1/%2 次）…")
		.arg(m_captchaAttempts + 1)
		.arg(kMaxCaptchaAttempts));

	m_client->fetchCaptcha([this](const QImage& image, const QString& error)
		{
			if (!error.isEmpty() || image.isNull())
			{
				dispatch(Event::CaptchaFetchFailed, error);
				return;
			}

			m_captchaImage = image;
			dispatch(Event::CaptchaFetched);
		});
}

/// 第四步：OCR 识别（跑在专属工作线程里，这里只等回调）
void PortalLogin::beginRecognizeCaptcha()
{
	setState(Recognizing);
	setMessage(QStringLiteral("正在识别验证码…"));

	if (!CaptchaOcr::available())
	{
		fail(QStringLiteral("缺少验证码识别组件，请手动登录"));
		return;
	}

	// 交给 OCR 线程后本对象就不再需要这张图了（QImage 隐式共享，拷贝很便宜）
	const QImage image = m_captchaImage;
	m_captchaImage = QImage();

	m_ocr->recognize(image, [this](const QString& text)
		{
			if (!TextUtils::isValidCaptchaText(text))
			{
				qDebug() << "错误的验证码识别结果：" << text;
				dispatch(Event::OcrRejected, QStringLiteral("识别结果不是 4 位数字"));
				return;
			}

			dispatch(Event::OcrAccepted, text);
		});
}

/// 第五步：先校验验证码，通过了才提交登录
void PortalLogin::beginCheckCaptcha()
{
	setState(CheckingCaptcha);
	setMessage(QStringLiteral("正在校验验证码（第 %1/%2 次）…")
		.arg(m_captchaAttempts + 1)
		.arg(kMaxCaptchaAttempts));

	m_client->checkCaptcha(m_captchaText, [this](const PortalClient::Result& result)
		{
			dispatch(result.ok ? Event::CaptchaAccepted : Event::CaptchaRejected,
				result.message);
		});
}

/// 第六步：提交账号密码
void PortalLogin::beginLogin()
{
	setState(LoggingIn);
	setMessage(QStringLiteral("正在登录…"));

	m_client->login(m_controller->username(), m_controller->password(),
		[this](const PortalClient::Result& result)
		{
			dispatch(result.ok ? Event::LoginOk : Event::LoginFailed, result.message);
		});
}

/// 第七步：登录后再确认一次真的能上网（离线时先重试一次，避免刚切网就误判失败）
void PortalLogin::beginVerifyOnline()
{
	if (!m_background)
	{
		setState(Checking);
		setMessage(QStringLiteral("正在确认登录结果…"));
	}

	queryOnline([this](OnlineResult result)
		{
			// 刚提交完登录，路由和 DNS 可能还没落定，首包失败不代表登录没生效：
			// 直接判失败会让 AutoReconnect 立刻重登，表现就是“成功了还在重复登录”
			if (result == OnlineResult::Offline && m_verifyAttempts < kMaxVerifyAttempts)
			{
				++m_verifyAttempts;
				QTimer::singleShot(kVerifyRetryDelayMs, this, [this]()
					{
						if (m_running && m_phase == Phase::VerifyOnline)
							beginVerifyOnline();
					});
				return;
			}

			dispatch(result == OnlineResult::Online ? Event::OnlineOk
				: result == OnlineResult::Timeout ? Event::OnlineTimeout
				: Event::OnlineOffline);
		});
}

// ============================ 内部工具 ============================

/// 发起一次在线检测，并把三种结果直接映射成流程事件
void PortalLogin::requestOnlineCheck()
{
	queryOnline([this](OnlineResult result)
		{
			dispatch(result == OnlineResult::Online ? Event::OnlineOk
				: result == OnlineResult::Timeout ? Event::OnlineTimeout
				: Event::OnlineOffline);
		});
}

/// Portal 探测不到时的处置：没网卡就提示，能上网但不是本校 Portal 就开浏览器
void PortalLogin::handlePortalUnreachable()
{
	// 本地根本没连上网（典型场景：开机自启动时 Wi-Fi 还没关联完成），
	// 打开浏览器也只会是一个错误页，直接提示失败，等用户自己连网
	if (!m_network->lanConnected())
	{
		fail(QStringLiteral("没有检测到网络连接，请先连接校园网"));
		return;
	}

	// 能上网但这个地址上不是本校 Portal（酒店、机场、别的校园网……）：
	// 自动登录帮不上忙，只能开浏览器让用户自己认证。
	// 同时打上「需要手动认证」标记，AutoReconnect 看到就不再无限重试，
	// 所以即使是后台重连触发的，这里也只会弹这一次，不会反复弹窗
	setPendingManualAuth(true);
	QDesktopServices::openUrl(QString::fromLatin1(kManualLoginUrl));
	fail(QStringLiteral("疑似非校园网，已在浏览器中打开认证页"));
}

/// 验证码这一次没通过：还有次数就回到取码重来，次数用尽则整体失败
void PortalLogin::retryCaptcha(const QString& reason)
{
	++m_captchaAttempts;
	if (m_captchaAttempts < kMaxCaptchaAttempts)
	{
		enterPhase(Phase::FetchCaptcha);
		return;
	}

	fail(reason.isEmpty()
		? QStringLiteral("验证码连续 %1 次未通过，请手动登录")
		.arg(kMaxCaptchaAttempts)
		: QStringLiteral("验证码连续 %1 次未通过：%2")
		.arg(kMaxCaptchaAttempts)
		.arg(TextUtils::abbreviate(reason, 18)));
}

/// 让 Network 检测一次；成功、失败、超时都会回调，保证流程不会悬着
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

	// 等结果的超时和底层探测的超时用同一个常量，不会出现"上层先放弃"的情况
	m_onlineTimeout->start(Network::kProbeTimeoutMs);
	m_network->refresh();
}

// ============================ 状态维护 ============================

/// 更新对外展示的登录状态并通知界面。
/// 状态 → Toast 的映射也只在这里定义一份，QML 收到什么就显示什么
void PortalLogin::setState(State state)
{
	if (state == m_state)
		return;

	m_state = state;
	emit stateChanged();

	// 只有这四种状态需要提示用户；探测、取码、识别这些中间状态不打扰界面
	const QString kind = (state == Checking) ? QStringLiteral("processing")
		: (state == Online) ? QStringLiteral("success")
		: (state == Failed) ? QStringLiteral("failed")
		: (state == Idle) ? QStringLiteral("hide")
		: QString();

	if (!kind.isEmpty())
		emit toastRequested(kind);
}

/// 更新提示文案并通知界面
void PortalLogin::setMessage(const QString& message)
{
	if (message == m_message)
		return;

	m_message = message;
	emit messageChanged();
}

/// 流程成功收尾：停掉运行标记、写入登录状态、更新展示
void PortalLogin::finishSuccess(const QString& message)
{
	if (m_running)
	{
		m_running = false;
		emit runningChanged();
	}

	m_phase = Phase::Done;
	setPendingManualAuth(false);
	setState(Online);
	setMessage(message);
}

/// 更新「需要用户自己认证」标记：变了才发信号
void PortalLogin::setPendingManualAuth(bool pending)
{
	if (pending == m_pendingManualAuth)
		return;

	m_pendingManualAuth = pending;
	emit pendingManualAuthChanged();
}

/// 把 9 个精细状态归并成界面需要的 4 种
LoginState::Kind PortalLogin::loginKind() const
{
	switch (m_state)
	{
	case Online: return LoginState::Kind::LoggedIn;
	case Failed: return LoginState::Kind::Failed;
	case Idle: return LoginState::Kind::NotLoggedIn;
	default: return LoginState::Kind::LoggingIn;
	}
}

/// 粗粒度状态标识，给 QML 判断用
QString PortalLogin::statusKind() const
{
	return LoginState::kindName(loginKind());
}

/// 粗粒度状态文案，托盘和界面共用
QString PortalLogin::statusText() const
{
	return LoginState::text(loginKind());
}

/// 流程失败收尾：停掉运行标记、写入失败状态、更新展示
void PortalLogin::fail(const QString& message)
{
	if (m_running)
	{
		m_running = false;
		emit runningChanged();
	}

	m_phase = Phase::Done;
	setState(Failed);
	setMessage(message);
}