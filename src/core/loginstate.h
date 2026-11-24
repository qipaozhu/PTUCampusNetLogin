#pragma once

#include <QString>

/// 登录状态的粗粒度视图。
///
/// 精细状态（检查网络 / 取验证码 / 识别 / 校验 / 登录中……）只在 PortalLogin 内部用，
/// 界面和托盘真正需要的只有这四种，所以单独定义一份，避免每个使用方各自映射一套。
namespace LoginState
{
	enum class Kind
	{
		NotLoggedIn,
		LoggingIn,
		LoggedIn,
		Failed
	};

	/// QML 判断用的英文标识，避免在 QML 里写魔法数字
	QString kindName(Kind kind);

	/// 中文文案：托盘菜单和界面共用同一份
	QString text(Kind kind);
}