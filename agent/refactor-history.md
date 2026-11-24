# 重构记录

按时间顺序，说清每一轮改了什么、为什么。新接手时看这个能理解"为什么代码长这样"。

---

## 一、登录流程：回调链 → 事件表状态机

**问题**：`checkOnline()` 的回调里调 `probePortal()`，后者再调 `fetchCaptcha()`……
"下一步是谁"埋在各个 lambda 里。重试环出现两次（`recognizeCaptcha` 和 `checkCaptcha`
各自跳回 `fetchCaptcha`），没有任何一处能看到完整流程。

**改法**：

- 步骤函数重命名为 `begin*()`，只做一步，结果只 `dispatch(Event)`；
- 新增 `dispatch()`，全部迁移规则集中在一个 switch；
- 新增 `Phase` 枚举，与展示用的 `State` 分开；
- `!m_running` 守卫从 7 处收敛到 `dispatch()` 开头 1 处。

**收益**：读一个函数就能看完全流程；漏分支会打 `qWarning`；迟到回调用一行统一丢弃。

---

## 二、OCR 移出主线程

**问题**：`CaptchaOcr::recognize()` 在主线程同步跑 Tesseract，
每次调用都重新 `Init()` 加载几十 MB 语言包，再跑 7 个预处理变体 ——
界面冻结数秒，而验证码最多重试 30 次。

**改法**：

- `CaptchaOcr` 改成 `QObject`，内置专属 `QThread`；
- 引擎 `CaptchaOcrEngine` 常驻，只 `Init` 一次，之后 `Clear()` + `SetImage()` 复用；
- 异步接口 `recognize(image, callback)`，结果用 `Qt::QueuedConnection` 回到主线程；
- 用请求代号 `m_token` 丢弃过期结果；
- **早退**：某个变体给出"4 位数字 + 置信度 ≥ 85"就直接返回，不再跑完剩下的变体。

---

## 三、网络检测：阻塞调用挪出主线程

**问题**：`Network::checkLan()` 在主线程同步调用 `GetIpForwardTable` +
`QNetworkInterface::addressEntries()`，每 30 秒卡一下。

**改法**：抽出 `net/lanprobe.*`（纯函数），`Network` 用
`QThreadPool`（`maxThreadCount = 1`）执行，结果 `invokeMethod` 回主线程。
析构先 `waitForDone()`。

> 互联网探测（`msftconnecttest`）本来就是异步的，没动。

---

## 四、修"已登录却反复登录"

四个独立成因，全部修掉：

| # | 根因 | 修法 |
|---|---|---|
| 1 | 单次探测失败就判掉线（Portal 劫持探测地址、DNS 抖动都会导致），`AutoReconnect` 立刻用陈旧的 `online=false` 起一次登录 | `Network` 改成**连续 2 次失败**才判离线，恢复在线立即生效 |
| 2 | `startInternal()` 先改 `m_background` 再判断 `m_running`，后台重连会把正在跑的手动登录标成后台任务，失败后进入无限退避 | 把 `if (m_running) return;` 提到最前 |
| 3 | 登录后复检首包失败直接判 `Failed`，触发重登 | `VerifyOnline` 遇到离线 1.5s 后重试一次 |
| 4 | 误判触发的探测会把状态切到 `Checking`，QML 弹一次"处理中 → 成功"，看起来就是重复登录 | 后台流程**不设** `Checking` 展示状态；已经在线时 `finishSuccess` 的 `setState` 是 no-op，全程静默 |

另外：网卡断开时立刻判离线（不必等探测超时），保证唤醒后能快速重连。

---

## 五、非校园网的处置

**问题**：探测不到 Portal 时，后台重连不弹浏览器（怕每 5 分钟弹一个窗口），
而 `AutoReconnect` 的退避永不停止 —— 用户被困在一个"帮不上忙还一直重试"的状态里。

**改法**：

- 只要不是"没网卡"，前台后台**都**打开浏览器；
- 同时打上 `pendingManualAuth` 标记，`AutoReconnect` 见到就停掉退避，
  等网络环境变化再试。

因为重试链断了，后台也只弹这一次。

---

## 六、平台层拆分

**问题**：`windows.h` 散在 `controller.cpp`、`powerwatcher.cpp`、`defaultroute.cpp`、
`ncsiprobing.cpp` 里，移植 Linux 无从下手。

**改法**：

- 新增 `src/platform/`：`platform.*`（门面）、`winregistry.*`（注册表封装，
  头文件不暴露 `HKEY`）、`ncsiprobing.*`、`powerwatcher.*`；
