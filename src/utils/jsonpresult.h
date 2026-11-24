#pragma once

#include <QByteArray>
#include <QString>

/// 校园网接口返回体（JSONP / JSON）的解析。
/// 协议细节只在这里出现，联调时看这一个文件就够。
namespace JsonpResult
{
	/// 解析返回体：result == "1" 视为成功，msga / msg 为错误信息
	bool parseBody(const QByteArray& body, QString* message = nullptr);

	/// 先看传输和状态码，通过了才去解析返回体
	bool parseResponse(bool transportOk, int httpStatus, const QByteArray& body,
		QString* message = nullptr);
}