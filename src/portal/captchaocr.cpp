#include "captchaocr.h"

#include "utils/captchaimage.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QStringList>
#include <QThread>
#include <QtGlobal>

#ifdef HAVE_TESSERACT
#include <tesseract/baseapi.h>

#include <vector>
#endif

#ifdef HAVE_TESSERACT
/// OCR 引擎：持有 TessBaseAPI，并且只在工作线程里被访问。
/// 语言包只加载一次（每次 recognize 都 Init/End 会白扔 0.2~1 秒），
/// 之后靠 Clear() + SetImage() 复用同一个引擎。
class CaptchaOcrEngine
{
public:
	/// 析构：把 Tesseract 引擎关掉（停止线程之后才会走到这里）
	~CaptchaOcrEngine()
	{
		if (m_ready)
		{
			m_api.End();
			m_ready = false;
		}
	}

	/// 识别 4 位数字验证码，失败返回空串
	QString recognize(const QImage& image);

private:
	/// 置信度达到这个值的 4 位结果直接采用，不再跑后面的预处理变体
	static constexpr int kAcceptConfidence = 85;

	/// 首次调用时加载语言包并设好参数，之后直接复用
	bool ensureReady();

	/// 跑一次 Tesseract，返回识别出的数字和平均置信度
	QString runOcr(const QImage& prepared, int* confidence);

	/// 依次在可执行文件目录、环境变量里查找 tessdata
	static QString findTessdataDir();

	/// PTU_CAPTCHA_DUMP=1 时把参与识别的图存到临时目录，方便离线调参
	static bool debugDumpEnabled();

	tesseract::TessBaseAPI m_api;
	bool m_ready = false;
	/// 初始化已经失败过：同一个 TessBaseAPI 不重复 Init，后续直接退化成"识别失败"
	bool m_initFailed = false;
};

/// 依次在可执行文件目录、环境变量里查找 tessdata
QString CaptchaOcrEngine::findTessdataDir()
{
	const QStringList candidates = {
		QCoreApplication::applicationDirPath() + QStringLiteral("/tessdata"),
		QCoreApplication::applicationDirPath() + QStringLiteral("/../share/tessdata"),
		qEnvironmentVariable("TESSDATA_PREFIX")
	};

	for (const QString& path : candidates)
	{
		if (!path.isEmpty()
			&& QFileInfo::exists(path + QStringLiteral("/eng.traineddata")))
		{
			return path;
		}
	}
	return {};
}

/// PTU_CAPTCHA_DUMP=1 时把参与识别的图存到临时目录
bool CaptchaOcrEngine::debugDumpEnabled()
{
	const QString value = qEnvironmentVariable("PTU_CAPTCHA_DUMP");
	return value == QLatin1String("1") || value.compare(QLatin1String("true"),
		Qt::CaseInsensitive) == 0;
}

/// 懒初始化：真正读语言包的时机放在工作线程里，不拖慢程序启动
bool CaptchaOcrEngine::ensureReady()
{
	if (m_ready)
		return true;
	if (m_initFailed)
		return false;

	const QString tessdataDir = findTessdataDir();
	const QByteArray tessdata = tessdataDir.toUtf8();
	if (m_api.Init(tessdata.isEmpty() ? nullptr : tessdata.constData(), "eng") != 0)
	{
		qWarning() << "Tesseract 初始化失败，tessdata 目录：" << tessdataDir;
		m_initFailed = true;
		return false;
	}

	m_api.SetPageSegMode(tesseract::PSM_SINGLE_LINE);
	m_api.SetVariable("tessedit_char_whitelist", "0123456789");
	m_api.SetVariable("classify_bln_numeric_mode", "1");
	// 验证码不可能是词典里的词，关掉词典能减少误纠
	m_api.SetVariable("load_system_dawg", "0");
	m_api.SetVariable("load_freq_dawg", "0");
	m_api.SetVariable("user_defined_dpi", "300");

	m_ready = true;
	return true;
}

/// 跑一次 Tesseract，只保留数字
QString CaptchaOcrEngine::runOcr(const QImage& prepared, int* confidence)
{
	// 换图之前先清掉上一次的识别结果，否则可能串用上一张图的统计信息
	m_api.Clear();
	m_api.SetImage(prepared.constBits(), prepared.width(), prepared.height(), 1,
		static_cast<int>(prepared.bytesPerLine()));

	char* raw = m_api.GetUTF8Text();
	*confidence = m_api.MeanTextConf();
	const QString text = raw ? QString::fromUtf8(raw) : QString();
	delete[] raw;

	QString digits;
	for (const QChar ch : text)
	{
		if (ch.isDigit())
			digits.append(ch);
	}
	return digits;
}

