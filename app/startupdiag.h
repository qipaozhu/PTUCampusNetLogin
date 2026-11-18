#pragma once

#include <QString>
#include <QStringList>

/// 启动阶段诊断：程序打包到别的目录后如果 QML 加载失败，exe 是 WIN32 无控制台窗口的，
/// 表现为“双击后任务管理器一闪而过、没有任何提示”。这里把启动阶段的警告收集起来，
/// 失败时由入口写日志并弹窗，直接告诉用户缺了什么。

/// 安装消息处理器，开始收集警告 / 错误
void installStartupDiagnostics();

/// 还原之前的消息处理器（正常启动后立刻调用，避免一直劫持日志）
void uninstallStartupDiagnostics();

/// 已收集到的启动消息；没有则返回空列表
QStringList startupDiagnostics();

/// 把失败详情写到 exe 同目录（不可写时退回临时目录），返回最终路径
QString writeStartupLog(const QString& details);
