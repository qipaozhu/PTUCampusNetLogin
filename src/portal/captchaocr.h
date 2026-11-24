#pragma once

#include <QImage>
#include <QObject>
#include <QString>

#include <functional>

class QThread;
class CaptchaOcrEngine;

/// 验证码 OCR。
/// 编译时找到 Tesseract 才会启用，否则 available() 返回 false。
///
/// Tesseract 的初始化要读几十 MB 语言包、一次识别还要跑多种预处理变体，
/// 放在主线程会让 QML 动画和窗口直接卡住几秒。所以这里把它做成：
///   - 引擎（TessBaseAPI）常驻，只初始化一次，之后反复复用；
///   - 识别在专属工作线程里执行，调用方拿到的是异步回调。
class CaptchaOcr : public QObject
{
	Q_OBJECT
public:
	/// Tesseract 是否可用
	static bool available();

	explicit CaptchaOcr(QObject* parent = nullptr);
	~CaptchaOcr() override;

	/// 异步识别 4 位数字验证码，失败时 text 为空（或不是 4 位数字）。
	/// 同一时刻只保留最后一个请求：更早的请求即使已经算完，结果也会被丢弃。
	void recognize(const QImage& image, std::function<void(const QString&)> callback);

	/// 丢弃尚未返回的结果（不会打断正在进行的识别，只是不再回调）
	void cancel();

private:
	/// 工作线程，专门跑 OCR，避免阻塞界面线程
	QThread* m_thread = nullptr;
	/// 仅作为“工作线程上下文”存在：invokeMethod 需要一个活在该线程里的 QObject
	QObject* m_context = nullptr;
	/// 只允许在工作线程里访问，析构在 stop 之后，不会再被并发使用
	CaptchaOcrEngine* m_engine = nullptr;

	/// 请求代号：回调时比对，用来丢弃过期结果
	quint64 m_token = 0;
	std::function<void(const QString&)> m_callback;
};