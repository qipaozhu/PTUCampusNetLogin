#pragma once

#include <QString>
#include <QtGlobal>

/// Windows 注册表的最小封装。
///
/// 头文件里不出现 windows.h，调用方不需要为了读写注册表引入系统头文件；
/// 非 Windows 平台上所有函数都返回失败，上层逻辑自然降级（读不到就用默认值）。
/// 统一走 64 位注册表视图（KEY_WOW64_64KEY），避免被重定向到 Wow6432Node。
namespace WinRegistry
{
	/// 根键用字符串表示，避免在头文件里暴露 HKEY
	inline constexpr const char* kLocalMachine = "HKEY_LOCAL_MACHINE";
	inline constexpr const char* kCurrentUser = "HKEY_CURRENT_USER";

	/// 读 DWORD，值不存在或类型不对返回 false
	bool readDword(const QString& root, const QString& subKey,
		const QString& valueName, quint32* value);
	/// 写 DWORD
	bool writeDword(const QString& root, const QString& subKey,
		const QString& valueName, quint32 value);

	/// 读字符串，读不到返回空串
	QString readString(const QString& root, const QString& subKey,
		const QString& valueName);
	/// 写字符串
	bool writeString(const QString& root, const QString& subKey,
		const QString& valueName, const QString& value);

	/// 删除一个值；值或键本来就不存在也算成功
	bool removeValue(const QString& root, const QString& subKey,
		const QString& valueName);
}