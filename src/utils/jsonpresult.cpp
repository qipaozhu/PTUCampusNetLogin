#include "jsonpresult.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QVariant>

namespace JsonpResult
{
	/// 解析返回体：result == "1" 视为成功，msga / msg 为错误信息
	bool parseBody(const QByteArray& body, QString* message)
	{
		const QString text = QString::fromUtf8(body).trimmed();
		if (text.isEmpty())
			return false;

		// JSONP：真正的内容夹在回调函数名和括号之间，取最外层的 {...}
		const int start = text.indexOf(QLatin1Char('{'));
		const int end = text.lastIndexOf(QLatin1Char('}'));
		if (start < 0 || end <= start)
			return false;

		QJsonParseError error{};
		const QJsonDocument document = QJsonDocument::fromJson(
			text.mid(start, end - start + 1).toUtf8(), &error);
		if (error.error != QJsonParseError::NoError || !document.isObject())
			return false;

		const QJsonObject object = document.object();
		if (message)
		{
			*message = object.value(QStringLiteral("msga")).toString();
			if (message->isEmpty())
				*message = object.value(QStringLiteral("msg")).toString();
		}

		return object.value(QStringLiteral("result")).toVariant().toString()
			== QLatin1String("1");
	}

	/// 先看传输和状态码，通过了才去解析返回体
	bool parseResponse(bool transportOk, int httpStatus, const QByteArray& body,
		QString* message)
	{
		if (!transportOk || httpStatus != 200)
			return false;

		return parseBody(body, message);
	}
}