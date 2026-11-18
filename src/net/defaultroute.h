#pragma once

#include <QtGlobal>

/// 默认路由（0.0.0.0/0）所在网卡的索引；没有默认路由时返回 0。
///
/// Windows 专属实现，Win32 的头文件（winsock2.h / iphlpapi.h）都隔离在
/// defaultroute.cpp 里，其它文件不需要为了查路由去引入系统头文件。
quint32 defaultRouteInterfaceIndex();
