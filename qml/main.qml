import QtQuick
import QtQuick.Controls
import QtQuick.Window

ApplicationWindow {
    id: root
    // 是否显示由 C++ 决定：开机自启动时保持隐藏，只弹 Toast
    visible: false
    width: 1040
    height: 740
    minimumWidth: 900
    minimumHeight: 720
    title: "莆田学院校园网自动登录"

    // ---------------- 主题配色 ----------------
    // 具体颜色由 C++ 的 SystemTheme 提供，这里只留一层同名别名，
    // 各页面读同一份，系统切换深浅色时所有绑定自动跟着变
    readonly property bool dark: systemTheme.dark
    readonly property color bgColor: systemTheme.bgColor
    readonly property color sidebarColor: systemTheme.sidebarColor
    readonly property color cardColor: systemTheme.cardColor
    readonly property color textColor: systemTheme.textColor
    readonly property color subTextColor: systemTheme.subTextColor
    readonly property color accentColor: systemTheme.accentColor
    readonly property color accentSoft: systemTheme.accentSoft
    readonly property color hoverSoft: systemTheme.hoverSoft

    // 0 = 主页，1 = 设置
    property int currentPage: 0
    // 启动后的第一次联网检测是否已经处理过
    property bool startupOnlineChecked: false

    color: root.bgColor

    // 网络状态由 C++ 的 Network 每 30 秒后台检测一次，开机自启动的自动登录也由 C++ 拉起，
    // QML 这里只负责显示
    // ---------------- 登录状态文案 / 配色 ----------------
    function loginStateText(state) {
        if (state === 1)
            return "登录中";
        if (state === 2)
            return "登录成功";
        if (state === 3)
            return "登录失败";
        return "未登录";
    }

    function loginStateColor(state) {
        if (state === 2)
            return "#FF107C10";
        if (state === 3)
            return "#FFC42B1C";
        if (state === 1)
            return root.accentColor;
        return root.subTextColor;
    }

    function loginStateBg(state) {
        if (state === 2)
            return "#26107C10";
        if (state === 3)
            return "#26C42B1C";
        if (state === 1)
            return root.accentSoft;
        return root.hoverSoft;
    }

    function loginStateHint(state) {
        if (state === 2)
            return "已通过校园网认证";
        if (state === 3)
            return "认证失败，请检查账号密码后重试";
        if (state === 1)
            return "正在提交认证请求";
        return "尚未完成校园网认证";
    }

    // ---------------- 临时预览 toast ----------------
    property var toastComponent: null
    property var toastWindow: null

    function showToast(status) {
        if (!toastComponent) {
            toastComponent = Qt.createComponent(Qt.resolvedUrl("Toast.qml"));
        }
        if (toastComponent.status !== Component.Ready) {
            console.warn("Toast.qml 加载失败:", toastComponent.errorString());
            return;
        }

        if (!toastWindow) {
            toastWindow = toastComponent.createObject(null);
            // toast 是独立窗口，自己销毁会留下悬空引用，交给 root 统一关闭
            if (toastWindow) {
                toastWindow.closeRequested.connect(root.hideToast);
                // 退出动画播完后 toast 会发 dismissed，这时才真正销毁
                toastWindow.dismissed.connect(root.destroyToast);
            }
        }
        if (toastWindow) {
            // present() 负责刷新状态、show()、raise() 和入场动画
            toastWindow.present(status);
            // 成功的提示短一些，失败/处理中留足时间
            hideTimer.interval = status === "success" ? 3500 : 7000;
            hideTimer.restart();
        }
    }

    // 开始隐藏：先播右滑退出动画，动画结束后由 destroyToast() 销毁
    function hideToast() {
        hideTimer.stop();
        if (toastWindow)
            toastWindow.dismiss();
    }

    function destroyToast() {
        hideTimer.stop();
        if (toastWindow) {
            toastWindow.close();
            toastWindow.destroy();
            toastWindow = null;
        }
    }

    Timer {
        id: hideTimer
        interval: 10000
        onTriggered: root.hideToast()
    }

    // 自动登录时联动 toast：处理中 / 失败 / 成功后收起。
    // 后台自动重连也算一次登录操作，同样会弹 Toast
    Connections {
        target: portal
        function onStateChanged() {
            if (portal.state === 1) {
                root.showToast("processing");
            } else if (portal.state === 8) {
                root.showToast("failed");
            } else if (portal.state === 7) {
                root.showToast("success");
            } else if (portal.state === 0) {
                root.hideToast();
            }
        }
    }

    // 启动后第一次网络检测：如果本来就已经能上网，说明之前已经登录过（含手动登录）
    Connections {
        target: network
        function onOnlineChecked(online) {
            if (root.startupOnlineChecked)
                return;
            root.startupOnlineChecked = true;

            if (online && !portal.running && controller.loginState === 0)
                portal.detectExistingSession();
        }
    }

    // ---------------- 左侧：导航 + Hero ----------------
    Rectangle {
        id: sidebar
        width: 240
        height: parent.height
        color: root.sidebarColor

        Column {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 18

            // Hero
            Item {
                width: parent.width
                height: 64

                // 侧栏 LOGO（和主页 Hero 用同一张资源图）
                Image {
                    id: logo
                    width: 44
                    height: 44
                    source: "qrc:/app_logo.png"
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                    mipmap: true
                }

                Column {
                    anchors.left: logo.right
                    anchors.leftMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 3

                    Text {
                        text: "莆田学院"
                        color: root.textColor
                        font.pixelSize: 15
                        font.weight: Font.DemiBold
                    }

                    Text {
                        text: "校园网自动登录"
                        color: root.subTextColor
                        font.pixelSize: 11
                    }
                }
            }

            // 导航
            Column {
                width: parent.width
                spacing: 6

                Repeater {
                    model: [
                        { title: "主页" },
                        { title: "设置" },
                        //{ title: "测试" } // 测试页面正式版不用
                    ]

                    delegate: Rectangle {
                        width: parent.width
                        height: 40
                        radius: 6
                        color: root.currentPage === index
                            ? root.accentSoft
                            : (navMouse.containsMouse ? root.hoverSoft : "transparent")

                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: 12
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.title
                            color: root.currentPage === index ? root.accentColor : root.textColor
                            font.pixelSize: 14
                        }

                        MouseArea {
                            id: navMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.currentPage = index
                        }
                    }
                }
            }
        }

        // 底部网络状态
        Item {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: 20
            anchors.rightMargin: 20
            anchors.bottomMargin: 20
            height: 20

            Rectangle {
                x: 0
                y: 6
                width: 8
                height: 8
                radius: 4
                color: network.online ? "#FF107C10" : "#FFC42B1C"
            }

            Text {
                x: 14
                y: 0
                height: 20
                verticalAlignment: Text.AlignVCenter
                text: network.online ? "互联网已连接" : "互联网未连接"
                color: root.subTextColor
                font.pixelSize: 12
            }
        }
    }

    // ---------------- 右侧：各个页面 ----------------
    Rectangle {
        anchors.left: sidebar.right
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        color: root.bgColor

        Item {
            anchors.fill: parent
            anchors.margins: 28

            // ---- 主页 ----
            Item {
                id: homePage
                anchors.fill: parent
                visible: root.currentPage === 0

                Column {
                    width: parent.width
                    spacing: 16

                    Text {
                        text: "主页"
                        color: root.textColor
                        font.pixelSize: 26
                        font.weight: Font.DemiBold
                    }

                    // Hero：应用图标 + 名称
                    Rectangle {
                        width: parent.width
                        height: 148
                        radius: 10
                        color: root.cardColor

                        Item {
                            anchors.fill: parent
                            anchors.margins: 20

                            // 应用图标（资源已打进 qrc:/app_logo.png）
                            Image {
                                id: appIcon
                                width: 92
                                height: 92
                                anchors.verticalCenter: parent.verticalCenter
                                source: "qrc:/app_logo.png"
                                fillMode: Image.PreserveAspectFit
                                smooth: true
                                mipmap: true
                            }

                            Column {
                                anchors.left: appIcon.right
                                anchors.leftMargin: 18
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 6

                                Text {
                                    width: parent.width
                                    text: "莆田学院校园网自动登录"
                                    color: root.textColor
                                    font.pixelSize: 22
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }

                                Text {
                                    width: parent.width
                                    text: "开机自动检测网络状态，快速完成校园网认证"
                                    color: root.subTextColor
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                }
                            }
                        }
                    }

                    // 连接状态
                    Rectangle {
                        width: parent.width
                        height: 236
                        radius: 10
                        color: root.cardColor

                        Column {
                            anchors.fill: parent
                            anchors.margins: 20
                            spacing: 12

                            Text {
                                text: "连接状态"
                                color: root.textColor
                                font.pixelSize: 16
                                font.weight: Font.DemiBold
                            }

                            // 局域网
                            Item {
                                width: parent.width
                                height: 28

                                Text {
                                    width: 220
                                    height: 28
                                    verticalAlignment: Text.AlignVCenter
                                    text: "局域网（WLAN / 有线）"
                                    color: root.textColor
                                    font.pixelSize: 14
                                }

                                Rectangle {
                                    x: 232
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 74
                                    height: 24
                                    radius: 12
                                    color: network.lanConnected ? "#26107C10" : "#26C42B1C"

                                    Text {
                                        anchors.centerIn: parent
                                        text: network.lanConnected ? "已连接" : "未连接"
                                        color: network.lanConnected ? "#FF107C10" : "#FFC42B1C"
                                        font.pixelSize: 12
                                    }
                                }

                                Text {
                                    x: 320
                                    width: parent.width - x
                                    height: 28
                                    verticalAlignment: Text.AlignVCenter
                                    text: network.lanDescription.length > 0
                                        ? network.lanDescription
                                        : "未发现活动网卡"
                                    color: root.subTextColor
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                }
                            }

                            // 互联网
                            Item {
                                width: parent.width
                                height: 28

                                Text {
                                    width: 220
                                    height: 28
                                    verticalAlignment: Text.AlignVCenter
                                    text: "互联网"
                                    color: root.textColor
                                    font.pixelSize: 14
                                }

                                Rectangle {
                                    x: 232
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 74
                                    height: 24
                                    radius: 12
                                    color: network.online ? "#26107C10" : "#26C42B1C"

                                    Text {
                                        anchors.centerIn: parent
                                        text: network.online ? "可访问" : "不可访问"
                                        color: network.online ? "#FF107C10" : "#FFC42B1C"
                                        font.pixelSize: 12
                                    }
                                }
                            }

                            // 校园网登录状态
                            Item {
                                width: parent.width
                                height: 28

                                Text {
                                    width: 220
                                    height: 28
                                    verticalAlignment: Text.AlignVCenter
                                    text: "校园网登录状态"
                                    color: root.textColor
                                    font.pixelSize: 14
                                }

                                Rectangle {
                                    x: 232
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 80
                                    height: 24
                                    radius: 12
                                    color: root.loginStateBg(controller.loginState)

                                    Text {
                                        anchors.centerIn: parent
                                        text: root.loginStateText(controller.loginState)
                                        color: root.loginStateColor(controller.loginState)
                                        font.pixelSize: 12
                                    }
                                }

                                Text {
                                    x: 326
                                    width: parent.width - x
                                    height: 28
                                    verticalAlignment: Text.AlignVCenter
                                    text: portal.message.length > 0
                                        ? portal.message
                                        : root.loginStateHint(controller.loginState)
                                    color: root.subTextColor
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                }
                            }

                            // 操作
                            Item {
                                width: parent.width
                                height: 32

                                Row {
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 10

                                    Button {
                                        text: "重新检测"
                                        font.family: appFontFamily
                                        onClicked: network.refresh()
                                    }
                                }
                            }
                        }
                    }

                    // 当前账号
                    Rectangle {
                        width: parent.width
                        height: 160
                        radius: 10
                        color: root.cardColor

                        Column {
                            anchors.fill: parent
                            anchors.margins: 20
                            spacing: 8

                            Text {
                                text: "当前账号"
                                color: root.subTextColor
                                font.pixelSize: 12
                            }

                            Text {
                                text: controller.username.length > 0 ? controller.username : "尚未设置"
                                color: root.textColor
                                font.pixelSize: 18
                                font.weight: Font.DemiBold
                            }

                            Text {
                                text: "在「设置」中填写账号密码；若已在浏览器完成认证，点「已手动登录」"
                                color: root.subTextColor
                                font.pixelSize: 11
                            }

                            Row {
                                spacing: 10

                                Button {
                                    text: portal.running ? "停止" : "登录"
                                    font.family: appFontFamily
                                    onClicked: {
                                        if (portal.running)
                                            portal.stop();
                                        else
                                            portal.start();
                                    }
                                }

                                Button {
                                    text: "注销"
                                    font.family: appFontFamily
                                    enabled: !portal.running
                                    onClicked: logoutDialog.open()
                                }

                                Button {
                                    text: "已手动登录"
                                    font.family: appFontFamily
                                    enabled: !portal.running && controller.loginState !== 2
                                    onClicked: portal.markManuallyLoggedIn()
                                }
                            }
                        }
                    }
                }
            }

            // ---- 设置 ----
            SettingsPage {
                anchors.fill: parent
                visible: root.currentPage === 1
            }

            // ---- 测试 ----
            TestPage {
                anchors.fill: parent
                visible: root.currentPage === 2
                onShowToastRequested: function (status) { root.showToast(status); }
            }
        }
    }

    // ---------------- 注销前确认 ----------------
    Dialog {
        id: logoutDialog
        anchors.centerIn: parent
        modal: true
        title: "注销校园网登录"
        width: 380
        padding: 10
        closePolicy: Popup.CloseOnEscape

        header: Label {
            text: logoutDialog.title
            font.family: appFontFamily
            font.bold: true
            font.pixelSize: 20
            font.weight: Font.DemiBold
            topPadding: logoutDialog.padding
            leftPadding: logoutDialog.padding
            rightPadding: logoutDialog.padding
        }

        background: Rectangle {
            color: root.cardColor
            radius: 10
            border.width: 0
        }

        contentItem: Column {
            width: 320
            spacing: 14

            Text {
                width: parent.width
                //text: "注销后当前设备会断开校园网认证，需要重新登录才能上网。确定要注销吗？"
                text: "注销后当前设备会断开校园网。由于技术问题，需要你手动前往浏览器窗口注销。请手动注销"
                color: root.textColor
                font.pixelSize: 13
                wrapMode: Text.WordWrap
            }

            
        }

        footer: Row {
                spacing: 20

                Button {
                    text: "取消"
                    font.family: appFontFamily
                    onClicked: logoutDialog.close()
                }

                Button {
                    text: "确定"
                    font.family: appFontFamily
                    onClicked: {
                        logoutDialog.close();
                        portal.logout();
                    }
                }
            }
    }
}
