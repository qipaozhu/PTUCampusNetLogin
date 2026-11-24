# 待办与已知缺陷

按优先级排列。做完一条就删一条，完成的记到文末「已完成」。

---

## P0：必须处理

### 1. 编译验证

平台层拆分、状态机、线程化、状态单一来源、设置异步化这几轮改动
**没有在本机完整编译过**。改动后的第一件事应该是：

```bash
cmake --build out/build/debug
```

重点盯：

- `portallogin.h` 的 `#include "core/loginstate.h"`（include 根是 `src/`）；
- `Network::kProbeTimeoutMs` 是 `static constexpr`，C++17 下类内定义即可；
- `StartupCoordinator` 对 `PortalLogin::Idle` 的引用；
- `Controller` 新增的 `QThreadPool` 成员和 `QRunnable::create`（Qt 5.15+）；
- `QStringLiteral("PTUCampusNetLogin/" PTU_APP_VERSION)` 的字符串拼接是否被 CMake 定义覆盖到；
- 新增的 `src/platform/*.cpp`、`src/utils/*.cpp` 是否被 GLOB 收进来。

---

## P1：功能与安全

### 2. 密码不是加密

`Controller::save()` 只对密码做了 Base64（代码注释里也写明了）。
Windows 上换 `CryptProtectData`（DPAPI）成本很低：

```cpp
DATA_BLOB in{...}, out{};
CryptProtectData(&in, L"PTUCampusLogin", nullptr, nullptr, nullptr, 0, &out);
```

### 3. 注销接口未实现

`PortalClient::logout()` 是空的；`PortalLogin::logout()` 只 `stop()` 后弹一句
"暂不支持注销，请手动打开浏览器"。要么按 drcom 的 `logout` 接口实现，
要么把 QML 里那个"注销"按钮改成"打开浏览器"。

### 4. 测试页还挂在导航上

`main.qml` 侧栏有 `{ title: "测试" }`。发布前注释掉。

### 5. 登录状态配色还硬编码在 QML

`main.qml` 的 `loginStateColor()` / `loginStateBg()` 里写着 `#FF107C10` 等色值，
没有进 `SystemTheme`。如果以后要做自定义主题，应该挪过去。

---

## P2：可维护性

### 6. `loginStateHint()` 还在 QML

`main.qml` 里那条"尚未完成校园网认证"之类的兜底提示是 QML 写的字符串。
按"文案归 C++"的约定，应该并进 `core/loginstate.*`。

### 7. `Toast.messageOverride` 是测试后门

只给测试页塞长文案用。正式版可以考虑去掉，或者用 `#ifdef` 包起来。

### 8. `SettingsPage.qml` 快 600 行了

可以拆成「账号卡 / 地址卡 / 系统设置卡 / 关于卡」四个子组件。

### 9. Linux 平台实现

`src/platform/platform.cpp` 的 `#else` 分支全是降级实现，
移植时按 [platform-layer.md](platform-layer.md) 里的对照表逐条补。
`powerwatcher.cpp` 的 D-Bus 监听也还没写。

---

## P3：工程质量

### 10. 没有单元测试

`utils/` 下的都是纯函数，很适合补 QtTest：

- `CaptchaImage` —— 用固定的样本验证码图做回归，防止调参时改坏；
- `PortalAddress::parse` —— 各种畸形输入的边界；
- `JsonpResult::parse` —— JSONP / JSON / 缺字段 / 非法 JSON。

`PortalClient` 目前是具体类，要单测 `PortalLogin` 的状态机需要先抽个接口
（虚函数即可），然后用假 client 跑：重试 30 次、中途 stop、验证码被拒等分支。

### 11. `Controller` 职责仍然偏重

现在管着：账号密码持久化 / Portal 地址 / 管理员检测 / 开机自启动 / 系统策略。
可以再拆成 `SettingsStore`（配置读写）+ `SystemPolicy`（自启动 + NCSI），
`Controller` 只做 QML 门面。

### 12. 没有 CI

至少加一个 Windows 的构建检查，保证每次提交都能编过。

---

## 已完成

### 2026-11-24 · 系统设置改为异步，修掉界面卡死

**问题**：`setAutoStart()` 在主线程写注册表，`setBlockAutoBrowser()` 更严重 ——
它要 `ShellExecuteEx(runas)` 弹 UAC 并 `WaitForSingleObject` 等提权小进程结束
（最长 60 秒），这几十秒里整个界面完全冻结。

**改法**：

- `Controller` 增加 `QThreadPool m_workPool`（maxThreadCount = 1，串行执行）；
- 两个 setter 变成"立即返回 + 后台执行 + 结果回主线程"；
- 新增 `autoStartBusy` / `blockAutoBrowserBusy` 属性，界面据此禁用开关并转圈；
- `blockAutoBrowser()` 改成返回缓存值，不再每次绑定求值都读一遍注册表；
- `Platform::runElevatedAndWait()` 补 `SEE_MASK_NOASYNC` —— 线程池线程没有消息循环，
  不加这个标志 `ShellExecuteEx` 会异步化，等待语义就失效了；
- 析构里 `m_workPool.waitForDone()`。

### 2026-11-24 · 迁移遗留文件清理

删除 `src/net/ncsiprobing.{h,cpp}`、`src/net/defaultroute.{h,cpp}`、
`app/powerwatcher.{h,cpp}`（实现早已搬到 `src/platform/`）。
现在 `src/net/` 只剩 `network.*` 和 `lanprobe.*`。

### 2026-11-24 · 平台样式 + 版本注入 + 小清理

- `QQuickStyle::availableStyles()` 这个 API 不存在（编译报错才发现），
  改成 `#if` 判断：Windows 且 Qt ≥ 6.8 用 `FluentWinUI3`，其它平台用 `Material`。
- 工程版本通过 `PTU_APP_VERSION` 宏注入到 UA 里，不再写死 `0.1`。
- `NcsiProbing` 是 Windows 独有概念：新增 `Platform::supportsBrowserLaunchPolicy()`，
  非 Windows 平台在设置页隐藏「阻止自动弹出浏览器」整项。
- `PortalLogin::detectExistingSession()` 去掉 `Q_INVOKABLE`（只给 `StartupCoordinator` 用）。
- 删掉 `PortalLogin::state` 的 `Q_PROPERTY`（QML 已改用 `statusKind`）。
- 删掉 `main.cpp` 里没用的 `#include <QTimer>`。

### 2026-11-24 · 「处理中」Toast 一闪而过

不是漏发信号，是显示层没有最短停留时间：本来就在线时整个流程只有一次 HTTP 往返，
Toast 窗口第一帧还没画出来，内容就被 `success` 覆盖了。
`ToastCenter` 加 `minProcessingMs = 700` + `deferred` 排队，详见
[refactor-history.md](refactor-history.md) 第十一节和 [ui.md](ui.md)。

---

## 已确认不算问题

- **`CacheOverflowException`**：VS 输出窗口里的首次机会异常，
  被第三方库内部捕获，程序正常运行。项目代码里没有 `throw`，不需要处理。
- **`resources/windows.rc` 里的版本号是手写的**，改版本时要和 `CMakeLists.txt`
  的 `APP_VERSION` 一起改（文件里已有注释提醒）。