# 登录状态机

实现在 `src/portal/portallogin.*`。**改流程只需要读这一份文档 + `dispatch()` 一个函数。**

## 三套枚举

### `State`（对外展示，9 个）

| 值 | 含义 | 界面表现 |
|---|---|---|
| `Idle` | 未登录 / 已中止 | 灰色「未登录」，收起 Toast |
| `Checking` | 检查网络 或 复检结果 | 「处理中」Toast |
| `ProbingPortal` | 探测 Portal | 进度文案 |
| `FetchingCaptcha` | 取验证码 | 进度文案 |
| `Recognizing` | OCR 识别中 | 进度文案 |
| `CheckingCaptcha` | 校验验证码 | 进度文案 |
| `LoggingIn` | 提交账号密码 | 进度文案 |
| `Online` | 已登录 | 绿色「已登录」，「成功」Toast |
| `Failed` | 失败 | 红色「登录失败」，「失败」Toast |

> QML **不要**直接比较这些数字，用 `portal.statusKind`。

### `Phase`（内部控制阶段，9 个）

`Idle` / `CheckOnline` / `ProbePortal` / `FetchCaptcha` / `Recognize` /
`CheckCaptcha` / `Login` / `VerifyOnline` / `Done`

和 `State` 不是一一对应：`CheckOnline` 和 `VerifyOnline` 都展示为 `Checking`，
但迁移规则完全不同 —— 这就是把它们分开的原因。

### `Event`（步骤上报的事实，13 个）

```
OnlineOk  OnlineOffline  OnlineTimeout
PortalUp  PortalDown
CaptchaFetched  CaptchaFetchFailed
OcrAccepted  OcrRejected
CaptchaAccepted  CaptchaRejected
LoginOk  LoginFailed
```

## 迁移表（`dispatch()` 原文结构）

| 阶段 | 事件 | 下一步 |
|---|---|---|
| `CheckOnline` | `OnlineOk` | `finishSuccess("当前已经可以正常上网")` |
| | `OnlineOffline` / `OnlineTimeout` | → `ProbePortal` |
| `ProbePortal` | `PortalUp` | → `FetchCaptcha` |
| | `PortalDown` | `handlePortalUnreachable()` |
| `FetchCaptcha` | `CaptchaFetched` | → `Recognize` |
| | `CaptchaFetchFailed` | `fail(...)` |
| `Recognize` | `OcrAccepted` | 记下验证码 → `CheckCaptcha` |
| | `OcrRejected` | `retryCaptcha(...)`（还有次数就回 `FetchCaptcha`） |
| `CheckCaptcha` | `CaptchaAccepted` | → `Login` |
| | `CaptchaRejected` | `retryCaptcha(...)` |
| `Login` | `LoginOk` | → `VerifyOnline` |
| | `LoginFailed` | `fail("登录失败：" + 原因)` |
| `VerifyOnline` | `OnlineOk` | `finishSuccess("登录成功，已可正常上网")` |
| | `OnlineTimeout` | `fail("网络状态检测超时")` |
| | `OnlineOffline` | `fail("登录已提交，但仍无法上网")` |

**任何阶段和事件对不上** → 打 `qWarning()`（`未处理的流程事件`），方便发现漏分支。
改流程后跑一遍，日志里不该出现这条。

## 中止守卫

`dispatch()` 开头唯一一处：

```cpp
if (!m_running)
    return;
```

所有迟到回调（HTTP 返回、OCR 结果、网络探测）都被这一行统一丢弃，
不需要每个回调各自判断。

## 验证码重试

- 上限 `kMaxCaptchaAttempts = 30`（`portallogin.cpp` 顶部）
- OCR 识别不出 4 位数字、或校验接口返回失败，都算一次
- 用尽后 `fail("验证码连续 30 次未通过…")`，转手动登录

## 登录后复检的重试

刚提交完登录，路由和 DNS 可能还没落定，首包失败很常见。
所以 `VerifyOnline` 遇到 `Offline` 会 **1.5 秒后重试一次**（`kMaxVerifyAttempts = 1`），
仍然失败才判定失败。

这一条很关键：不重试的话，一次网络抖动就会让状态变 `Failed`，
`AutoReconnect` 立刻再登一次，用户看到的就是"明明登录成功了还在反复登录"。

## 探测不到 Portal 时怎么办（`handlePortalUnreachable()`）

```
没有网卡？
  └─ 是 → fail("没有检测到网络连接，请先连接校园网")
        （开浏览器也没用，等用户连网）

有网卡但不是本校 Portal（酒店 / 机场 / 别的校园网）？
  └─ setPendingManualAuth(true)
     QDesktopServices::openUrl("http://www.msftconnecttest.com/redirect")
     fail("疑似非校园网，已在浏览器中打开认证页")
```

打开的地址会被当前网络劫持/302 到它自己的认证页，所以对任何 captive portal 都通用。

**`pendingManualAuth` 是给 `AutoReconnect` 的信号**：这种失败重试多少次都一样，
所以看到这个标记就不再做退避重试，等网络环境变化（换网 / 重新连上）再试一次。
这样后台重连也敢弹浏览器 —— 只会弹一次，不会每 5 分钟弹一个窗口。

## 后台 vs 手动

`m_background` 区分两种发起方式：

| | 手动（用户点「登录」） | 后台（`AutoReconnect`） |
|---|---|---|
| 触发方式 | `start()` | `startInBackground()` |
| 「检查中」展示状态 | 设为 `Checking`（有 Toast） | **不设**（避免误判时闪一下） |
| 失败后是否重试 | 不重试，交给用户 | 按退避重试（`pendingManualAuth` 除外） |
| 是否开浏览器 | 是 | 是 |

`startInternal()` 里 **先判断 `m_running` 再改 `m_background`** ——
否则后台重连会把正在跑的手动登录标成"后台任务"，失败后被当成后台失败进入重试循环。

## 时间参数一览

| 参数 | 值 | 位置 |
|---|---|---|
| 互联网探测超时 | 20 s | `Network::kProbeTimeoutMs` |
| 等探测结果的超时 | 20 s（同一个常量） | `portallogin.cpp` 复用 |
| Portal HTTP 请求超时 | 8 s | `PortalClientConfig::kRequestTimeoutMs` |
| 验证码最大尝试 | 30 次 | `portallogin.cpp` |
| 复检重试 | 1 次 / 1.5 s | `portallogin.cpp` |
| 后台轮询间隔 | 30 s | `NetworkConfig::kPollIntervalMs` |
| 掉线确认次数 | 连续 2 次失败 | `NetworkConfig::kOfflineConfirmCount` |