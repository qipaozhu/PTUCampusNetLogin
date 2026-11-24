# 界面（QML）

## 文件职责

| 文件 | 职责 |
|---|---|
| `main.qml` | 主窗口、侧栏导航、主页卡片、注销确认对话框 |
| `SettingsPage.qml` | 账号 / Portal 地址 / 自启动 / 阻止弹浏览器 |
| `TestPage.qml` | Toast 预览 + 回归用例（发布前可以隐藏） |
| `Toast.qml` | 独立的无边框置顶小窗口，一个 Toast 的样子和动画 |
| `ToastCenter.qml` | Toast 的创建 / 复用 / 销毁 / 自动隐藏计时 |

全部编进 qrc（`:/mainui/qml/...`），运行时不依赖散装文件。
新增 QML 文件要加进 `CMakeLists.txt` 的 `QML_FILES`。

## 铁律：QML 只管显示

| 该由 C++ 决定 | 该由 QML 决定 |
|---|---|
| 现在是什么状态 | 用什么颜色 / 字号 / 圆角 |
| 状态的中文文案（`portal.statusText`） | 文案在布局里的位置 |
| 该弹哪种 Toast（`portal.toastRequested`） | 弹窗动画怎么播 |
| 启动时该不该自动登录 | 窗口要不要显示 |

**不要在 QML 里写状态数字。** 判断状态用字符串：

```qml
color: root.loginStateBg(portal.statusKind)   // "notLoggedIn" / "loggingIn" / "loggedIn" / "failed"
text: portal.statusText
enabled: !portal.running && portal.statusKind !== "loggedIn"
```

## 状态 → Toast 的链路

```
PortalLogin::setState()
  → emit stateChanged()        （托盘、状态徽标跟着变）
  → emit toastRequested(kind)  （kind: processing / success / failed / hide）
      → main.qml 的 Connections 转发给 ToastCenter
          → ToastCenter.show(kind) / hide()
              → Toast.present(kind) / dismiss()
```

映射只在 `PortalLogin::setState()` 里定义一份。中间状态
（`ProbingPortal` / `FetchingCaptcha` / `Recognizing` / `CheckingCaptcha` / `LoggingIn`）
不发 Toast，只有 `Checking` / `Online` / `Failed` / `Idle` 会。

## Toast 生命周期

```
show(kind)
  ├─ 没有窗口 → createObject → 首次 present() 播入场动画（从右滑入 + 淡入）
  └─ 已有窗口 → present() 只更新内容：不播入场，只做
                 · contentFade（内容透明度 1 → 0.35 → 1）
                 · iconPop（图标 0.55 → 1.0）
                 · 描边颜色 180ms 过渡
  然后重启自动隐藏计时（success 3.5s，其它 7s）

hide()
  → Toast.dismiss() 播右滑退出 → dismissed 信号 → ToastCenter.destroyWindow()
```

**关键点**：`Toast.present()` 里有 `shown` 标记，只有"界面上本来没有 toast"时才播入场动画。
否则"处理中 → 成功"会再滑入一次，看起来像闪了一下。

### 「处理中」的最短展示时长

`ToastCenter.minProcessingMs = 700`：`processing` 还没露够 700ms 就要被
`success` / `failed` 覆盖时，切换先排队（`deferred`），到点再补上。

没这个下限会出一个很像 bug 的现象：本来就在线时，整个流程就是一次 HTTP 往返
（几十毫秒），Toast 是独立窗口、第一帧还没画出来内容就被改成"登录成功"，
用户看到的只有一张成功卡片，像是"处理中"根本没弹过。

配套细节：

- `shownAt` 只在 `present()` 收到 `processing` 时刷新，所以流程中途再次进入
  `Checking`（登录后确认上网）会重新计时；结果互相覆盖时时钟不会被重置。
- `hide()` / `destroyWindow()` 都会清掉排队的 `deferred`：流程中止或用户主动
  收起时，迟到的结果不该再把卡片弹回来。

`Toast.messageOverride` 是给测试页塞长文案用的，正常调用时为空。

## 主题

`SystemTheme`（C++）提供 `dark` / `bgColor` / `cardColor` / `textColor` /
`subTextColor` / `accentColor` / `accentSoft` / `hoverSoft`，
`main.qml` 顶部把 `systemTheme.xxx` 转成 `root.xxx` 别名，各页面读同一份。

系统深浅色切换时 `SystemTheme::darkChanged` 自动触发所有绑定更新。
`Toast.qml` 因为是独立窗口，不能继承 `root` 的属性，所以自己从 `systemTheme` 取。

遗留：登录状态的绿/红/蓝三个色值还硬编码在 `main.qml`
的 `loginStateColor()` / `loginStateBg()` 里，没有进 `SystemTheme`。
如果以后要支持自定义主题，应该挪过去。

## 设置页的两个开关是异步的

`开机自启动` 和 `阻止系统自动弹出浏览器` 的写入都在后台线程（后者还要等 UAC，
最长 60 秒），所以 **不能**在 `onToggled` 里同步比较结果：

```qml
Switch {
    checked: controller.autoStart
    enabled: !controller.autoStartBusy      // 写入期间禁用
    property bool syncing: false
    onToggled: {
        if (syncing) return;                // 程序化同步时不要再触发写入
        controller.autoStart = checked;     // 只是发起请求，立即返回
    }
}
```

真实状态要等 `autoStartChanged` / `blockAutoBrowserChanged` 回来再同步：

```qml
Connections {
    target: controller
    function onAutoStartChanged() {
        autoStartSwitch.syncing = true;
        autoStartSwitch.checked = controller.autoStart;
        autoStartSwitch.syncing = false;
    }
}
```

`syncing` 标记必须有：`checked` 赋值会触发 `toggled`，不拦住就会形成回环。

`阻止系统自动弹出浏览器` 整项在非 Windows 平台是隐藏的
（`visible: controller.supportsBrowserLaunchPolicy`）—— NCSI 是 Windows 独有的。

## 布局注意

- 侧栏宽 240，右侧内容区留 28 的外边距，窗口最小 900×720。
- 卡片高度尽量别写死，用 `height: xxxColumn.implicitHeight + 40` 自适应。
- 内容可能超出一屏的页面（设置页、测试页）用 `Flickable` + `ScrollBar`。
- **`Row` / `Column` 的子项不要加 `anchors`**，Qt 会告警且行为不可靠；
  需要居中就往里套一层 `Item`。

## 测试页

`TestPage.qml` 有三组用例：

| 分组 | 用例 |
|---|---|
| 单状态弹出 | 处理中 / 登录成功 / 登录失败 / 失败 + 超长原因 |
| 状态原地更新 | 处理中→成功、处理中→失败、成功→处理中（验证不重播入场动画） |
| 时序与生命周期 | 连续切换 5 段、重复同状态、收起 |

用例播放器会先 `hide()` 再等 420ms（等退场动画播完），保证从"界面上没有 toast"开始，
首帧的入场动画才真的会播。

**发布前**：`main.qml` 侧栏导航里的 `{ title: "测试" }` 注释掉。