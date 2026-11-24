#pragma once

#include <QImage>

/// 验证码图像预处理。
/// 纯 Qt 实现、不依赖 Tesseract，所以从 OCR 引擎里拆出来：
/// 可以单独编译、单独用固定样本图做回归测试。
namespace CaptchaImage
{
	/// 识别前统一放大几倍：Tesseract 对十几像素高的字识别率很低
	constexpr int kUpscale = 5;
	/// 四周留白：字形紧贴图片边缘时识别率会明显变差
	constexpr int kBorder = 12;

	/// 转灰度
	QImage toGray(const QImage& source);

	/// 3x3 中值滤波：抹掉 1 像素大小的彩色噪点，数字笔画基本不受影响
	QImage medianFilter(const QImage& gray);

	/// 大津法求二值化阈值
	int otsuThreshold(const QImage& gray);

	/// 按阈值二值化，并统一成 "0 = 墨迹 / 255 = 背景"
	QImage binarize(const QImage& gray, int threshold);

	/// 3x3 膨胀：笔画变粗，断掉的笔画也能接回去，更利于 Tesseract 分割
	QImage dilate3x3(const QImage& binary);

	/// 连通域过滤：只留下"像数字"的连通块（背景细线、小噪点会被剔掉）
	QImage keepDigitComponents(const QImage& binary);

	/// 放大 + 四周留白，得到干净的黑字白底图
	QImage upscaleAndPad(const QImage& binary, int scale, int border);
}