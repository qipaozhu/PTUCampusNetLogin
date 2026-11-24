#pragma once

#include <QObject>
#include <QString>
#include <QThreadPool>

/// 保存 / 读取校园网账号信息（JSON），以及系统设置。
///
/// 系统设置（开机自启动 / 阻止自动弹出浏览器）都可能碰到阻塞操作
/// —— 前者写注册表，后者要等 UAC 提权 —— 所以统一放到后台线程执行，
/// 界面立刻返回，通过 xxxBusy / 结果信号更新。
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
		/// 当前平台是否支持「阻止系统自动弹出浏览器」（只有 Windows 有这套策略）
		Q_PROPERTY(bool supportsBrowserLaunchPolicy READ supportsBrowserLaunchPolicy CONSTANT)
		Q_PROPERTY(bool blockAutoBrowser READ blockAutoBrowser WRITE setBlockAutoBrowser
			NOTIFY blockAutoBrowserChanged)
		/// 上一次修改「阻止自动弹出浏览器」失败的原因，成功后为空串
		Q_PROPERTY(QString blockAutoBrowserError READ blockAutoBrowserError
			NOTIFY blockAutoBrowserErrorChanged)
		/// 正在后台改写上面那个策略（可能要等 UAC），界面据此禁用开关
		Q_PROPERTY(bool blockAutoBrowserBusy READ blockAutoBrowserBusy
			NOTIFY blockAutoBrowserBusyChanged)
		Q_PROPERTY(bool autoStart READ autoStart WRITE setAutoStart NOTIFY autoStartChanged)
		/// 正在后台写开机自启动
		Q_PROPERTY(bool autoStartBusy READ autoStartBusy NOTIFY autoStartBusyChanged)
		Q_PROPERTY(bool startedAtBoot READ startedAtBoot CONSTANT)

public:
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

	/// 当前进程是否以管理员权限运行
	bool isAdmin() const { return m_isAdmin; }

	/// 当前平台是否支持「阻止系统自动弹出浏览器」
	bool supportsBrowserLaunchPolicy() const;

	/// 是否阻止系统在检测到需要认证时自动弹出浏览器。
	/// 返回的是缓存的当前状态，修改请用 setBlockAutoBrowser（异步）
	bool blockAutoBrowser() const { return m_blockAutoBrowser; }
	/// 异步修改：立即返回，结果通过 blockAutoBrowserChanged / blockAutoBrowserError 通知
	void setBlockAutoBrowser(bool blocked);

	/// 上一次修改失败的原因，成功后为空串
	QString blockAutoBrowserError() const { return m_blockAutoBrowserError; }
	bool blockAutoBrowserBusy() const { return m_blockAutoBrowserBusy; }

	/// 是否随系统启动
	bool autoStart() const { return m_autoStart; }
	/// 异步修改：立即返回，结果通过 autoStartChanged 通知
	void setAutoStart(bool enabled);
	bool autoStartBusy() const { return m_autoStartBusy; }

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
	void blockAutoBrowserChanged();
	void blockAutoBrowserErrorChanged();
	void blockAutoBrowserBusyChanged();
	void autoStartChanged();
	void autoStartBusyChanged();

private:
	/// 后台改「阻止自动弹出浏览器」：写注册表或者弹 UAC，结果回主线程收尾
	void applyBlockAutoBrowser(bool blocked);
	/// 后台改开机自启动：写 / 删注册表项
	void applyAutoStart(bool enabled);

	QString m_username;
	QString m_password;

	QString m_portalAddress = QStringLiteral("192.168.116.8:801");
	QString m_portalHost = QStringLiteral("192.168.116.8");
	int m_portalPort = 801;

	/// 系统设置的后台执行线程：一共只有一条，保证注册表和 UAC 操作串行
	QThreadPool m_workPool;

	bool m_isAdmin = true;
	bool m_startedAtBoot = false;

	bool m_autoStart = false;
	bool m_autoStartBusy = false;

	bool m_blockAutoBrowser = false;
	bool m_blockAutoBrowserBusy = false;
	QString m_blockAutoBrowserError;
};