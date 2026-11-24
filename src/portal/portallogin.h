#pragma once

#include <QImage>
#include <QObject>
#include <QString>

#include <functional>

#include "core/loginstate.h"

class CaptchaOcr;
class Controller;
class Network;
class PortalClient;
class QTimer;

/// 校园网 Portal 登录流程（事件表状态机）。
///
/// 改动流程时的约定：
///   - begin*() 一次只做一步：设展示状态、发一个异步请求，拿到结果只 dispatch() 报事件，
///     自己不知道下一步是什么；
///   - dispatch() 是唯一决定“下一步去哪”的地方，读它一个函数就能看完全部流程；
///   - Phase 是内部控制流阶段，State 是给 QML 看的展示状态，两者分开，
///     免得为了界面好看而扭曲控制流。
class PortalLogin : public QObject
{
	Q_OBJECT
		/// 粗粒度状态标识：notLoggedIn / loggingIn / loggedIn / failed。
		/// QML 用它判断，不要在 QML 里写状态数字
		Q_PROPERTY(QString statusKind READ statusKind NOTIFY stateChanged)
		/// 粗粒度状态的中文文案，托盘和界面共用同一份
		Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)
		Q_PROPERTY(QString message READ message NOTIFY messageChanged)
		Q_PROPERTY(bool running READ running NOTIFY runningChanged)
		/// 本次登录是否由后台自动重连发起：是的话不刷「处理中」Toast，
		/// 失败后是否重试由 AutoReconnect 决定
		Q_PROPERTY(bool background READ background NOTIFY backgroundChanged)
		/// 上一次失败是因为「不是本校校园网」：这种情况自动重试没有意义，
		/// AutoReconnect 看到这个标记就停下，等网络环境变化再试
		Q_PROPERTY(bool pendingManualAuth READ pendingManualAuth NOTIFY pendingManualAuthChanged)

public:
	/// 精细的登录状态。界面不要直接拿它做判断，用 statusKind / statusText
	enum State
	{
		Idle = 0,
		Checking,
		ProbingPortal,
		FetchingCaptcha,
		Recognizing,
		CheckingCaptcha,
		LoggingIn,
		Online,
		Failed
	};
	Q_ENUM(State)

		explicit PortalLogin(Controller* controller, Network* network,
			QObject* parent = nullptr);
	~PortalLogin() override;

	int state() const { return m_state; }
	QString statusKind() const;
	QString statusText() const;
	QString message() const { return m_message; }
	bool running() const { return m_running; }
	bool background() const { return m_background; }
	bool pendingManualAuth() const { return m_pendingManualAuth; }

	/// 用户手动发起一轮登录
	Q_INVOKABLE void start();
	/// 后台自动重连发起一轮登录（失败时不弹浏览器、不刷 Toast）
	void startInBackground();
	/// 中止当前流程，回到「未登录」
	Q_INVOKABLE void stop();
	/// 注销校园网登录（接口暂不可用，仅提示用户去浏览器手动操作）
	Q_INVOKABLE void logout();
	/// 用户已在浏览器完成认证：跳过流程直接标记为已登录
	Q_INVOKABLE void markManuallyLoggedIn();
	/// 启动时已经能上网：确认 Portal 可达后再标记为已登录。
	/// 由 StartupCoordinator 调用，QML 不参与启动决策，所以不导出给 QML
	void detectExistingSession();

signals:
	void stateChanged();
	void messageChanged();
	void runningChanged();
	void backgroundChanged();
	void pendingManualAuthChanged();
	/// 状态变化到需要提示用户时发出："processing" / "success" / "failed" / "hide"。
	/// 状态到 Toast 的映射只在 C++ 里定义一份，QML 收到什么就显示什么
	void toastRequested(const QString& kind);

private:
	/// “现在能不能上网”的三种结果，超时和离线要分开处理
	enum class OnlineResult
	{
		Online,
		Offline,
		Timeout
	};

	/// 流程事件：步骤做完只报“发生了什么”，不带“所以去干嘛”的意图
	enum class Event
	{
		OnlineOk,
		OnlineOffline,
		OnlineTimeout,
		PortalUp,
		PortalDown,
		CaptchaFetched,
		CaptchaFetchFailed,
		OcrAccepted,
		OcrRejected,
		CaptchaAccepted,
		CaptchaRejected,
		LoginOk,
		LoginFailed,
	};

	/// 内部的流程阶段（控制流用），和对外展示的 State 分开
	enum class Phase
	{
		Idle,
		CheckOnline,
		ProbePortal,
		FetchCaptcha,
		Recognize,
		CheckCaptcha,
		Login,
		VerifyOnline,
		Done,
	};

	/// 手动登录和后台重连共用的启动实现
	void startInternal(bool background);

	// ---- 流程中枢 ----
	/// 唯一的状态迁移决策点；detail 承载错误原因或识别出的验证码文本
	void dispatch(Event event, const QString& detail = QString());
	/// 进入某个阶段并启动该阶段对应的那一步
	void enterPhase(Phase phase);

	// ---- 各阶段：只做一步，结果一律交给 dispatch() ----
	/// 检查是否本来就能上网
	void beginCheckOnline();
	/// 探测 Portal 是否可访问
	void beginProbePortal();
	/// 取验证码图片
	void beginFetchCaptcha();
	/// 识别验证码（异步，OCR 跑在工作线程）
	void beginRecognizeCaptcha();
	/// 校验验证码
	void beginCheckCaptcha();
	/// 提交账号密码
	void beginLogin();
	/// 登录后复检是否真的能上网
	void beginVerifyOnline();

	/// 发起一次在线检测，把三种结果直接映射成流程事件
	void requestOnlineCheck();
	/// Portal 探测失败时的处置：没网卡 / 后台重连 / 需要手动登录
	void handlePortalUnreachable();
	/// 验证码这一次没通过：还有次数就回到取码，次数用尽则失败
	void retryCaptcha(const QString& reason);

	/// 让 Network 检测一次，结果通过回调返回（超时也算一次结果）
	void queryOnline(std::function<void(OnlineResult)> callback);

	// ---- 状态维护 ----
	/// 更新展示状态并通知界面
	void setState(State state);
	/// 更新提示文案并通知界面
	void setMessage(const QString& message);
	/// 成功收尾：清运行标记、置为已登录
	void finishSuccess(const QString& message);
	/// 失败收尾：清运行标记、置为登录失败
	void fail(const QString& message);
	/// 更新「需要用户自己认证」标记
	void setPendingManualAuth(bool pending);
	/// 把 9 个精细状态归并成界面需要的 4 种
	LoginState::Kind loginKind() const;

	Controller* m_controller = nullptr;
	Network* m_network = nullptr;
	PortalClient* m_client = nullptr;
	CaptchaOcr* m_ocr = nullptr;
	QTimer* m_onlineTimeout = nullptr;
	QMetaObject::Connection m_onlineConnection;
	std::function<void(OnlineResult)> m_onlineCallback;

	Phase m_phase = Phase::Idle;
	State m_state = Idle;
	QString m_message;
	bool m_running = false;
	bool m_background = false;
	bool m_pendingManualAuth = false;

	int m_captchaAttempts = 0;
	/// 登录后复检已经重试过几次（网络刚切换时首包容易失败）
	int m_verifyAttempts = 0;
	QString m_captchaText;
	QImage m_captchaImage;
};