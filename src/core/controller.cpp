#include "controller.h"

#include "platform/ncsiprobing.h"
#include "platform/platform.h"
#include "utils/portaladdress.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QRunnable>
#include <QSaveFile>
#include <QStandardPaths>
#include <QString>

namespace ControllerConfig
{
	// 开机拉起时带上这个参数，程序据此自动执行一次登录
	constexpr const char* kAutoStartArg = "--autostart";
	// 开机自启动在系统里的登记名
	constexpr const char* kAutostartName = "PTUCampusLogin";

	/// 系统里登记的自启动命令是否指向当前 exe（程序被移动过就视为未开启）
	bool isAutostartRegistered()
	{
		const QString command = Platform::autostartCommand(
			QString::fromLatin1(kAutostartName));
		if (command.isEmpty())
			return false;

		const QString exePath = QDir::toNativeSeparators(
			QCoreApplication::applicationFilePath());
		return command.contains(exePath, Qt::CaseInsensitive)
			&& command.contains(QString::fromLatin1(kAutoStartArg));
	}

	/// 写进注册表的自启动命令（路径带空格必须加引号）
	QString autostartCommand()
	{
		return QStringLiteral("\"%1\" %2")
			.arg(QDir::toNativeSeparators(QCoreApplication::applicationFilePath()),
				QString::fromLatin1(kAutoStartArg));
	}
}

/// 构造函数：检测管理员权限，读取系统设置并加载本地已保存的账号密码
Controller::Controller(QObject* parent)
	: QObject(parent)
{
	// 注册表和 UAC 操作串行执行，一条线程就够
	m_workPool.setMaxThreadCount(1);

	m_isAdmin = Platform::isElevated();

	// 由开机自启动拉起时命令行带 --autostart，界面据此自动登录
	m_startedAtBoot = QCoreApplication::arguments().contains(
		QString::fromLatin1(ControllerConfig::kAutoStartArg));

	// 两个系统设置的初始值在这里（主线程）读一次，之后都维护缓存
	m_autoStart = ControllerConfig::isAutostartRegistered();
	m_blockAutoBrowser = NcsiProbing::isBrowserLaunchBlocked();

	load();
}

/// 析构：等后台设置操作结束，避免回调打到已经析构的对象上
Controller::~Controller()
{
	m_workPool.waitForDone();
}

/// 设置校园网账号，值变化时通知界面刷新
void Controller::setUsername(const QString& username)
{
	if (username == m_username)
		return;

	m_username = username;
	emit usernameChanged();
}

/// 设置校园网密码，值变化时通知界面刷新
void Controller::setPassword(const QString& password)
{
	if (password == m_password)
		return;

	m_password = password;
	emit passwordChanged();
}

/// 校验并应用 Portal 地址：只有格式正确才会写入，并同步解析出的 host / port
bool Controller::applyPortalAddress(const QString& raw)
{
	PortalAddress::Endpoint endpoint;
	if (!PortalAddress::parse(raw, &endpoint))
		return false;

	// 统一存成 host:port，方便界面回显和配置文件阅读
	const QString normalized = endpoint.toString();
	if (normalized == m_portalAddress)
		return true;

	m_portalAddress = normalized;
	m_portalHost = endpoint.host;
	m_portalPort = endpoint.port;
	emit portalAddressChanged();
	return true;
}

/// 返回账号配置文件的完整路径
QString Controller::configPath() const
{
	// Windows 下 GenericConfigLocation 就是 %LOCALAPPDATA%
	const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
	return QDir(base).filePath(QStringLiteral("Maplekilo/PTUCampusLogin/config.json"));
}

/// 当前平台是否支持「阻止系统自动弹出浏览器」
bool Controller::supportsBrowserLaunchPolicy() const
{
	return Platform::supportsBrowserLaunchPolicy();
}

// ============================ 系统设置（异步） ============================

/// 请求修改「阻止自动弹出浏览器」：只是把活儿丢到后台，立刻返回
void Controller::setBlockAutoBrowser(bool blocked)
{
	// 上一次还没结束，或已经是目标状态，都不必再动
	if (m_blockAutoBrowserBusy || m_blockAutoBrowser == blocked)
		return;

	m_blockAutoBrowserBusy = true;
	emit blockAutoBrowserBusyChanged();

	// 开始新的一次修改，先把上次的失败原因清掉
	if (!m_blockAutoBrowserError.isEmpty())
	{
		m_blockAutoBrowserError.clear();
		emit blockAutoBrowserErrorChanged();
	}

	applyBlockAutoBrowser(blocked);
}

