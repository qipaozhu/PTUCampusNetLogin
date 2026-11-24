#pragma once

#include <QString>

/// 纯文本工具：跨平台、无副作用，方便单测
namespace TextUtils
{
	/// 验证码必须是 4 位纯数字
	bool isValidCaptchaText(const QString& text);

	/// 把可能很长的原因压短，超长部分用省略号收尾
	QString abbreviate(const QString& text, int maxChars = 36);
}