#include "captchaocr.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QStringList>
#include <QtGlobal>

#include <vector>

#ifdef HAVE_TESSERACT
#include <tesseract/baseapi.h>
#endif

namespace
{
#ifdef HAVE_TESSERACT
	/// 识别前统一放大几倍，Tesseract 对十几像素高的字识别率很低
	const int kUpscale = 5;
	/// 四周留白：字形紧贴图片边缘时识别率会明显变差
	const int kBorder = 12;

	/// 依次在可执行文件目录、环境变量里查找 tessdata
	QString findTessdataDir()
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

	/// 转灰度
	QImage toGray(const QImage& source)
	{
		return source.convertToFormat(QImage::Format_Grayscale8);
	}

	/// 3x3 中值滤波：抹掉 1 像素大小的彩色噪点，数字的笔画基本上不受影响
	QImage medianFilter(const QImage& gray)
	{
		const int width = gray.width();
		const int height = gray.height();
		QImage out(gray.size(), QImage::Format_Grayscale8);

		for (int y = 0; y < height; ++y)
		{
			uchar* dst = out.scanLine(y);
			for (int x = 0; x < width; ++x)
			{
				uchar window[9];
				int count = 0;
				for (int dy = -1; dy <= 1; ++dy)
				{
					const uchar* row = gray.constScanLine(qBound(0, y + dy, height - 1));
					for (int dx = -1; dx <= 1; ++dx)
						window[count++] = row[qBound(0, x + dx, width - 1)];
				}

				// 9 个值的中位数，插入排序足够快
				for (int i = 1; i < 9; ++i)
				{
					const uchar key = window[i];
					int j = i - 1;
					while (j >= 0 && window[j] > key)
					{
						window[j + 1] = window[j];
						--j;
					}
					window[j + 1] = key;
				}
				dst[x] = window[4];
			}
		}
		return out;
	}

	/// 大津法求二值化阈值
	int otsuThreshold(const QImage& gray)
	{
		const int width = gray.width();
		const int height = gray.height();

		int histogram[256] = {};
		for (int y = 0; y < height; ++y)
		{
			const uchar* line = gray.constScanLine(y);
			for (int x = 0; x < width; ++x)
				++histogram[line[x]];
		}

		const int total = width * height;
		double sum = 0.0;
		for (int i = 0; i < 256; ++i)
			sum += i * histogram[i];

		double sumB = 0.0;
		int weightB = 0;
		double maxVariance = 0.0;
		int threshold = 128;
		for (int t = 0; t < 256; ++t)
		{
			weightB += histogram[t];
			if (weightB == 0)
				continue;
			const int weightF = total - weightB;
			if (weightF == 0)
				break;

			sumB += t * histogram[t];
			const double meanB = sumB / weightB;
			const double meanF = (sum - sumB) / weightF;
			const double variance = weightB * weightF * (meanB - meanF) * (meanB - meanF);
			if (variance > maxVariance)
			{
				maxVariance = variance;
				threshold = t;
			}
		}
		return threshold;
	}

	/// 按阈值二值化，并统一成 "0 = 墨迹 / 255 = 背景"
	QImage binarize(const QImage& gray, int threshold)
	{
		const int width = gray.width();
		const int height = gray.height();
		QImage out(gray.size(), QImage::Format_Grayscale8);

		long inkPixels = 0;
		for (int y = 0; y < height; ++y)
		{
			const uchar* src = gray.constScanLine(y);
			uchar* dst = out.scanLine(y);
			for (int x = 0; x < width; ++x)
			{
				const bool dark = src[x] <= threshold;
				dst[x] = dark ? 0 : 255;
				if (dark)
					++inkPixels;
			}
		}

		// 墨迹比背景还多说明是深底浅字，整幅图反相
		if (inkPixels * 2 > static_cast<long>(width) * height)
		{
			for (int y = 0; y < height; ++y)
			{
				uchar* line = out.scanLine(y);
				for (int x = 0; x < width; ++x)
					line[x] = static_cast<uchar>(255 - line[x]);
			}
		}
		return out;
	}

