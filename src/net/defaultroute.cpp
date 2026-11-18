#include "defaultroute.h"

// Win32 头文件只出现在这个文件里：winsock2.h 必须排在可能引入 windows.h 的头文件之前
#include <winsock2.h>
#include <iphlpapi.h>

#include <QByteArray>

/// 查询 IPv4 路由表，挑出默认路由所在的网卡索引
quint32 defaultRouteInterfaceIndex()
{
	// 第一次调用只用来问需要多大的缓冲区（返回 ERROR_INSUFFICIENT_BUFFER）
	DWORD size = 0;
	if (::GetIpForwardTable(nullptr, &size, FALSE) != ERROR_INSUFFICIENT_BUFFER || size == 0)
		return 0;

	QByteArray buffer(static_cast<int>(size), Qt::Uninitialized);
	auto* table = reinterpret_cast<MIB_IPFORWARDTABLE*>(buffer.data());

	if (::GetIpForwardTable(table, &size, FALSE) != NO_ERROR)
		return 0;

	quint32 bestIndex = 0;
	DWORD bestMetric = MAXDWORD;

	for (DWORD i = 0; i < table->dwNumEntries; ++i)
	{
		const MIB_IPFORWARDROW& row = table->table[i];

		// 只要目的地址和掩码都为 0 的默认路由
		if (row.dwForwardDest != 0 || row.dwForwardMask != 0)
			continue;
		if (row.dwForwardType == MIB_IPROUTE_TYPE_INVALID)
			continue;

		// 正常情况下只会有一条默认路由；多条时取度量最小的那条
		if (row.dwForwardMetric1 < bestMetric)
		{
			bestMetric = row.dwForwardMetric1;
			bestIndex = row.dwForwardIfIndex;
		}
	}

	return bestIndex;
}
