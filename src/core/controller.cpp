#include "controller.h"

#include <QAbstractSocket>
#include <QByteArray>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QString>
#include <QUrl>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <QSettings>
#endif

#include "net/ncsiprobing.h"

namespace
{
	// 开机拉起时带上这个参数，程序据此自动执行一次登录
	const char* const kAutoStartArg = "--autostart";

#ifdef Q_OS_WIN
	/// 通过进程令牌判断当前进程是否已经提权
	bool isElevated()
	{
		HANDLE token = nullptr;
		if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
			return false;

		TOKEN_ELEVATION elevation = {};
		DWORD size = static_cast<DWORD>(sizeof(elevation));
		const BOOL ok = GetTokenInformation(token, TokenElevation, &elevation,
			sizeof(elevation), &size);
		CloseHandle(token);
		return ok && elevation.TokenIsElevated != 0;
	}

	// 开机自启动写在这里，HKCU 下的 Run 不需要管理员权限
	const char* const kRunKey =
		"HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
	const char* const kRunValue = "PTUCampusLogin";

	/// 注册表里的自启动项是否指向当前 exe（程序被移动过就视为未开启）
	bool isAutoStartRegistered()
	{
		QSettings settings(QString::fromLatin1(kRunKey), QSettings::NativeFormat);
		const QString command = settings.value(QString::fromLatin1(kRunValue)).toString();
		if (command.isEmpty())
			return false;

		const QString exePath = QDir::toNativeSeparators(
			QCoreApplication::applicationFilePath());
		return command.contains(exePath, Qt::CaseInsensitive)
			&& command.contains(QString::fromLatin1(kAutoStartArg));
	}
#endif

	// Portal 地址的默认端口（eportal 接口；登录接口仍然走 80 端口）
	const int kDefaultPortalPort = 801;

	/// 解析并校验 "host[:port]"（允许带协议头、路径），成功时输出主机和端口。
	/// 主机只接受合法的 IPv4 地址或域名，端口缺省为 801。
	bool parsePortalAddress(const QString& raw, QString* host, int* port)
	{
		QString address = raw.trimmed();
		if (address.isEmpty())
			return false;

		// 去掉协议头，只保留 host[:port]
		if (address.startsWith(QLatin1String("http://"), Qt::CaseInsensitive))
			address.remove(0, 7);
		else if (address.startsWith(QLatin1String("https://"), Qt::CaseInsensitive))
			address.remove(0, 8);

		// 去掉路径、查询串和锚点
		const int cut = address.indexOf(QLatin1Char('/'));
		const int query = address.indexOf(QLatin1Char('?'));
		const int fragment = address.indexOf(QLatin1Char('#'));
		int end = address.size();
		if (cut >= 0)
			end = qMin(end, cut);
		if (query >= 0)
			end = qMin(end, query);
		if (fragment >= 0)
			end = qMin(end, fragment);
		address.truncate(end);

		if (address.isEmpty() || address.endsWith(QLatin1Char(':')))
			return false;

		// 借 QUrl 拆分主机和端口，顺便校验端口范围
		const QUrl url(QStringLiteral("http://") + address);
		if (!url.isValid() || url.host().isEmpty())
			return false;

		const QString parsedHost = url.host();
		const int parsedPort = url.port(kDefaultPortalPort);
		if (parsedPort <= 0 || parsedPort > 65535)
			return false;

		// 纯数字 / 点号的主机必须是一个合法的 IPv4，其它按域名规则校验
		static const QRegularExpression numericHost(
			QStringLiteral("^[0-9.]+$"));
		static const QRegularExpression domainName(QStringLiteral(
			"^(?=.{1,253}$)([A-Za-z0-9]([A-Za-z0-9-]{0,61}[A-Za-z0-9])?)"
			"(\\.[A-Za-z0-9]([A-Za-z0-9-]{0,61}[A-Za-z0-9])?)*$"));

		if (numericHost.match(parsedHost).hasMatch())
		{
			QHostAddress ip;
			if (!ip.setAddress(parsedHost)
				|| ip.protocol() != QAbstractSocket::IPv4Protocol)
			{
				return false;
			}
		}
		else if (!domainName.match(parsedHost).hasMatch())
		{
			return false;
		}

		if (host)
			*host = parsedHost;
		if (port)
			*port = parsedPort;
		return true;
	}
}

