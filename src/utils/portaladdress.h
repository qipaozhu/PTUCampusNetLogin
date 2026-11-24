#pragma once

#include <QString>

/// 校园网 Portal 地址的解析与规范化。
/// 和界面、网络请求都无关，所以单独放这里，方便单测。
namespace PortalAddress
{
	/// Portal 接口地址：主机只接受合法 IPv4 或域名，端口缺省 801
	struct Endpoint
	{
		QString host;
		int port = 0;

		/// 规范化成 "host:port"，方便界面回显和写配置文件
		QString toString() const;
	};

	/// 缺省端口（eportal 接口用；登录接口仍然走 80）
	constexpr int kDefaultPort = 801;

	/// 解析并校验 "host[:port]"（允许带协议头、路径、查询串）。
	/// 格式不合法时返回 false，且不修改 endpoint
	bool parse(const QString& raw, Endpoint* endpoint);
}