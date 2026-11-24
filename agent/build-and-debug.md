# 构建与调试

> 面向使用者的完整构建 / 运行 / 发布步骤在根目录的 [BUILD.md](../BUILD.md)，
> 这里只补充**开发时排查问题**需要的内部信息。

## 环境

| 项 | 要求 |
|---|---|
| 编译器 | MSVC（VS 2022 及以上） |
| CMake | ≥ 3.16（工程里用的是 4.x 预设） |
| Qt | 6.x，需要 Quick / Widgets / Gui / Network / QuickControls2 |
| 可选 | Tesseract 5 + vcpkg（没有也能编，验证码降级为手动登录） |

## 构建

```bash
cmake --build out/build/debug
cmake --build out/build/release
```

- 中间产物：`out/build/<debug|release>/`
- 最终 exe 和 pdb：`build/<Debug|Release>/`
- `Tesseract` 找到时，`cmake/tesseract.cmake` 会把 DLL 和 `tessdata` 拷到 exe 旁边

源文件用 `file(GLOB_RECURSE ... CONFIGURE_DEPENDS)` 收集 —— **新增 `.cpp` 不用改
`CMakeLists.txt`**，但要重新 configure（CMake 会自动触发）。
新增 **QML 文件必须**加进 `QML_FILES` 列表。

## 安装可选依赖

```bash
"D:/Program Files/Microsoft Visual Studio/18/Community/VC/vcpkg/vcpkg.exe" install --triplet x64-windows
```

查找顺序：`TESSERACT_ROOT` → `vcpkg_installed/x64-windows` → `third_party/tesseract`。
找不到时只打印一行 `Tesseract OCR not found, captcha OCR disabled`，不报错。

## 运行时文件位置

| 内容 | 路径 |
|---|---|
| 配置文件 | `%LOCALAPPDATA%\Maplekilo\PTUCampusLogin\config.json` |
| 提权日志 | `%LOCALAPPDATA%\Maplekilo\PTUCampusLogin\ncsi.log` |
| 启动失败日志 | exe 同目录 `startup-error.log`（不可写时退到临时目录） |
| 验证码调试图 | `%TEMP%\ptu_captcha\variant_N.png` |

## 命令行参数

| 参数 | 作用 |
|---|---|
| `--autostart` | 由开机自启动拉起，启动后自动登录，窗口保持隐藏 |
| `--apply-ncsi-policy=0\|1` | 提权小进程入口：1 禁止拉浏览器，0 恢复。写完注册表立即退出 |

## 调试开关

| 环境变量 | 作用 |
|---|---|
| `PTU_CAPTCHA_DUMP=1` | 把每个预处理变体存成 PNG，方便离线调 OCR 参数 |
| `TESSDATA_PREFIX` | 手动指定 tessdata 目录 |

## 注册表位置

| 键 | 用途 |
|---|---|
| `HKCU\Software\Microsoft\Windows\CurrentVersion\Run\PTUCampusLogin` | 开机自启动 |
| `HKLM\SOFTWARE\Policies\Microsoft\Windows\NetworkConnectivityStatusIndicator\NoWebBrowserLaunch` | 1 = 禁止系统自动拉浏览器（需要管理员权限） |

> 注意键名里 **`NetworkConnectivityStatusIndicator` 没有空格**。
> 网上流传的 "Network Connectivity Status Indicator"（带空格）是错的，Windows 不读。

## 调试技巧

**VS 里看到 `0x00007FFD… 处引发的异常: Microsoft C++ 异常: CacheOverflowException`**
—— 这是**首次机会异常**，被第三方库内部捕获，程序继续正常运行，不需要处理。
项目代码里没有任何 `throw`。想确认来源：调试 → 异常设置 → 勾选 C++ Exceptions，
让它断下来看调用堆栈最上面的模块名。

**程序双击没反应**：`WIN32_EXECUTABLE` 没有控制台，
看 exe 同目录的 `startup-error.log`，或在 VS 里启动看输出窗口。

**界面卡顿**：先确认没有阻塞调用跑到主线程（见 [threading.md](threading.md)）。
OCR 和网卡探测都应该是异步的。

**登录成功后还反复登录**：`AutoReconnect` 的触发条件依赖 `Network::online`，
而 `online` 有"连续 2 次失败才判离线"的去抖。如果还出现，看
`NetworkConfig::kOfflineConfirmCount` 和 `Portallogin` 的复检重试。

## 没有的东西

- **没有单元测试**。`utils/` 下的函数（尤其 `captchaimage`、`portaladdress`、
  `jsonpresult`）都是纯函数，很适合补 QtTest。
- **没有 CI**。