/// 在后台线程写策略：先按当前权限直接写，写不进去再弹 UAC 起提权小进程。
/// 提权要等用户点确认（最长 kElevationTimeoutMs），所以绝不能占着主线程
void Controller::applyBlockAutoBrowser(bool blocked)
{
	m_workPool.start(QRunnable::create([this, blocked]()
		{
			bool written = NcsiProbing::setBrowserLaunchBlocked(blocked);
			if (!written)
				written = NcsiProbing::requestElevatedChange(blocked);

			if (written)
				Platform::notifyNetworkSettingsChanged();

			// 成功失败都以注册表的实际状态为准
			const QString error = written ? QString() : NcsiProbing::lastError();
			const bool actual = NcsiProbing::isBrowserLaunchBlocked();

			QMetaObject::invokeMethod(this, [this, error, actual]()
				{
					m_blockAutoBrowser = actual;
					m_blockAutoBrowserBusy = false;
					emit blockAutoBrowserBusyChanged();

					if (error != m_blockAutoBrowserError)
					{
						m_blockAutoBrowserError = error;
						emit blockAutoBrowserErrorChanged();
					}

					// 不管成功失败都通知界面，让开关回到真实状态
					emit blockAutoBrowserChanged();
				}, Qt::QueuedConnection);
		}));
}

/// 请求修改开机自启动：同样是异步的
void Controller::setAutoStart(bool enabled)
{
	if (m_autoStartBusy || m_autoStart == enabled)
		return;

	m_autoStartBusy = true;
	emit autoStartBusyChanged();

	applyAutoStart(enabled);
}

/// 在后台线程写 / 删自启动项
void Controller::applyAutoStart(bool enabled)
{
	m_workPool.start(QRunnable::create([this, enabled]()
		{
			const QString name = QString::fromLatin1(ControllerConfig::kAutostartName);

			bool written = false;
			if (enabled)
			{
				// 系统里的状态可能被外部改动过，先按实际状态判断，避免无谓的写入
				written = ControllerConfig::isAutostartRegistered()
					|| Platform::setAutostart(name, ControllerConfig::autostartCommand(), nullptr);
			}
			else
			{
				written = Platform::removeAutostart(name, nullptr);
			}

			// 以系统里的实际状态为准：写失败时这个值仍是原来的
			const bool actual = ControllerConfig::isAutostartRegistered();

			QMetaObject::invokeMethod(this, [this, written, actual]()
				{
					m_autoStartBusy = false;
					emit autoStartBusyChanged();

					// 写失败就保持原值，界面会把开关拨回去
					if (written)
						m_autoStart = actual;

					emit autoStartChanged();
				}, Qt::QueuedConnection);
		}));
}

// ============================ 配置读写 ============================

/// 把当前账号密码写入 JSON 配置文件
bool Controller::save()
{
	const QString path = configPath();
	QDir dir = QFileInfo(path).absoluteDir();
	if (!dir.exists() && !dir.mkpath(QStringLiteral(".")))
		return false;

	QJsonObject object;
	object.insert(QStringLiteral("username"), m_username);
	// 只做 Base64 编码，避免明文可读，并不是加密
	object.insert(QStringLiteral("password"),
		QString::fromLatin1(m_password.toUtf8().toBase64()));
	object.insert(QStringLiteral("portalAddress"), m_portalAddress);
	object.insert(QStringLiteral("updatedAt"),
		QDateTime::currentDateTime().toString(Qt::ISODate));

	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly))
		return false;

	file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
	return file.commit();
}

/// 从 JSON 配置文件读取账号密码并填充到属性
void Controller::load()
{
	QFile file(configPath());
	if (!file.open(QIODevice::ReadOnly))
		return;

	const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
	if (!document.isObject())
		return;

	const QJsonObject object = document.object();
	const QString username = object.value(QStringLiteral("username")).toString();
	const QString password = QString::fromUtf8(
		QByteArray::fromBase64(
			object.value(QStringLiteral("password")).toString().toLatin1()));
	const QString portalAddress = object.value(QStringLiteral("portalAddress")).toString();

	setUsername(username);
	setPassword(password);
	// 配置里的地址可能被手改坏，校验不通过就保留默认值
	if (!portalAddress.isEmpty())
		applyPortalAddress(portalAddress);
}