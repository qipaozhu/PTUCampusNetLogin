# 平台层

目标：**除 `src/platform/` 之外，任何文件都不出现系统头文件。**
这样移植 Linux 只需要在一个 `platform.cpp` 里补 `#else` 分支。

## 当前唯一的系统 API 落点

| 文件 | 包含的系统头 | 用途 |
|---|---|---|
| `platform.cpp` | `winsock2.h` `windows.h` `iphlpapi.h` `shellapi.h` `wininet.h` | 见下表 |
| `winregistry.cpp` | `windows.h` | 注册表读写 |
| `powerwatcher.cpp` | `windows.h` | `WM_POWERBROADCAST` |

验证命令（应该只命中这三个文件）：

```bash
grep -rn "windows\.h\|iphlpapi\.h\|shellapi\.h\|wininet\.h\|winsock2\.h" src app
```

## `Platform` 门面（`src/platform/platform.h`）

| 函数 | Windows 实现 | Linux 该怎么做 |
|---|---|---|
| `isElevated()` | `OpenProcessToken` + `TokenElevation` | `geteuid() == 0` |
| `defaultRouteInterfaceIndex()` | `GetIpForwardTable`，取目的/掩码为 0 且 metric 最小的 | 解析 `/proc/net/route` 里 `Destination == 00000000` 那一行 |
| `isBrowserLaunchBlocked()` | 读 `HKLM\...\NetworkConnectivityStatusIndicator\NoWebBrowserLaunch` | 无对应概念，返回 `false` |
| `setBrowserLaunchBlocked(bool)` | 写 / 删上面的 DWORD | 返回 `false` |
| `notifyNetworkSettingsChanged()` | `InternetSetOption` + `WM_SETTINGCHANGE` 广播 | 空实现（或 `NetworkManager` 的 D-Bus） |
| `runElevatedAndWait(...)` | `ShellExecuteExW` + `lpVerb="runas"` + 等进程 | `pkexec` |
| `autostartCommand(name)` | 读 `HKCU\...\Run` | 读 `~/.config/autostart/<name>.desktop` 的 `Exec=` |
| `setAutostart(name, cmd)` | 写 `HKCU\...\Run` | 写一个 `.desktop` |
| `removeAutostart(name)` | 删该值 | 删该文件 |
| `systemPrefersDark()` | 先问 `QStyleHints::colorScheme()`，取不到再读 `Themes\Personalize\AppsUseLightTheme` | 取不到时读 `gsettings get org.gnome.desktop.interface color-scheme` |
| `supportsBrowserLaunchPolicy()` | `true` | `false`（NCSI 是 Windows 独有概念） |

## `WinRegistry`（`src/platform/winregistry.h`）

头文件里**不出现 `HKEY`**，根键用字符串常量表示：

```cpp
WinRegistry::readDword(WinRegistry::kLocalMachine, "SOFTWARE\\...", "Value", &out);
```

- 统一走 64 位视图（`KEY_WOW64_64KEY`），避免被重定向到 `Wow6432Node`；
- 非 Windows 平台所有函数返回失败 / 空串，上层逻辑自然降级。

## `NcsiProbing`（`src/platform/ncsiprobing.*`）

Windows 独有功能：**禁止系统在检测到需要认证时自动拉起浏览器**。

- 用组策略值 `NoWebBrowserLaunch = 1`，只关"拉浏览器"，不动主动探测；
- 键在 `HKLM`，需要管理员权限，所以没有权限时用 `ShellExecuteEx(runas)`
  起本程序的另一个实例（带 `--apply-ncsi-policy=0|1`）去写，写完立刻退出；
- 主进程用 `Platform::runElevatedAndWait` 等它，结束后**以注册表实际状态为准**判断成败；
- 提权过程记录在 `%LOCALAPPDATA%/Maplekilo/PTUCampusLogin/ncsi.log`。

小进程入口在 `main.cpp` 最早期：

```cpp
if (NcsiProbing::isHelperInvocation(arguments))
    return NcsiProbing::runHelper(arguments);
```

**必须在单实例判断之前**，否则会被正在运行的主实例当成第二个实例踢掉。

## `PowerWatcher`（`src/platform/powerwatcher.*`）

监听 `WM_POWERBROADCAST` 的 `PBT_APMRESUMEAUTOMATIC` / `PBT_APMRESUMESUSPEND`，
睡醒后立刻触发一次 `network.refresh()`，省掉最多 30 秒的轮询等待。

只是"快一点"的优化，收不到消息也不影响功能。
Linux 以后接 `org.freedesktop.login1` 的 `PrepareForSleep` 信号。

## 移植 Linux 的清单

1. `CMakeLists.txt`：`find_package` 的组件、`target_link_libraries` 里的
   `advapi32 wininet shell32 iphlpapi` 已经包在 `if(WIN32)` 里，无需改；
   `resources/windows.rc` 同样已包在 `if(WIN32)`。
2. `platform.cpp`：补 `#else` 分支（上表逐条）。
   - 注册表相关全部改成文件 / gsettings；
   - 提权改成 `pkexec`；
   - 系统主题接 `xdg-desktop-portal` 或 `gsettings`。
3. `powerwatcher.cpp`：补 `#else`（D-Bus 监听）。
4. `ncsiprobing.*`：整块是 Windows 独有，Linux 下保持"全部返回失败"即可，
   界面上那个开关会一直是关的 —— 可以考虑在 QML 里按平台隐藏。
5. `main.cpp`：样式用 `#if` 判断 —— Windows 且 Qt ≥ 6.8 用 `FluentWinUI3`，
   其它平台用 `Material`（跟随系统深浅色，跨平台可用）。
6. 字体：`resources` 里的 `font.otf` 是内置的，不依赖系统字体，可直接用。
7. `QSystemTrayIcon`：Linux 需要一个支持 `StatusNotifierItem` 的桌面环境。

## 遗留

迁移时留下的空壳 / 转发头已于 2026-11-24 删除：

- `src/net/ncsiprobing.{h,cpp}` → 现在只有 `src/platform/ncsiprobing.*`
- `src/net/defaultroute.{h,cpp}` → 功能并入 `Platform::defaultRouteInterfaceIndex()`
- `app/powerwatcher.{h,cpp}` → 现在只有 `src/platform/powerwatcher.*`

`src/net/` 现在只剩 `network.*`（网络状态）和 `lanprobe.*`（网卡探测）。
新代码请直接 include `platform/...` 路径，不要再建转发头。