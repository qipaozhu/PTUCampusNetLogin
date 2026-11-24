#include "captchaimage.h"

#include <QtGlobal>

#include <vector>

namespace CaptchaImage
{
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

	/// 3x3 膨胀：笔画变粗，断掉的笔画也能接回去
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

	/// 连通域过滤：背景里的细长横线、对角线和小噪点又矮又稀，
	/// 按高度 / 面积 / 填充率三个条件就能把绝大部分剔掉
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
}