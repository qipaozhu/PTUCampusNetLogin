#pragma once

#include <QString>

/// 应用日志：GUI 程序没有控制台，出问题时只能靠落盘的文件排查
namespace AppLog
{
	/// 追加一行到 <配置目录>/Maplekilo/PTUCampusLogin/<fileName>，自动带时间戳和进程号
	void append(const QString& fileName, const QString& message);
}