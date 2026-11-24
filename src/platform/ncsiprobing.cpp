#include "ncsiprobing.h"

#include "platform.h"
#include "utils/applog.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QString>

namespace NcsiProbingDetail
{
	/// 提权小进程的启动参数，值 1 = 禁止拉起浏览器，0 = 恢复
	constexpr const char* kHelperArg = "--apply-ncsi-policy";

	/// 提权过程记录到这个文件，GUI 程序没有控制台，出问题只能靠它排查
	constexpr const char* kLogFile = "ncsi.log";

	/// "--apply-ncsi-policy=" 前缀
	QString helperPrefix()
	{
		return QString::fromLatin1(kHelperArg) + QLatin1Char('=');
	}

	/// 提权小进程的完整参数
	QString helperArgument(bool blocked)
	{
		return helperPrefix() + QLatin1String(blocked ? "1" : "0");
	}

	/// 最近一次失败的原因，界面直接拿去显示
	QString& errorBuffer()
	{
		static QString text;
		return text;
	}

	/// 记一条日志，同时写文件和 qInfo
	void log(const QString& message)
	{
		AppLog::append(QString::fromLatin1(kLogFile), message);
	}
}

namespace NcsiProbing
{
	/// 本进程是不是被当作"提权写注册表"的小进程拉起
	bool isHelperInvocation(const QStringList& arguments)
	{
		const QString prefix = NcsiProbingDetail::helperPrefix();
		for (const QString& argument : arguments)
		{
			if (argument.startsWith(prefix))
				return true;
		}
		return false;
	}

	/// 提权小进程的入口：写注册表并返回进程退出码（0 成功 / 1 写入失败 / 2 没拿到管理员权限）
	int runHelper(const QStringList& arguments)
	{
		const QString prefix = NcsiProbingDetail::helperPrefix();
		for (const QString& argument : arguments)
		{
			if (!argument.startsWith(prefix))
				continue;

			const QString value = argument.mid(prefix.size()).trimmed();
			const bool blocked = value == QLatin1String("1");

			NcsiProbingDetail::log(QStringLiteral("提权进程启动，参数=%1").arg(value));

			const bool ok = setBrowserLaunchBlocked(blocked);
			if (ok)
				notifySettingsChanged();

			// 写不进去时区分一下：是不是压根没拿到管理员权限
			if (!ok && !Platform::isElevated())
			{
				NcsiProbingDetail::log(QStringLiteral("提权进程没有拿到管理员权限"));
				qWarning() << "提权进程没有拿到管理员权限";
				return 2;
			}

			NcsiProbingDetail::log(QStringLiteral("提权进程写入%1，结果=%2")
				.arg(blocked ? QStringLiteral("禁止拉起浏览器") : QStringLiteral("恢复拉起浏览器"),
					ok ? QStringLiteral("成功") : QStringLiteral("失败")));

			qInfo() << "提权写注册表：" << (blocked ? "禁止拉起浏览器" : "恢复拉起浏览器")
				<< (ok ? "成功" : "失败");
			return ok ? 0 : 1;
		}

		return 1;
	}

	/// 当前是否已禁止 NCSI 自动拉起浏览器
	bool isBrowserLaunchBlocked()
	{
		return Platform::isBrowserLaunchBlocked();
	}

	/// 直接写注册表，返回写入后注册表是否达到期望状态
	bool setBrowserLaunchBlocked(bool blocked)
	{
		return Platform::setBrowserLaunchBlocked(blocked);
	}

	/// 弹 UAC 起提权小进程去写，等它结束后再读注册表确认
	bool requestElevatedChange(bool blocked)
	{
		NcsiProbingDetail::errorBuffer().clear();

		const QString exePath = QDir::toNativeSeparators(
			QCoreApplication::applicationFilePath());
		const QString parameters = NcsiProbingDetail::helperArgument(blocked);

		NcsiProbingDetail::log(QStringLiteral("申请提权：blocked=%1，exe=%2")
			.arg(blocked ? QStringLiteral("1") : QStringLiteral("0"), exePath));

		int exitCode = -1;
		QString error;
		if (!Platform::runElevatedAndWait(exePath, parameters,
			Platform::kElevationTimeoutMs, &exitCode, &error))
		{
			// 用户取消 UAC、等待超时，或者系统没起进程。
			// 超时的情况也可能是已经写完了，所以先看注册表的实际状态
			const bool ok = isBrowserLaunchBlocked() == blocked;
			if (!ok)
			{
				NcsiProbingDetail::errorBuffer() = error.isEmpty()
					? QStringLiteral("提权失败，设置未修改")
					: error + QStringLiteral("，设置未修改");
			}
			NcsiProbingDetail::log(QStringLiteral("提权失败：%1")
				.arg(NcsiProbingDetail::errorBuffer()));
			return ok;
		}

		if (exitCode == 2)
		{
			NcsiProbingDetail::errorBuffer() =
				QStringLiteral("提权进程没有拿到管理员权限，设置未修改");
			return false;
		}
		if (exitCode != 0)
		{
			NcsiProbingDetail::errorBuffer() = QStringLiteral("提权进程写注册表失败（退出码 %1）")
				.arg(exitCode);
			return false;
		}

		const bool ok = isBrowserLaunchBlocked() == blocked;
		if (!ok)
		{
			NcsiProbingDetail::errorBuffer() = QStringLiteral(
				"提权进程执行成功，但注册表未生效（可能被组策略或安全软件锁定）");
		}

		NcsiProbingDetail::log(QStringLiteral("提权进程结束，退出码=%1，当前 blocked=%2")
			.arg(exitCode)
			.arg(isBrowserLaunchBlocked() ? QStringLiteral("1") : QStringLiteral("0")));

		return ok;
	}

	/// 最近一次修改失败的原因，成功时为空
	QString lastError()
	{
		return NcsiProbingDetail::errorBuffer();
	}

	/// 通知系统网络设置已经变化
	void notifySettingsChanged()
	{
		Platform::notifyNetworkSettingsChanged();
	}
}