/// 构造函数：检测管理员权限，读取开机自启动状态，并加载本地已保存的账号密码
Controller::Controller(QObject* parent)
	: QObject(parent)
{
#ifdef Q_OS_WIN
	m_isAdmin = isElevated();
#endif

	// 由开机自启动拉起时命令行带 --autostart，界面据此自动登录
	m_startedAtBoot = QCoreApplication::arguments().contains(
		QString::fromLatin1(kAutoStartArg));

#ifdef Q_OS_WIN
	m_autoStart = isAutoStartRegistered();
#endif

	load();
}

/// 析构函数
Controller::~Controller()
{}

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
	QString host;
	int port = 0;
	if (!parsePortalAddress(raw, &host, &port))
		return false;

	// 统一存成 host:port，方便界面回显和配置文件阅读
	const QString normalized = QStringLiteral("%1:%2").arg(host).arg(port);
	if (normalized == m_portalAddress)
		return true;

	m_portalAddress = normalized;
	m_portalHost = host;
	m_portalPort = port;
	emit portalAddressChanged();
	return true;
}

/// 更新校园网登录状态（未登录 / 登录中 / 成功 / 失败）
void Controller::setLoginState(int state)
{
	if (state == m_loginState)
		return;

	m_loginState = state;
	emit loginStateChanged();
}

/// 返回账号配置文件的完整路径
QString Controller::configPath() const
{
	// Windows 下 GenericConfigLocation 就是 %LOCALAPPDATA%
	const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
	return QDir(base).filePath(QStringLiteral("Maplekilo/PTUCampusLogin/config.json"));
}

/// 读取注册表，判断当前是否已禁止系统自动弹出浏览器。
/// 具体读写都在 NcsiProbing 里，这里只做属性转发
bool Controller::blockAutoBrowser() const
{
	return NcsiProbing::isBrowserLaunchBlocked();
}

/// 修改注册表：禁止 / 恢复 NCSI 自动拉起浏览器打开认证页。
/// 目标是 HKLM 下的组策略键（只关"拉浏览器"，不动主动探测）：
/// 本进程有管理员权限时直接写，否则弹 UAC 起一个提权小进程去写，写完再读注册表确认
void Controller::setBlockAutoBrowser(bool blocked)
{
	// 已经是目标状态，不必重复写注册表
	if (NcsiProbing::isBrowserLaunchBlocked() == blocked)
	{
		if (!m_blockAutoBrowserError.isEmpty())
		{
			m_blockAutoBrowserError.clear();
			emit blockAutoBrowserErrorChanged();
		}
		return;
	}

	// 先按当前权限直接写（程序本身是管理员时就不必再弹 UAC）
	bool written = NcsiProbing::setBrowserLaunchBlocked(blocked);

	// 没权限才去提权；用户取消 UAC 会直接返回 false
	if (!written)
		written = NcsiProbing::requestElevatedChange(blocked);

	if (written)
		NcsiProbing::notifySettingsChanged();

	// 失败原因原样传给界面，成功时清空
	const QString error = written ? QString() : NcsiProbing::lastError();
	if (error != m_blockAutoBrowserError)
	{
		m_blockAutoBrowserError = error;
		emit blockAutoBrowserErrorChanged();
	}

	// 不管成功失败都通知界面，让开关回到注册表里的真实状态
	emit blockAutoBrowserChanged();
}

/// 开启 / 关闭开机自启动：在 HKCU 的 Run 项里写入带 --autostart 的完整路径
void Controller::setAutoStart(bool enabled)
{
#ifdef Q_OS_WIN
	// 注册表可能被外部改动过，先按实际状态判断，避免无谓的写入
	if (enabled && isAutoStartRegistered())
	{
		if (!m_autoStart)
		{
			m_autoStart = true;
			emit autoStartChanged();
		}
		return;
	}

	QSettings settings(QString::fromLatin1(kRunKey), QSettings::NativeFormat);
	if (enabled)
	{
		// 路径带空格必须加引号，否则系统会把路径拆成多个参数
		const QString command = QStringLiteral("\"%1\" %2")
			.arg(QDir::toNativeSeparators(QCoreApplication::applicationFilePath()),
				QString::fromLatin1(kAutoStartArg));
		settings.setValue(QString::fromLatin1(kRunValue), command);
	}
	else
	{
		// 关掉时直接清掉，顺便处理程序换过位置的旧值
		settings.remove(QString::fromLatin1(kRunValue));
	}
	settings.sync();

	// 写失败时保持原状态，通知界面把开关弹回去
	if (settings.status() != QSettings::NoError)
	{
		emit autoStartChanged();
		return;
	}

	m_autoStart = enabled;
	emit autoStartChanged();
#else
	Q_UNUSED(enabled)
#endif
}

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
