#pragma once

#include <QObject>
#include <QString>

/// 保存 / 读取校园网账号信息（JSON），以及登录状态、系统设置
class Controller : public QObject
{
	Q_OBJECT
		Q_PROPERTY(QString username READ username WRITE setUsername NOTIFY usernameChanged)
		Q_PROPERTY(QString password READ password WRITE setPassword NOTIFY passwordChanged)
		Q_PROPERTY(QString portalAddress READ portalAddress NOTIFY portalAddressChanged)
		Q_PROPERTY(QString portalHost READ portalHost NOTIFY portalAddressChanged)
		Q_PROPERTY(int portalPort READ portalPort NOTIFY portalAddressChanged)
		Q_PROPERTY(QString configPath READ configPath CONSTANT)
		Q_PROPERTY(bool isAdmin READ isAdmin CONSTANT)
		Q_PROPERTY(int loginState READ loginState WRITE setLoginState NOTIFY loginStateChanged)
		Q_PROPERTY(bool blockAutoBrowser READ blockAutoBrowser WRITE setBlockAutoBrowser
			NOTIFY blockAutoBrowserChanged)
		/// 上一次修改「阻止自动弹出浏览器」失败的原因，成功后为空串
		Q_PROPERTY(QString blockAutoBrowserError READ blockAutoBrowserError
			NOTIFY blockAutoBrowserErrorChanged)
		Q_PROPERTY(bool autoStart READ autoStart WRITE setAutoStart NOTIFY autoStartChanged)
		Q_PROPERTY(bool startedAtBoot READ startedAtBoot CONSTANT)

public:
	/// 校园网登录状态，和 QML 里的数字一一对应
	enum LoginState
	{
		NotLoggedIn = 0,
		LoggingIn = 1,
		LoggedIn = 2,
		LoginFailed = 3
	};
	Q_ENUM(LoginState)

		explicit Controller(QObject* parent = nullptr);
	~Controller();

	QString username() const { return m_username; }
	void setUsername(const QString& username);

	QString password() const { return m_password; }
	void setPassword(const QString& password);

	/// 校园网 Portal 地址，永远是校验过的 "host:port"，形如 192.168.116.8:801
	QString portalAddress() const { return m_portalAddress; }
	/// Portal 主机名 / IP（从 portalAddress 解析得到）
	QString portalHost() const { return m_portalHost; }
	/// eportal 接口端口（默认 801，登录接口仍走 80）
	int portalPort() const { return m_portalPort; }

	/// 校验并应用 Portal 地址：
	/// 接受 192.168.116.8:801、192.168.116.8、http://192.168.116.8:801/ 等写法，
	/// 格式不合法时返回 false，且不修改已有地址
	Q_INVOKABLE bool applyPortalAddress(const QString& raw);

	QString configPath() const;

	int loginState() const { return m_loginState; }
	void setLoginState(int state);

	/// 当前进程是否以管理员权限运行
	bool isAdmin() const { return m_isAdmin; }

	/// 是否阻止系统在检测到需要认证时自动弹出浏览器。
	/// 靠的是组策略键 NoWebBrowserLaunch：
	/// HKLM\SOFTWARE\Policies\Microsoft\Windows\NetworkConnectivityStatusIndicator
	/// 只关"拉浏览器"，不动 NCSI 主动探测；属于机器级设置，写入需要管理员权限
	bool blockAutoBrowser() const;
	void setBlockAutoBrowser(bool blocked);

	/// 上一次修改失败的原因，成功后为空串
	QString blockAutoBrowserError() const { return m_blockAutoBrowserError; }

	/// 是否随 Windows 启动（写入 HKCU\...\Run，无需管理员权限）
	bool autoStart() const { return m_autoStart; }
	void setAutoStart(bool enabled);

	/// 本次进程是否由开机自启动拉起（命令行带 --autostart），用于决定要不要自动登录
	bool startedAtBoot() const { return m_startedAtBoot; }

	/// 把当前账号密码写入 JSON 配置文件
	Q_INVOKABLE bool save();
	/// 从 JSON 配置文件读取账号密码
	Q_INVOKABLE void load();

signals:
	void usernameChanged();
	void passwordChanged();
	void portalAddressChanged();
	void loginStateChanged();
	void blockAutoBrowserChanged();
	void blockAutoBrowserErrorChanged();
	void autoStartChanged();

private:
	QString m_username;
	QString m_password;

	QString m_portalAddress = QStringLiteral("192.168.116.8:801");
	QString m_portalHost = QStringLiteral("192.168.116.8");
	int m_portalPort = 801;

	int m_loginState = NotLoggedIn;
	bool m_isAdmin = true;
	bool m_autoStart = false;
	bool m_startedAtBoot = false;
	QString m_blockAutoBrowserError;
};
