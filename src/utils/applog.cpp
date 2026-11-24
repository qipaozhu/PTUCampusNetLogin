#include "applog.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTextStream>

namespace AppLog
{
	/// 追加一行到配置目录下的指定文件
	void append(const QString& fileName, const QString& message)
	{
		const QString dir = QStandardPaths::writableLocation(
			QStandardPaths::GenericConfigLocation) + QStringLiteral("/Maplekilo/PTUCampusLogin");
		QDir().mkpath(dir);

		QFile file(dir + QLatin1Char('/') + fileName);
		if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
			return;

		QTextStream stream(&file);
		stream << QDateTime::currentDateTime().toString(Qt::ISODate)
			<< " [" << QCoreApplication::applicationPid() << "] "
			<< message << '\n';
	}
}