	/// 3x3 膨胀：笔画变粗，断掉的笔画也能接回去，更利于 Tesseract 分割
	QImage dilate3x3(const QImage& binary)
	{
		const int width = binary.width();
		const int height = binary.height();
		QImage out = binary.copy();

		for (int y = 0; y < height; ++y)
		{
			const uchar* src = binary.constScanLine(y);
			for (int x = 0; x < width; ++x)
			{
				// 只从原图里的墨迹向外扩散，避免整幅图一起变黑
				if (src[x] != 0)
					continue;

				for (int dy = -1; dy <= 1; ++dy)
				{
					const int yy = y + dy;
					if (yy < 0 || yy >= height)
						continue;
					uchar* line = out.scanLine(yy);
					for (int dx = -1; dx <= 1; ++dx)
					{
						const int xx = x + dx;
						if (xx < 0 || xx >= width)
							continue;
						line[xx] = 0;
					}
				}
			}
		}
		return out;
	}

	/// 连通域过滤：只留下"像数字"的连通块。
	/// 背景里的细长横线、对角线和小噪点的共同特点是又矮又稀，按
	/// 高度 / 面积 / 填充率三个条件就能把绝大部分剔掉
	QImage keepDigitComponents(const QImage& binary)
	{
		const int width = binary.width();
		const int height = binary.height();
		if (width <= 0 || height <= 0)
			return binary;

		const int total = width * height;
		// 数字差不多占满整张图的高度，细线和小点远达不到
		const int minHeight = qMax(4, height * 2 / 5);
		const int minArea = qMax(10, total / 150);

		std::vector<int> labels(static_cast<size_t>(total), 0);
		std::vector<char> keep;				// keep[label - 1] 表示该连通块要不要保留
		std::vector<int> stack;
		std::vector<int> members;

		int nextLabel = 0;
		for (int start = 0; start < total; ++start)
		{
			if (labels[start] != 0 || binary.constScanLine(start / width)[start % width] != 0)
				continue;

			// 8 邻域洪泛填充
			++nextLabel;
			members.clear();
			stack.assign(1, start);
			labels[start] = nextLabel;

			while (!stack.empty())
			{
				const int index = stack.back();
				stack.pop_back();
				members.push_back(index);

				const int x = index % width;
				const int y = index / width;
				for (int dy = -1; dy <= 1; ++dy)
				{
					const int yy = y + dy;
					if (yy < 0 || yy >= height)
						continue;
					const uchar* row = binary.constScanLine(yy);
					for (int dx = -1; dx <= 1; ++dx)
					{
						const int xx = x + dx;
						if (xx < 0 || xx >= width)
							continue;

						const int next = yy * width + xx;
						if (labels[next] != 0 || row[xx] != 0)
							continue;
						labels[next] = nextLabel;
						stack.push_back(next);
					}
				}
			}

			// 统计包围盒，用"填充率"区分实心笔画和细线
			int minX = width;
			int maxX = -1;
			int minY = height;
			int maxY = -1;
			for (const int index : members)
			{
				const int x = index % width;
				const int y = index / width;
				minX = qMin(minX, x);
				maxX = qMax(maxX, x);
				minY = qMin(minY, y);
				maxY = qMax(maxY, y);
			}

			const int boxWidth = maxX - minX + 1;
			const int boxHeight = maxY - minY + 1;
			const int area = static_cast<int>(members.size());
			const double fillRatio = static_cast<double>(area) / (boxWidth * boxHeight);

			const bool likeDigit = boxHeight >= minHeight
				&& boxWidth >= 2
				&& area >= minArea
				&& fillRatio >= 0.18;
			keep.push_back(likeDigit ? 1 : 0);
		}

		QImage out(binary.size(), QImage::Format_Grayscale8);
		out.fill(255);
		for (int y = 0; y < height; ++y)
		{
			uchar* dst = out.scanLine(y);
			for (int x = 0; x < width; ++x)
			{
				const int index = y * width + x;
				const int label = labels[index];
				if (label > 0 && keep[label - 1] != 0)
					dst[x] = 0;
			}
		}
		return out;
	}

