# 构建说明

莆田学院校园网自动登录（Qt 6 / QML）在 Windows 上的构建、运行和发布步骤。

## 1. 环境要求

| 项目 | 说明 |
| --- | --- |
| 系统 | Windows 10 / 11 x64 |
| 编译器 | Visual Studio 的 MSVC x64 工具链（VS 2022 及以上） |
| CMake | 3.21 以上（`CMakePresets.json` 需要；`CMakeLists.txt` 本身最低 3.16） |
| 生成器 | Ninja（VS 自带或独立安装） |
| Qt | 6.12.0+ msvc2022_64，我的环境默认装在 `D:/Qt/6.12.0/msvc2022_64` |
| vcpkg 依赖 | `vcpkg_installed/`（Tesseract OCR，可选；不入库，没有就自动关闭 OCR，见 3.1） |

## 2. 工程结构

```
app/                                                     入口和桌面集成
  main.cpp                                               只做组装：单实例、引擎、托盘、自启动
  startupdiag.h/.cpp                                     启动失败的日志捕获与 startup-error.log
  tray.h/.cpp                                            系统托盘菜单
  powerwatcher.h/.cpp                                    监听睡眠恢复消息，醒来立刻检测网络
src/core/                                                controller（配置 / 登录状态）、singleinstance
src/net/                                                 network（检测）、defaultroute（Win32 默认路由）、ncsiprobing（NCSI 组策略）
src/portal/                                              portalclient（协议层：URL / 请求 / JSONP）、portallogin（登录状态机）、autoreconnect（掉线自动重连）、captchaocr（验证码 OCR）
src/ui/                                                  systemtheme（深浅色判断 + 整套配色）
qml/                                                     main.qml / Toast.qml / SettingsPage.qml / TestPage.qml
resources/                                               app_icon.ico / app_logo.png / font.otf / resources.qrc / windows.rc
cmake/tesseract.cmake                                    可选的 Tesseract 查找、链接与运行时复制
CMakeLists.txt                                           构建脚本（按 1~8 小节编号排列）
CMakePresets.json                                        Qt 预设
CMakeUserPresets.json                                    本机 Debug-x64 / Release-x64 预设（含 QTDIR，已忽略）
vcpkg.json                                               Tesseract 依赖声明
```

QML 通过 `qt_add_qml_module()` 编进 exe 的 `qrc:/mainui/qml/main.qml`，所以运行时不依赖散装的 `.qml` 文件。
C++ 头文件统一从 `src/` 根和 `app/` 根开始写（如 `#include "core/controller.h"`、`#include "tray.h"`），
由 `target_include_directories()` 提供搜索路径，和文件放在哪一层子目录无关。

## 3. 配置工程

### 3.1 vcpkg 依赖（可选，自动识别验证码的依赖，建议配置）

`vcpkg_installed/` 是 vcpkg 导出的依赖目录（Tesseract OCR 及其图片库依赖），体积很大，已经写进 `.gitignore`，不会提交到仓库。

新机器上没有这个目录时，在工程根目录执行一次（路径按本机 VS 的 vcpkg 调整）：

```bat
vcpkg.exe install --triplet x64-windows
```

没有它也能正常编译，只是 `cmake` 会打印 `Tesseract OCR not found, captcha OCR disabled`，验证码识别功能关闭。

### 3.2 配置 CMake

`CMakeUserPresets.json` 记录本机的 `QTDIR` 和 Qt VS Tools 生成的 preset，属于机器相关文件，同样在 `.gitignore` 里。本机已经有时，直接用预设：

```bat
set QTDIR=D:\Qt\6.12.0\msvc2022_64

cmake --preset Debug-x64
cmake --preset Release-x64
```

新机器上没有这个文件（或被 git 忽略掉）时，用命令行直接配置，效果一样：

```bat
cmake -S . -B out/build/debug   -G Ninja -DCMAKE_BUILD_TYPE=Debug   -DCMAKE_PREFIX_PATH=D:/Qt/6.12.0/msvc2022_64
cmake -S . -B out/build/release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=D:/Qt/6.12.0/msvc2022_64
```

也可以用 Visual Studio「打开文件夹」或 Qt Creator 打开工程，参数保持和上面一致。

## 4. 编译

```bat
cmake --build out/build/debug
cmake --build out/build/release
```

CMake 中间文件在 `out/build/<config>/`，可执行文件统一输出到源码根目录：

```
build/debug/PTUCampusNetLogin.exe    （含 pdb）
build/release/PTUCampusNetLogin.exe
```

编译后的 POST_BUILD 步骤会自动把 Tesseract 相关文件复制到 exe 同级：

- `build/<config>/tesseract55.dll`（Debug 是 `tesseract55d.dll`）以及 leptonica / libpng / libwebp 等依赖 DLL
- `build/<config>/tessdata/eng.traineddata`

如果没找到 Tesseract，`cmake` 配置输出里会写 `Tesseract OCR not found, captcha OCR disabled`，其余功能不受影响。

## 5. 运行

- **Debug**：直接在 Visual Studio 里 F5，或用 Qt Creator 运行。Qt 的 DLL 由 IDE 的环境变量（PATH）提供，不需要额外复制。
- **Release**：`build/release` 里只有 vcpkg 的依赖，Qt 的运行库、平台插件和 QML 模块需要自己部署，见下一节。

## 6. 发布（Release 拷到别的目录）

1. 先关掉正在运行的实例（托盘里的那个），否则 DLL 被占用。
2. 把整个 `build/release` 目录拷到目标位置。
3. 用 Qt 自带的 `windeployqt` 补齐 Qt 运行库和平台插件：

```bat
"D:\Qt\6.12.0\msvc2022_64\bin\windeployqt.exe" --release --no-translations ^
    "<目标目录>\PTUCampusNetLogin.exe" ^
    --qmldir "<项目目录>\qml"
```

4. 部署完尝试双击程序启动下

## 7. 启动失败怎么排查

程序是 WIN32 无控制台窗口的，双击运行时不会打印任何东西。`main.cpp` 里做了兜底：

- QML 加载失败会弹窗，并把完整警告写进 exe 同级的 `startup-error.log`（不可写时写到 `%TEMP%`）。

也可以看进程退出码：

```bat
cd /d <目标目录>
"PTUCampusNetLogin.exe"
echo %errorlevel%
```

| 退出码 | 含义 |
| --- | --- |
| `9009` | cmd 没找到这个 exe（路径或文件名不对），程序没启动 |
| `-1073741515`（0xC0000135） | 缺 DLL（例如 `tesseract55.dll`、`Qt6Core.dll`） |
| `-1`（0xFFFFFFFF） | 程序跑起来了，但 QML 加载失败（缺 qml 模块 / 样式），看 `startup-error.log` |
| `0` | 正常退出；如果是秒退，多半是单实例拦截：托盘里已经有一个实例，新进程通知完就退 |

其他注意点：

- **单实例**：程序已在运行时，再次启动只会让已有窗口显示出来，然后以 0 退出。
- **配置文件**：`%LOCALAPPDATA%\Maplekilo\PTUCampusLogin\config.json`。
- **开机自启动**：写在 `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`，命令带 `--autostart` 参数；程序移动位置后需要在「设置」里重新打开一次。