/// 思路：中值滤波去噪点 → 多个阈值二值化 → 连通域过滤掉背景线和小点 →
/// 放大留白 → Tesseract 识别。多个变体一起投票，选最可信的结果。
/// 为了让常见情况更快，一旦某个变体给出"4 位数字 + 高置信度"就直接返回，
/// 不再跑完剩下的变体（省掉大约 80% 的识别时间）。
QString CaptchaOcrEngine::recognize(const QImage& image)
{
	if (image.isNull() || !ensureReady())
		return {};

	const QImage gray = CaptchaImage::toGray(image);
	if (gray.isNull())
		return {};

	const QImage smoothed = CaptchaImage::medianFilter(gray);

	const int otsuGray = CaptchaImage::otsuThreshold(gray);
	const int otsuSmoothed = CaptchaImage::otsuThreshold(smoothed);

	struct Attempt
	{
		QImage image;
		int threshold;
		bool filterComponents;
	};

	// 按经验从优到劣排列：中值滤波 + 连通域过滤通常最干净，
	// 阈值偏严格（85%）更适合笔画与背景颜色接近的验证码。
	// 后几个不过滤 / 不过中值的变体是兜底：万一数字笔画只有 1 像素宽，
	// 中值滤波会把它抹掉，这时反而要靠原图
	const std::vector<Attempt> attempts = {
		{ smoothed, otsuSmoothed, true },
		{ smoothed, otsuSmoothed * 85 / 100, true },
		{ smoothed, qMin(255, otsuSmoothed * 115 / 100), true },
		{ gray, otsuGray, true },
		{ smoothed, otsuSmoothed, false },
		{ gray, otsuGray, false },
		{ gray, otsuGray * 85 / 100, false },
	};

	const bool dump = debugDumpEnabled();
	const QString dumpDir = QDir::tempPath() + QStringLiteral("/ptu_captcha");

	QStringList fourDigits;		// 所有 4 位结果，用来投票
	QString bestFour;
	int bestFourConfidence = -1;
	QString bestAny;

	for (int i = 0; i < static_cast<int>(attempts.size()); ++i)
	{
		const Attempt& attempt = attempts[static_cast<size_t>(i)];

		QImage binary = CaptchaImage::binarize(attempt.image, attempt.threshold);
		if (attempt.filterComponents)
			binary = CaptchaImage::keepDigitComponents(CaptchaImage::dilate3x3(binary));

		const QImage prepared = CaptchaImage::upscaleAndPad(binary,
			CaptchaImage::kUpscale, CaptchaImage::kBorder);
		if (dump)
		{
			QDir().mkpath(dumpDir);
			prepared.save(QStringLiteral("%1/variant_%2.png").arg(dumpDir).arg(i));
		}

		int confidence = -1;
		const QString text = runOcr(prepared, &confidence);
		if (text.isEmpty())
			continue;

		qDebug() << "验证码识别变体" << i << "阈值" << attempt.threshold
			<< "过滤" << attempt.filterComponents
			<< "结果" << text << "置信度" << confidence;

		if (text.size() != 4)
		{
			if (bestAny.isEmpty())
				bestAny = text;
			continue;
		}

		// 早退：高置信度的 4 位结果已经足够可信，没必要再投票
		if (confidence >= kAcceptConfidence)
			return text;

		fourDigits << text;
		if (confidence > bestFourConfidence)
		{
			bestFourConfidence = confidence;
			bestFour = text;
		}
	}

	// 多数投票：同一个 4 位结果被两个以上变体认出来，基本可以认为是对的
	qsizetype mostVotes = 0;
	QString voted;
	for (const QString& candidate : fourDigits)
	{
		const qsizetype count = fourDigits.count(candidate);
		if (count > mostVotes)
		{
			mostVotes = count;
			voted = candidate;
		}
	}
	if (mostVotes >= 2 && !voted.isEmpty())
		return voted;

	if (!bestFour.isEmpty())
		return bestFour;

	return bestAny;
}
#endif

/// 返回 Tesseract 是否在编译时启用
bool CaptchaOcr::available()
{
#ifdef HAVE_TESSERACT
	return true;
#else
	return false;
#endif
}

/// 构造：起一条专属 OCR 线程，引擎和线程上下文都准备好，但不加载语言包
CaptchaOcr::CaptchaOcr(QObject* parent)
	: QObject(parent)
{
#ifdef HAVE_TESSERACT
	m_engine = new CaptchaOcrEngine;

	m_thread = new QThread(this);
	m_thread->setObjectName(QStringLiteral("captcha-ocr"));

	// 一个普通的 QObject 就够了：它只负责把任务排到工作线程，
	// 逻辑全在 m_engine 里，不需要额外的信号槽定义
	m_context = new QObject;
	m_context->moveToThread(m_thread);

	m_thread->start();
#endif
}

/// 析构：先停线程再做清理，保证没有线程还在碰 m_engine
CaptchaOcr::~CaptchaOcr()
{
	m_callback = nullptr;

	if (m_thread)
	{
		m_thread->quit();
		m_thread->wait();
	}

	delete m_context;
	m_context = nullptr;

#ifdef HAVE_TESSERACT
	delete m_engine;
#endif
	m_engine = nullptr;
}

/// 发起一次异步识别
void CaptchaOcr::recognize(const QImage& image, std::function<void(const QString&)> callback)
{
	if (!callback)
		return;

#ifdef HAVE_TESSERACT
	if (!m_thread || !m_context || !m_engine)
	{
		callback(QString());
		return;
	}

	++m_token;
	const quint64 token = m_token;
	m_callback = std::move(callback);

	// 第一步：排队到工作线程里做重活（QImage 是隐式共享的，跨线程传递很便宜）
	QMetaObject::invokeMethod(m_context, [this, token, image]()
		{
			const QString text = m_engine->recognize(image);

			// 第二步：把结果送回到本对象所在的线程再回调调用方
			QMetaObject::invokeMethod(this, [this, token, text]()
				{
					// 期间又发了新请求（或流程被中止）：这次结果已经过期
					if (token != m_token)
						return;

					auto pending = std::move(m_callback);
					m_callback = nullptr;
					if (pending)
						pending(text);
				}, Qt::QueuedConnection);
		}, Qt::QueuedConnection);
#else
	Q_UNUSED(image)
	callback(QString());
#endif
}

/// 丢弃尚未返回的结果
void CaptchaOcr::cancel()
{
	++m_token;
	m_callback = nullptr;
}