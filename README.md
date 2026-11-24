<p align="center">
      <img src="resources/app_logo.png" width="96" alt="logo" style="margin: 2px auto;" />
</p>

<h1 align="center">莆田学院校园网自动登录App</h1>

莆田学院校园网自动认证工具：开机会自动检测网络并完成 ePortal 认证，不用再手动打开浏览器、输账号、认验证码。

目前支持 Windows 10 / 11（用到了注册表、NCSI、UAC 提权等系统接口）。如需 MacOS 和 Linux、HarmonyOS 的版本可提issue，Qt支持跨平台，仅需剥离 Windows专用API。当前暂未有支持的计划

## 功能
- **自动登录**：开机自启动后由程序后台直接执行一次认证，**不弹出主界面**，只在右下角用 Toast 显示进度和结果；需要操作时从托盘打开界面，或者在主页手动点「登录」。
- **掉线自动重连**：已经认证过之后网络断开（锁屏睡眠唤醒、Wi-Fi 重连、会话过期），程序会自动在后台重新认证，失败按 30s / 1min / 2min / 5min 退避重试，登录进度和结果照常用右下角 Toast 提示。如果判断当前网络不是本校校园网（酒店、机场、别的学校），会自动打开浏览器让你手动认证，并**停止自动重试**，不会反复弹窗。
- **验证码识别**：本地用 Tesseract OCR 识别 ePortal 验证码，不需要联网识别；找不到 Tesseract 时自动退化成手动。
- **状态提示**：登录中 / 成功 / 失败用右下角 Toast 提示，带滑入滑出动画，失败和成功时可以手动关掉。
- **常驻托盘**：关闭主窗口不会退出程序，从托盘菜单可以重新打开界面或彻底退出。
- **深浅色跟随系统**：界面配色跟随 Windows 的浅色 / 深色主题。
- **阻止自动弹浏览器（可选）**：通过组策略键 `NoWebBrowserLaunch` 让 NCSI 不再自动拉起浏览器打开认证页；主动探测保持开启，系统判断网络状态的行为不受影响。这是机器级设置，切换时会弹 UAC。
- **账号本地保存**：账号、密码、Portal 地址存在本地 JSON 配置里。
- **占用极小**：无需下载Python，仅需使用安装包安装后即可使用，硬盘占用仅100MB+，内存占用很小。

## 安装、初次使用配置
在 [Releases](https://github.com/qipaozhu/PTUCampusNetLogin/releases/latest) 下载最新版本
> 使用前一定要完成配置，没完成配置容易导致无法登录
1. 首次启动后打开「设置」，填写校园网**账号**和**密码**，点 **「保存设置」**。
2. 选择 Portal 地址（移动 / 电信 / 自定义）点 **「应用」**
3. 勾选 **「开机自启动」**，让程序在开机后自动认证。
4. 推荐：勾选 **「阻止系统自动弹出浏览器」**，开机后不会打开浏览器，无感登录

## 构建
环境依赖、配置和打包步骤见 [BUILD.md](BUILD.md)
### 设置项
| 设置 | 说明 |
| --- | --- |
| 账号 / 密码 | 校园网账号（学号、工号）和密码，保存在本地配置文件 |
| Portal 地址 | 移动：`192.168.116.8:801`；电信：`192.168.111.8:801`；也可以手填抓包得到的地址 |
| 开机自启动 | 写入 `HKCU\...\Run`，登录 Windows 后后台自动认证一次，不弹主界面，只显示右下角提示 |
| 阻止系统自动弹出浏览器 | 关闭 NCSI 自动拉起浏览器的行为（`NoWebBrowserLaunch=1`）；需要管理员权限，切换时弹 UAC |

### 托盘菜单
托盘图标右键可以看当前登录状态、打开主界面、跳到设置页，或者退出程序。

## 配置文件
```
%LOCALAPPDATA%\Maplekilo\PTUCampusLogin\config.json
```

密码只做了 Base64 编码，**不是加密**，请不要在公共电脑上保存账号密码。

## 项目结构
```text
app/            入口和桌面集成：main.cpp / appshell（图标字体窗口）/ tray（托盘）/ startupdiag（启动诊断）
src/platform/   所有平台相关代码：platform（权限 / 路由 / 自启动 / 主题 / 提权）、winregistry（注册表）、
                ncsiprobing（NCSI 组策略）、powerwatcher（睡眠恢复）
src/utils/      跨平台纯工具：textutils / portaladdress / jsonpresult / captchaimage / applog
src/net/         network（网络检测）、lanprobe（默认路由所在网卡探测）
src/core/        controller（配置与系统设置）、singleinstance、loginstate（状态文案）、startupcoordinator（启动决策）
src/portal/      portalclient（Portal 协议：URL / 请求 / JSONP）、portallogin（登录状态机）、
                 autoreconnect（掉线自动重连）、captchaocr（验证码识别）
src/ui/          systemtheme（深浅色 + 配色）
qml/             main / SettingsPage / TestPage / Toast / ToastCenter（编译进 exe 的 qrc）
resources/       图标、字体、logo、resources.qrc、windows.rc
cmake/           tesseract.cmake（可选的 OCR 依赖）
CMakeLists.txt   构建脚本
BUILD.md         构建、运行、发布说明
AGENT.md         项目总览与开发约定（专题文档在 agent/ 下）
```

系统 API（注册表、UAC、NCSI、路由表）全部集中在 `src/platform/`，
其它模块只通过 `Platform::` 访问，为以后移植 Linux 留好了边界。

## 常见问题
- **双击没反应 / 一闪而过**：多半是发布目录缺 Qt 的 DLL 或 qml 模块。看 exe 同级的 `startup-error.log`，退出码含义见 [BUILD.md](BUILD.md) 第 7 节。
- **验证码识别失败**：OCR 有小概率识别错，重试一次即可，或者去浏览器手动完成认证后点「已手动登录」。
- **系统显示「无 Internet」**：未完成认证时系统本来就会这样显示；本程序只关闭「自动拉起浏览器」，不会关闭 NCSI 主动探测。
- **开机没有自动登录**：确认在设置里打开了「开机自启动」，并且程序被移动过位置后重新打开一次开关（注册表里记的是完整路径）。
- **锁屏 / 睡眠唤醒后要重新登录**：不用手动操作。程序会在检测到掉线后自动在后台重新认证（睡眠恢复时会立刻检测一次），过程中不弹浏览器，Toast 照常提示进度和结果；托盘图标和主界面里的状态也会实时更新。前提是程序还在托盘里运行，关窗口不会退出。
- **登录后仍然没有网络**：请确保你有办理校园网套餐，并使用了正确的登录地址，即选择了正确的运营商。注意移动、电信的区别，以及选择登录地址后是否点击了 **应用**

## 已知限制
- 验证码识别不是 100%，同一轮最多试 30 次，失败要求手动登录
- 学校升级门户系统后接口参数可能变化

## 许可
代码许可见 [LICENSE](LICENSE) 。关于学校图片素材归学校所有，与本项目与之无从属或授权关系
## 说明
本项目仅用于个人学习与方便自己使用，请遵守学校的网络管理规定。账号密码只保存在本机配置文件里。
