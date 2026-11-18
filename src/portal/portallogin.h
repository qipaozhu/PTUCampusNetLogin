#pragma once

#include <QImage>
#include <QObject>
#include <QString>

#include <functional>

class Controller;
class Network;
class PortalClient;
class QTimer;

/// 校园网 Portal 登录流程（状态机）：
/// 检查网络 -> 探测 Portal -> 取验证码 -> OCR -> 校验验证码 -> 登录 -> 复检
/// 每个方法只做“一步”，做完通过回调进入下一步；
/// 协议细节（路径、参数、JSONP 解析）都在 PortalClient 里，不在这里。
class PortalLogin : public QObject
{
	Q_OBJECT
		Q_PROPERTY(int state READ state NOTIFY stateChanged)
		Q_PROPERTY(QString message READ message NOTIFY messageChanged)
		Q_PROPERTY(bool running READ running NOTIFY runningChanged)
		/// 本次登录是否由后台自动重连发起：是的话失败时不弹浏览器（Toast 照常提示）
		Q_PROPERTY(bool background READ background NOTIFY backgroundChanged)

public:
	/// 登录状态，和 QML 里的数字一一对应
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
	QString message() const { return m_message; }
	bool running() const { return m_running; }
	bool background() const { return m_background; }

	/// 开始一轮自动登录
	Q_INVOKABLE void start();
	/// 后台自动重连：和 start() 同一套流程，只是失败时不弹浏览器
	void startInBackground();
	/// 中止当前登录流程
	Q_INVOKABLE void stop();
	/// 注销校园网登录
	Q_INVOKABLE void logout();
	/// 已经手动登录（或程序启动前就已经联网）时，直接把状态标记为已登录
	Q_INVOKABLE void markManuallyLoggedIn();
	/// 启动时发现已经能上网：再确认一下是不是校园网环境，是的话标记为已登录
	Q_INVOKABLE void detectExistingSession();

signals:
	void stateChanged();
	void messageChanged();
	void runningChanged();
	void backgroundChanged();

private:
	/// “现在能不能上网”的三种结果，超时和离线要分开处理
	enum class OnlineResult
	{
		Online,
		Offline,
		Timeout
	};

	/// 手动登录和后台重连共用的入口
	void startInternal(bool background);

	// ---- 流程步骤：每个方法只做一步，然后进入下一步 ----
	void checkOnline();
	void probePortal();
	void fetchCaptcha();
	void recognizeCaptcha(const QImage& image);
	void checkCaptcha();
	void login();
	void verifyOnline();

	/// 让 Network 做一次检测；成功、失败、超时都会回调，不会让流程卡住
	void queryOnline(std::function<void(OnlineResult)> callback);

	/// 验证码失败一次：还有次数就返回 true（调用方继续重试），
	/// 次数用尽则置为失败并返回 false
	bool retryCaptchaOrFail(const QString& reason);

	// ---- 状态维护 ----
	void setState(State state);
	void setMessage(const QString& message);
	void finishSuccess(const QString& message);
	void fail(const QString& message);

	Controller* m_controller = nullptr;
	Network* m_network = nullptr;
	PortalClient* m_client = nullptr;
	QTimer* m_onlineTimeout = nullptr;
	QMetaObject::Connection m_onlineConnection;
	std::function<void(OnlineResult)> m_onlineCallback;

	State m_state = Idle;
	QString m_message;
	bool m_running = false;
	bool m_background = false;

	int m_captchaAttempts = 0;
	QString m_captchaText;
};
