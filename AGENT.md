# AGENT.md

给在这个仓库里干活的 AI / 新人的总览。**先读这份，再按需跳到 `agent/` 下的专题文档。**

---

## 这是什么

莆田学院校园网（drcom Portal）自动登录工具。Windows 桌面程序，常驻托盘，
开机自启后在后台自动完成"探测 → 取验证码 → OCR → 校验 → 登录 → 复检"，
掉线后按退避策略自动重连。

| 项 | 值 |
|---|---|
| 语言 / 标准 | C++17 |
| 框架 | Qt 6（Quick / QML / Widgets / Network / QuickControls2） |
| 构建 | CMake ≥ 3.16 + MSVC（Windows x64） |
| 可选依赖 | Tesseract 5（vcpkg 安装，找不到时验证码功能自动降级为手动登录） |
| 当前平台 | Windows 10/11 x64（代码已为 Linux 预留平台层） |

---

## 硬性约定（改代码前务必看）

1. **系统头文件只允许出现在 `src/platform/`**
   `windows.h` / `iphlpapi.h` / `shellapi.h` / `wininet.h` / `winsock2.h`
   只在 `platform.cpp`、`winregistry.cpp`、`powerwatcher.cpp` 里。
   其它任何文件需要平台能力，一律走 `Platform::` 命名空间（见 `src/platform/platform.h`）。

2. **不写匿名 namespace**
   小常量用文件级 `constexpr`；成组的常量 / 小函数用**有名字**的 namespace
   （`NetworkConfig`、`PortalLoginConfig`、`StartupConfig`…）；成块的逻辑拆独立文件。

3. **登录状态只有一个来源**
   唯一存储是 `PortalLogin::m_state`。`Controller` 里**没有**登录状态。
   界面判断用 `portal.statusKind`（字符串），**不要在 QML 里写状态数字**。

4. **QML 只管显示**
   状态语义、文案、该弹哪种 Toast、启动决策，全部在 C++ 里决定。
   QML 只负责颜色、布局、动画。

5. **耗时操作不许在主线程**
   OCR 在专属 `QThread`；本地网卡探测在 `QThreadPool`；
   互联网探测靠 `QNetworkAccessManager` 天然异步。

---

## 目录地图

```
app/            程序入口与桌面集成（只做组装，不含业务逻辑）
  main.cpp          组装：单实例 → 字体 → 对象 → QML → 托盘 → 启动
  appshell.*        图标、字体、主窗口访问、加载失败提示
  tray.*            系统托盘菜单
  startupdiag.*     启动期日志捕获
src/
  platform/       ★ 全部平台相关代码（Linux 移植只改这里）
    platform.*        权限 / 路由表 / 自启动 / 主题 / 提权 / 系统策略
    winregistry.*     注册表封装（头文件不暴露 HKEY）
    ncsiprobing.*     NCSI 策略 + UAC 提权小进程
    powerwatcher.*    睡眠恢复事件
  utils/          跨平台纯工具，无副作用、可单测
    textutils.*       验证码文本校验、长文案截断
    portaladdress.*   "host:port" 解析校验
    jsonpresult.*     JSONP / JSON 返回体解析
    captchaimage.*    验证码图像预处理
    applog.*          落盘日志
  net/
    network.*         网络状态检测 + `checking` 状态
    lanprobe.*        本地网卡探测（阻塞调用，供线程池用）
  core/
    controller.*      账号 / Portal 地址 / 自动启动 / 系统策略（面向 QML 的门面）
    singleinstance.*  单实例守卫
    loginstate.*      4 态粗粒度视图 + 文案（唯一映射）
    startupcoordinator.*  启动决策
  portal/
    portallogin.*     登录状态机（流程中枢）
    portalclient.*    Portal 协议层（URL / 请求 / 响应）
    captchaocr.*      OCR 外观类 + 引擎（工作线程）
    autoreconnect.*   掉线自动重连的时机与退避
  ui/
    systemtheme.*     系统深浅色主题
qml/            main / SettingsPage / TestPage / Toast / ToastCenter
cmake/          tesseract.cmake
resources/      qrc、字体、图标、windows.rc
```

依赖目录 `vcpkg_installed/`、构建产物 `out/build/`、输出 `build/` —— **都不要手改**。

---

## 登录流程一句话版

```
Idle
 └─检查网络 ──在线──> 直接成功
            └─离线/超时──> 探测 Portal ──不通──> 开浏览器，转手动
                                     └─通──> 取验证码 → OCR → 校验
                                                ├─不通过（≤30 次）→ 回到取验证码
                                                └─通过 → 登录 → 复检 → 成功/失败
```

所有"下一步去哪"只在 `PortalLogin::dispatch()` 一处决定。
细节见 [agent/login-flow.md](agent/login-flow.md)。

---

## 常见改动怎么下手

| 想做的事 | 改哪里 |
|---|---|
| 加一步流程 | `portallogin.h` 加 `Phase` + `Event`；`begin*()` 只发事件；在 `dispatch()` 加迁移；`enterPhase()` 加一行 |
| 改超时 / 重试次数 | 探测超时 `Network::kProbeTimeoutMs`（全项目只此一处）；验证码次数 `portallogin.cpp` 顶部 `kMaxCaptchaAttempts`；退避间隔 `autoreconnect.cpp` 顶部 `kRetryDelaysMs` |
| 改提示文案 | 状态文案 `core/loginstate.cpp` 的 `kEntries` 表；具体失败原因在 `portallogin.cpp` |
| 加平台能力（如 Linux 自启动） | 只改 `src/platform/platform.cpp` 的 `#else` 分支 |
| 改界面 | `qml/`；不要在新 QML 里写状态数字，用 `portal.statusKind` |
| 调验证码识别率 | `utils/captchaimage.cpp`（预处理）+ `captchaocr.cpp`（变体顺序、`kAcceptConfidence`） |

---

## 已知问题 / 待办

完整清单见 [agent/todo.md](agent/todo.md)，挑几条最要紧的：

- 最近几轮重构（平台层拆分、状态机、线程化、状态单一来源）**没有在本机编译验证过**，
  改动后的第一件事是完整构建。
- 配置文件里的密码只做了 Base64 编码，**不是加密**（Windows 上可换 `CryptProtectData`）。
- 注销接口未实现，只弹提示。
- `qml/TestPage.qml` 现在挂在侧栏导航上，发布前把 `{ title: "测试" }` 注释掉。

> 迁移遗留的空壳文件（`src/net/ncsiprobing.*`、`src/net/defaultroute.*`、
> `app/powerwatcher.*`）已于 2026-11-24 删除，`src/net/` 现在只剩 `network.*` 和 `lanprobe.*`。

---

## 文档索引

| 文档 | 内容 |
|---|---|
| [agent/architecture.md](agent/architecture.md) | 分层架构、对象关系、数据流 |
| [agent/login-flow.md](agent/login-flow.md) | 登录状态机：状态 / 阶段 / 事件表、重试与失败策略 |
| [agent/threading.md](agent/threading.md) | 线程模型与线程安全规则 |
| [agent/platform-layer.md](agent/platform-layer.md) | 平台抽象、Windows API 清单、Linux 移植清单 |
| [agent/ui.md](agent/ui.md) | QML 结构、状态映射、Toast 生命周期、主题 |
| [agent/build-and-debug.md](agent/build-and-debug.md) | 构建、依赖安装、配置与日志位置、调试开关 |
| [agent/refactor-history.md](agent/refactor-history.md) | 几轮重构做了什么、为什么 |
| [agent/todo.md](agent/todo.md) | 待办与已知缺陷 |