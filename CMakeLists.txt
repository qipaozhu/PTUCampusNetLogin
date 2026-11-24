# ============================================================================
# 莆田学院校园网自动登录 —— 顶层构建脚本
#
#   1. 工程与编译标准      cmake_minimum_required / project / C++17
#   2. 默认构建类型        Ninja 这种单配置生成器补一个默认值
#   3. Qt 依赖             查找 Qt6 及其组件
#   4. 源码、资源与目标     app/ src/ resources/ -> PTUCampusNetLogin
#   5. QML 模块            qml/ 下的界面编进 exe 的 qrc
#   6. 链接库              Qt 模块 + Windows 系统库
#   7. 输出目录            exe / pdb 统一落到 build/<debug|release>
#   8. 可选功能            验证码 OCR，见 cmake/tesseract.cmake
# ============================================================================

# ---------------------------- 1. 工程与编译标准 ----------------------------
cmake_minimum_required(VERSION 3.16)
set(APP_VERSION 1.1.0)
project(PTUCampusNetLogin LANGUAGES CXX VERSION ${APP_VERSION})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------- 2. 默认构建类型 ------------------------------
# 单配置生成器（Ninja）下没有指定构建类型时，$<CONFIG> 会展开成空字符串，
# 输出目录就会变成 build/ 本身；补个默认值，保证 exe 一定落在 build/<debug|release> 里
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "构建类型" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

# ---------------------------- 3. Qt 依赖 -----------------------------------
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core)
find_package(Qt${QT_VERSION_MAJOR}
    COMPONENTS
        Quick Widgets Gui Network QuickControls2
)

# Qt 的元对象 / 资源处理，必须在 add_executable 之前打开
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# ---------------------------- 4. 源码、资源与目标 --------------------------
# 代码：app/ 是入口和桌面集成（托盘、启动诊断），src/ 按职责分子目录。
# 用 GLOB + CONFIGURE_DEPENDS 省去每加一个文件就改一次这里，
# 代价是新增文件后需要重新 configure（CMake 会自动触发）。
file(GLOB_RECURSE SOURCE_FILE CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/app/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

# 资源：resources.qrc 提供托盘 / 窗口图标 / 字体；
# windows.rc 只给 Windows 的 exe 提供文件图标，其它平台不需要
file(GLOB RESOURCE_FILE
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/resources.qrc")
if(WIN32)
    list(APPEND RESOURCE_FILE "${CMAKE_CURRENT_SOURCE_DIR}/resources/windows.rc")
endif()

add_executable(${PROJECT_NAME} ${SOURCE_FILE} ${RESOURCE_FILE})

# 头文件统一从 app/ 和 src/ 根开始写（"tray.h"、"core/controller.h"），
# 不必关心文件实际放在哪一层子目录
target_include_directories(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/app"
    "${CMAKE_CURRENT_SOURCE_DIR}/src")

# 把工程版本注入到代码里（PTU_APP_VERSION="1.0.0"），
# 免得 UA、日志里的版本号和 CMakeLists 里的 APP_VERSION 各写一份、慢慢对不上
target_compile_definitions(${PROJECT_NAME} PRIVATE
    PTU_APP_VERSION="${APP_VERSION}")

# ---------------------------- 5. QML 模块 ----------------------------------
# 界面全部编进 exe 的 qrc（:/mainui/qml/main.qml），运行时不依赖散装的 .qml 文件。
# 新增 QML 文件要加进下面的列表。
if(QT_VERSION VERSION_GREATER_EQUAL 6.2)
    qt_add_qml_module(${PROJECT_NAME}
        URI mainui
        VERSION 1.0
        QML_FILES
            qml/main.qml
            qml/Toast.qml
            qml/ToastCenter.qml
            qml/SettingsPage.qml
            qml/TestPage.qml
    )
endif()

# ---------------------------- 6. 链接库 ------------------------------------
target_link_libraries(${PROJECT_NAME}
    PUBLIC
        Qt::Quick
        Qt::Core
        Qt::Widgets
        Qt::Network
        Qt::QuickControls2
)

# 管理员权限检测 OpenProcessToken / GetTokenInformation（advapi32）；
# 阻止自动弹出浏览器后用 InternetSetOption 通知系统重读网络设置（wininet）；
# 提权小进程用 ShellExecuteEx(runas) 拉起（shell32）；
# 查默认路由所在网卡用 GetIpForwardTable（iphlpapi，见 src/net/defaultroute.cpp）
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE advapi32 wininet shell32 iphlpapi)
endif()

if(Qt6_FOUND)
    qt_import_qml_plugins(${PROJECT_NAME})
endif()

# ---------------------------- 7. 输出目录 ----------------------------------
# 运行文件（exe、pdb、依赖 DLL、tessdata）统一输出到源码根目录的 build/debug、build/release，
# 编译中间文件仍然留在 out/build/ 下（由 CMakeUserPresets.json 的 binaryDir 决定）
set(APP_OUTPUT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/build")

set_target_properties(${PROJECT_NAME} PROPERTIES
    WIN32_EXECUTABLE TRUE
    # 未单独列出的其它配置（如 RelWithDebInfo）落到 build/<配置名>
    RUNTIME_OUTPUT_DIRECTORY "${APP_OUTPUT_ROOT}/$<CONFIG>"
    PDB_OUTPUT_DIRECTORY     "${APP_OUTPUT_ROOT}/$<CONFIG>"
)

# 单配置生成器（Ninja / NMake）按 CMAKE_BUILD_TYPE 取下面这些配置项；
# pdb 跟 exe 放一起，断点 / 调用堆栈才找得到符号
foreach(config IN ITEMS DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    if(config STREQUAL "DEBUG")
        set(config_dir "${APP_OUTPUT_ROOT}/Debug")
    else()
        set(config_dir "${APP_OUTPUT_ROOT}/Release")
    endif()

    set_target_properties(${PROJECT_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${config} "${config_dir}"
        PDB_OUTPUT_DIRECTORY_${config}     "${config_dir}")
endforeach()

message(STATUS "Program output directory: ${APP_OUTPUT_ROOT}/<config>")

# ---------------------------- 8. 可选功能 ----------------------------------
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/tesseract.cmake")
ptu_enable_tesseract(${PROJECT_NAME})
