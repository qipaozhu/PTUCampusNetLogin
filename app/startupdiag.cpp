#include "startupdiag.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>

namespace
{
	QStringList g_startupMessages;
	QtMessageHandler g_previousMessageHandler = nullptr;

	void captureMessages(QtMsgType type, const QMessageLogContext& context, const QString& message)
	{
		if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg)
			g_startupMessages.append(message);

		if (g_previousMessageHandler)
			g_previousMessageHandler(type, context, message);
	}
}

/// 安装消息处理器，开始收集警告 / 错误
void installStartupDiagnostics()
{
	g_previousMessageHandler = qInstallMessageHandler(captureMessages);
}

/// 还原之前的消息处理器
void uninstallStartupDiagnostics()
{
	qInstallMessageHandler(g_previousMessageHandler);
}

/// 已收集到的启动消息
QStringList startupDiagnostics()
{
	return g_startupMessages;
}

/// 把失败详情写到 exe 同目录（不可写时退回临时目录），返回最终路径
QString writeStartupLog(const QString& details)
{
	const QStringList candidates = {
		QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("startup-error.log")),
		QDir(QDir::tempPath()).filePath(QStringLiteral("PTUCampusLogin-startup-error.log"))
	};

	for (const QString& path : candidates)
	{
		QFile file(path);
		if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
			continue;

		file.write(details.toUtf8());
		file.close();
		return path;
	}

	return QString();
}