- `platform.cpp` 里每个函数都是 `#ifdef Q_OS_WIN` 真实现 / `#else` 降级实现的成对结构；
- `controller.cpp` / `systemtheme.cpp` 改用 `Platform::`，不再包含系统头；
- `defaultroute.*` 并入 `Platform::defaultRouteInterfaceIndex()`。

`grep windows.h|iphlpapi.h|...` 现在只命中 `src/platform/` 三个文件。

---

## 七、去掉匿名 namespace

**要求**：不写 `namespace { }`。

**改法**：

| 原内容 | 去向 |
|---|---|
| `portallogin.cpp` 的文本辅助 | `utils/textutils.*` |
| `portalclient.cpp` 的 JSONP 解析 | `utils/jsonpresult.*` |
| `captchaocr.cpp` 的 8 个图像函数 | `utils/captchaimage.*` |
| `controller.cpp` 的地址解析 | `utils/portaladdress.*` |
| `network.cpp` 的 LAN 探测 | `net/lanprobe.*` |
| `main.cpp` 的图标 / 字体 / 窗口 / 失败提示 | `app/appshell.*` |
| `tray.cpp` 的状态文案 | `core/loginstate.*` |
| 其余小常量 / 小函数 | 文件级 `constexpr`，或**有名字**的 namespace（`NetworkConfig`、`StartupConfig`…） |

---

## 八、状态单一来源 + QML 去魔法数字

**问题**：`Controller::LoginState`（4 态）和 `PortalLogin::State`（9 态）并存，
靠 6 处手工 `setLoginState()` 同步；映射散落在 `tray.cpp`、`main.qml` 三个函数、
以及 `portal.state === 1/7/8/0` 里。

**改法**：

- 删掉 `Controller::LoginState` 枚举、属性、`setLoginState()`、成员变量；
- `LoginState::Kind`（4 态）+ 一张 `kEntries` 表给出英文标识和中文文案；
- `PortalLogin` 派生只读属性 `statusKind` / `statusText`；
- 托盘构造参数从 `Controller*` 改成 `PortalLogin*`；
- QML 全部改用 `portal.statusKind` 字符串判断；
- Toast 触发映射搬到 C++：`setState()` 里 `emit toastRequested(kind)`。

---

## 九、启动决策收进 C++

**问题**："自启动→自动登录"在 `main.cpp`，"已在线→探测既有会话"在 `main.qml`
监听 `onlineChecked`。QML 没加载出来时行为不一致。

**改法**：新增 `core/startupcoordinator.*`，三种情况（自启动 / 首次检测结果 / 超时）
统一在 C++ 决定，QML 的 `startupOnlineChecked` 属性和整个 `Connections` 块删除。

---

## 十、其他

- 统一超时：探测和"等探测结果"都用 `Network::kProbeTimeoutMs = 20000`，只有一个定义。
- Toast 逻辑从 `main.qml` 抽到 `ToastCenter.qml`；修掉状态更新时重播入场动画的问题。
- 提示文案整体压短，并给服务器返回的长文本加了 `abbreviate()` 截断。
- 按钮 / 输入框等加了函数级注释，`main.cpp` 瘦到 ~110 行。
- `CMakeLists.txt`：`windows.rc` 包进 `if(WIN32)`；`main.cpp` 的样式改成
  `#if` 判断 —— Windows 且 Qt ≥ 6.8 用 `FluentWinUI3`，其它平台用 `Material`。

  > 注：一开始写的是 `QQuickStyle::availableStyles()`，但这个 API 并不存在，
  > 编译时才发现。`QQuickStyle` 只有 `setStyle` / `style` / `name` / `setFallbackStyle`。

---

## 十一、"处理中" Toast 一闪而过

**现象**：有时候点登录，界面直接说"处理好了"，"正在自动处理"那张卡片根本没看见。

**原因**（不是漏发信号）：`setState(Checking)` 确实发了 `toastRequested("processing")`，
但本来就在线时整个流程只有一次 HTTP 往返（几十毫秒）。Toast 是独立原生窗口，
第一帧还没画出来内容就被 `success` 覆盖；`ToastCenter.show()` 在"已有窗口"时
只是立即更新内容，没有任何最短展示时长。

**改法**：`ToastCenter` 加 `minProcessingMs = 700` + `deferred` 队列 ——
`processing` 没露够时间就要被结果覆盖时，切换先排队，到点再补。
计时从 `processing` 最后一次出现算起，`hide()` / `destroyWindow()` 会清掉队列。

> 后台自动重连仍然**不**设 `Checking`（避免误判时闪"登录中"），
> 所以后台成功后只弹 `success`，这是有意为之，不算漏。