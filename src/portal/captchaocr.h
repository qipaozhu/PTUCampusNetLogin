#pragma once

#include <QImage>
#include <QString>

/// 验证码 OCR。
/// 编译时找到 Tesseract 才会启用，否则 available() 返回 false。
class CaptchaOcr
{
public:
	/// Tesseract 是否可用
	static bool available();

	/// 识别 4 位数字验证码，失败返回空字符串
	static QString recognize(const QImage& image);
};
