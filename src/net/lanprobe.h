#pragma once

#include <QString>

/// 本地网卡探测：默认路由所在的那块网卡是否真的接入了网络（WLAN / 有线）。
///
/// 里面是阻塞的系统调用（Windows 上是 GetIpForwardTable + 网卡枚举），
/// 所以只允许在工作线程里调用，绝不能在主线程直接跑。
namespace LanProbe
{
	struct Result
	{
		/// 有可用的非回环、非链路本地地址才算真正接入网络
		bool connected = false;
		/// 网卡的可读名称，界面直接显示
		QString description;
	};

	/// 探测一次；可以在非主线程调用，不碰任何全局状态
	Result probe();
}