	/// 放大 + 四周留白，再二值化一次，得到干净的黑字白底图
	QImage upscaleAndPad(const QImage& binary, int scale, int border)
	{
		const QImage scaled = binary.scaled(binary.width() * scale, binary.height() * scale,
			Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

		QImage canvas(scaled.width() + border * 2, scaled.height() + border * 2,
			QImage::Format_Grayscale8);
		canvas.fill(255);

		for (int y = 0; y < scaled.height(); ++y)
		{
			const uchar* src = scaled.constScanLine(y);
			uchar* dst = canvas.scanLine(y + border);
			for (int x = 0; x < scaled.width(); ++x)
				dst[x + border] = src[x] >= 128 ? 255 : 0;
		}
		return canvas;
	}

	/// 跑一次 Tesseract，返回识别出的数字和平均置信度
	QString runOcr(tesseract::TessBaseAPI& api, const QImage& image, int* confidence)
	{
		// 换图之前先清掉上一次的识别结果，否则可能串用上一张图的统计信息
		api.Clear();
		api.SetImage(image.constBits(), image.width(), image.height(), 1,
			static_cast<int>(image.bytesPerLine()));

		char* raw = api.GetUTF8Text();
		*confidence = api.MeanTextConf();
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

	/// 设置 PTU_CAPTCHA_DUMP=1 时把参与识别的图存到临时目录，方便离线调参
	bool debugDumpEnabled()
	{
		const QString value = qEnvironmentVariable("PTU_CAPTCHA_DUMP");
		return value == QLatin1String("1") || value.compare(QLatin1String("true"),
			Qt::CaseInsensitive) == 0;
	}
#endif
}

/// 返回 Tesseract 是否在编译时启用
bool CaptchaOcr::available()
{
#ifdef HAVE_TESSERACT
	return true;
#else
	return false;
#endif
}

/// 识别验证码并过滤出数字。
/// 思路：中值滤波去噪点 → 多个阈值二值化 → 连通域过滤掉背景线和小点 →
/// 放大留白 → Tesseract 识别。多个变体一起投票，选最可信的结果
QString CaptchaOcr::recognize(const QImage& image)
{
#ifdef HAVE_TESSERACT
	if (image.isNull())
		return {};

	const QImage gray = toGray(image);
	if (gray.isNull())
		return {};

	const QImage smoothed = medianFilter(gray);

	const int otsuGray = otsuThreshold(gray);
	const int otsuSmoothed = otsuThreshold(smoothed);

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

	tesseract::TessBaseAPI api;
	const QString tessdataDir = findTessdataDir();
	const QByteArray tessdata = tessdataDir.toUtf8();
	if (api.Init(tessdata.isEmpty() ? nullptr : tessdata.constData(), "eng") != 0)
		return {};

	api.SetPageSegMode(tesseract::PSM_SINGLE_LINE);
	api.SetVariable("tessedit_char_whitelist", "0123456789");
	api.SetVariable("classify_bln_numeric_mode", "1");
	// 验证码不可能是词典里的词，关掉词典能减少误纠
	api.SetVariable("load_system_dawg", "0");
	api.SetVariable("load_freq_dawg", "0");
	api.SetVariable("user_defined_dpi", "300");

	const bool dump = debugDumpEnabled();
	const QString dumpDir = QDir::tempPath() + QStringLiteral("/ptu_captcha");

	QStringList fourDigits;		// 所有 4 位结果，用来投票
	QString bestFour;
	int bestFourConfidence = -1;
	QString bestAny;

	for (int i = 0; i < static_cast<int>(attempts.size()); ++i)
	{
		const Attempt& attempt = attempts[static_cast<size_t>(i)];

		QImage binary = binarize(attempt.image, attempt.threshold);
		if (attempt.filterComponents)
			binary = keepDigitComponents(dilate3x3(binary));

		const QImage prepared = upscaleAndPad(binary, kUpscale, kBorder);
		if (dump)
		{
			QDir().mkpath(dumpDir);
			prepared.save(QStringLiteral("%1/variant_%2.png").arg(dumpDir).arg(i));
		}

		int confidence = -1;
		const QString text = runOcr(api, prepared, &confidence);
		if (text.isEmpty())
			continue;

		qDebug() << "验证码识别变体" << i << "阈值" << attempt.threshold
			<< "过滤" << attempt.filterComponents
			<< "结果" << text << "置信度" << confidence;

		if (text.size() == 4)
		{
			fourDigits << text;
			if (confidence > bestFourConfidence)
			{
				bestFourConfidence = confidence;
				bestFour = text;
			}
		}
		else if (bestAny.isEmpty())
		{
			bestAny = text;
		}
	}

	api.End();

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
#else
	Q_UNUSED(image)
		return {};
#endif
}
