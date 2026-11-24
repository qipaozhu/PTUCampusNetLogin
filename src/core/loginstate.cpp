#include "loginstate.h"

namespace LoginStateTable
{
	/// 每种状态的英文标识和中文文案，只写这一份
	struct Entry
	{
		LoginState::Kind kind;
		const char* name;
		const char* text;
	};

	constexpr Entry kEntries[] = {
		{ LoginState::Kind::NotLoggedIn, "notLoggedIn", "未登录" },
		{ LoginState::Kind::LoggingIn, "loggingIn", "登录中" },
		{ LoginState::Kind::LoggedIn, "loggedIn", "已登录" },
		{ LoginState::Kind::Failed, "failed", "登录失败" },
	};

	const Entry& lookup(LoginState::Kind kind)
	{
		for (const Entry& entry : kEntries)
		{
			if (entry.kind == kind)
				return entry;
		}
		return kEntries[0];
	}
}

namespace LoginState
{
	/// QML 判断用的英文标识
	QString kindName(Kind kind)
	{
		return QString::fromLatin1(LoginStateTable::lookup(kind).name);
	}

	/// 中文文案：托盘菜单和界面共用同一份
	QString text(Kind kind)
	{
		return QString::fromUtf8(LoginStateTable::lookup(kind).text);
	}
}