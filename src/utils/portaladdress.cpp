#include "portaladdress.h"

#include <QAbstractSocket>
#include <QHostAddress>
#include <QRegularExpression>
#include <QUrl>

namespace PortalAddress
{
	/// 规范化成 "host:port"
	QString Endpoint::toString() const
	{
		return QStringLiteral("%1:%2").arg(host).arg(port);
	}

	/// 解析并校验 "host[:port]"，成功时写入 endpoint
	bool parse(const QString& raw, Endpoint* endpoint)
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
		const int parsedPort = url.port(kDefaultPort);
		if (parsedPort <= 0 || parsedPort > 65535)
			return false;

		// 纯数字 / 点号的主机必须是合法 IPv4，其它按域名规则校验
		static const QRegularExpression numericHost(QStringLiteral("^[0-9.]+$"));
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

		if (endpoint)
		{
			endpoint->host = parsedHost;
			endpoint->port = parsedPort;
		}
		return true;
	}
}