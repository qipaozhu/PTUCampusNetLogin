#include "textutils.h"

namespace TextUtils
{
	/// 验证码必须是 4 位纯数字
	bool isValidCaptchaText(const QString& text)
	{
		if (text.size() != 4)
			return false;

		for (const QChar ch : text)
		{
			if (!ch.isDigit())
				return false;
		}
		return true;
	}

	/// 把可能很长的原因压短，超长部分用省略号收尾
	QString abbreviate(const QString& text, int maxChars)
	{
		const QString trimmed = text.trimmed();
		if (trimmed.size() <= maxChars)
			return trimmed;
		return trimmed.left(maxChars) + QStringLiteral("…");
	}
}