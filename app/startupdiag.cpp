#include "startupdiag.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>

namespace StartupDiagState
{
	/// 启动阶段收集到的警告 / 错误
	QStringList messages;
	/// 之前安装的消息处理器，卸载时要还原回去
	QtMessageHandler previousHandler = nullptr;

	/// 拦截日志：只挑警告及以上级别的留下来，然后继续交给上一个处理器
	void captureMessages(QtMsgType type, const QMessageLogContext& context,
		const QString& message)
	{
		if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg)
			messages.append(message);

		if (previousHandler)
			previousHandler(type, context, message);
	}
}

/// 安装消息处理器，开始收集警告 / 错误
void installStartupDiagnostics()
{
	StartupDiagState::previousHandler = qInstallMessageHandler(
		StartupDiagState::captureMessages);
}

/// 还原之前的消息处理器
void uninstallStartupDiagnostics()
{
	qInstallMessageHandler(StartupDiagState::previousHandler);
}

/// 已收集到的启动消息
QStringList startupDiagnostics()
{
	return StartupDiagState::messages;
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