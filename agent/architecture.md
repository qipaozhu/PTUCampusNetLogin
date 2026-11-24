# 架构

## 总体分层

```
┌─────────────────────────────────────────────────────────┐
│  QML（qml/）              只管显示：颜色、布局、动画      │
│  main.qml / SettingsPage / TestPage / Toast / ToastCenter │
└───────────────────────┬─────────────────────────────────┘
                        │ context property + Q_PROPERTY
┌───────────────────────┴─────────────────────────────────┐
│  门面层（C++ 对 QML 暴露的对象）                          │
│  controller  network  portal  systemTheme  appFontFamily │
└───────────────────────┬─────────────────────────────────┘
┌───────────────────────┴─────────────────────────────────┐
│  业务层                                                   │
│  PortalLogin      登录流程状态机                          │
│  AutoReconnect    何时重试                                │
│  StartupCoordinator 启动决策                              │
│  Controller       账号/地址/系统设置                      │
└───────────────────────┬─────────────────────────────────┘
┌───────────────────────┴─────────────────────────────────┐
│  能力层                                                   │
│  PortalClient  协议    Network   网络状态   CaptchaOcr  OCR│
│  LanProbe      网卡    WinRegistry 注册表                  │
└───────────────────────┬─────────────────────────────────┘
┌───────────────────────┴─────────────────────────────────┐
│  基础设施                                                 │
│  utils/（纯函数）   platform/（唯一能碰系统 API 的地方）    │
└─────────────────────────────────────────────────────────┘
```

依赖方向**只从上往下**，没有任何一层回头依赖 QML。

## 启动时的对象关系（main.cpp）

```cpp
Controller      controller;   // 账号、Portal 地址、自启、系统策略
Network         network;      // 30s 轮询 + 手动刷新
SystemTheme     systemTheme;  // 深浅色
PortalLogin     portal(&controller, &network);          // 登录状态机
AutoReconnect   autoReconnect(&network, &portal);       // 掉线重连
PowerWatcher    powerWatcher([]{ network.refresh(); }); // 睡醒立刻检测
StartupCoordinator startup(&controller, &network, &portal); // 启动决策
AppTray         tray(appIcon, &portal);                 // 托盘
```

对象全部是 `main()` 的栈对象，销毁顺序是声明的逆序 —— 注意
`AutoReconnect` / `StartupCoordinator` 比 `PortalLogin` 晚声明，所以先析构，
不会出现悬空指针。

## 各模块职责边界

| 模块 | 负责 | 明确不负责 |
|---|---|---|
| `PortalLogin` | 流程推进、状态迁移、对外状态与文案 | 拼 URL、发请求、图像处理 |
| `PortalClient` | URL 构造、请求发送、响应收敛 | 流程控制、界面状态 |
| `CaptchaOcr` | 异步 OCR 调度、线程管理 | 图像算法（在 `CaptchaImage`） |
| `AutoReconnect` | 什么时候再试一次、退避多久 | 登录本身 |
| `StartupCoordinator` | 启动时该不该自动登录 / 探测既有会话 | 窗口显隐（在 main） |
| `Network` | 网络状态与 `checking` 标志 | 登录决策 |
| `Controller` | 账号、Portal 地址、系统设置持久化 | 登录状态（已迁出） |

> `Controller` 的两个系统设置（开机自启动、阻止弹浏览器）是**异步**的：
> setter 立即返回，实际写入在后台线程，结果通过 `xxxChanged` / `xxxBusy` 通知。
> 原因见 [threading.md](threading.md) 第 3 节。

## 关键设计决策

### 1. 流程用「事件表状态机」而不是回调链

早期是 `checkOnline()` 的回调里调 `probePortal()`、后者再调 `fetchCaptcha()`……
重试环藏在两个不同函数里，谁也说不清完整流程。

现在：

- `begin*()` 只做一步，拿到结果只 `dispatch(Event)`；
- `dispatch()` 是唯一决定"下一步去哪"的地方，一张 switch 读完全流程；
- `!m_running` 中止守卫从 7 处收敛成 1 处。

### 2. `Phase`（控制流）与 `State`（展示）分离

`State` 有 9 个值（给界面看进度），`Phase` 是内部控制阶段。
两者分开的好处：为了界面好看加个状态，不会污染控制流。

### 3. 状态单一来源

历史上 `Controller::LoginState`（4 态）和 `PortalLogin::State`（9 态）并存，
靠 6 处手工同步，漏一处就是 bug（注销功能被注释后状态就再也没复位过）。

现在 `Controller` 完全不存登录状态，`PortalLogin` 派生两个只读属性：

| 属性 | 类型 | 用途 |
|---|---|---|
| `statusKind` | `"notLoggedIn"` / `"loggingIn"` / `"loggedIn"` / `"failed"` | QML 判断 |
| `statusText` | 中文文案 | 托盘 + 界面 |

映射只有一份：`core/loginstate.cpp` 的 `kEntries` 表。

### 4. 启动决策收进 C++

以前"自启动→自动登录"在 `main.cpp`，"已在线→探测既有会话"在 `main.qml`
监听 `onlineChecked`。窗口没加载出来时行为就不一致。

现在全部由 `StartupCoordinator` 负责，QML 不参与任何启动判断。

### 5. 平台隔离

所有系统调用收在 `src/platform/`，其余代码只调 `Platform::` 门面。
移植 Linux 时只改 `platform.cpp` 的 `#else` 分支。
详见 [platform-layer.md](platform-layer.md)。

## 一次登录的数据流

```
用户点「登录」
  → PortalLogin::start()
  → startInternal()  清残留、校验账号、enterPhase(CheckOnline)
  → beginCheckOnline()  setState(Checking) → queryOnline(...)
      → Network::refresh() → LAN 探测(线程池) + HTTP 探测(异步)
      → onlineChecked(bool) → dispatch(OnlineOk/OnlineOffline)
  → ... 后续阶段见 login-flow.md
  → finishSuccess() / fail()
      → setState(Online/Failed) → emit stateChanged + toastRequested
      → QML 更新状态徽标 / ToastCenter 弹提示
```

注意 **`stateChanged` 先于 `toastRequested`**：`AutoReconnect` 先看到状态变化做出决策，
界面再弹提示。