#include "lanprobe.h"

#include "platform/platform.h"

#include <QHostAddress>
#include <QNetworkInterface>

namespace LanProbe
{
	/// 探测一次默认路由所在网卡的状态
	Result probe()
	{
		Result result;

		const quint32 index = Platform::defaultRouteInterfaceIndex();
		if (index == 0)
			return result;

		const QNetworkInterface iface = QNetworkInterface::interfaceFromIndex(
			static_cast<int>(index));
		const auto flags = iface.flags();

		if (!iface.isValid()
			|| !(flags & QNetworkInterface::IsUp)
			|| !(flags & QNetworkInterface::IsRunning)
			|| (flags & QNetworkInterface::IsLoopBack))
		{
			return result;
		}

		// 还要有可用的非回环、非链路本地地址，才算真正接入网络
		for (const QNetworkAddressEntry& entry : iface.addressEntries())
		{
			const QHostAddress ip = entry.ip();
			if (ip.isLoopback() || ip.isLinkLocal())
				continue;

			result.connected = true;
			result.description = iface.humanReadableName();
			break;
		}

		return result;
	}
}