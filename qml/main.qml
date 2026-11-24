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

    color: root.bgColor

    // ---------------- 登录状态配色 ----------------
    // 状态的语义（叫什么、该显示什么文案）全在 C++ 的 PortalLogin 里，
    // QML 只按 statusKind 决定颜色，不再写状态数字
    function loginStateColor(kind) {
        if (kind === "loggedIn")
            return "#FF107C10";
        if (kind === "failed")
            return "#FFC42B1C";
        if (kind === "loggingIn")
            return root.accentColor;
        return root.subTextColor;
    }

    function loginStateBg(kind) {
        if (kind === "loggedIn")
            return "#26107C10";
        if (kind === "failed")
            return "#26C42B1C";
        if (kind === "loggingIn")
            return root.accentSoft;
        return root.hoverSoft;
    }

    function loginStateHint(kind) {
        if (kind === "loggedIn")
            return "已通过校园网认证";
        if (kind === "failed")
            return "认证失败，请检查账号密码后重试";
        if (kind === "loggingIn")
            return "正在提交认证请求";
        return "尚未完成校园网认证";
    }

    // ---------------- Toast ----------------
    // 窗口生命周期和显示逻辑都在 ToastCenter.qml 里；
    // 什么状态该弹什么 Toast 由 C++ 决定，这里只负责显示
    ToastCenter {
        id: toastCenter
    }

    Connections {
        target: portal
        function onToastRequested(kind) {
            if (kind === "hide")
                toastCenter.hide();
            else if (kind.length > 0)
                toastCenter.show(kind);
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
                        {
                            title: "主页"
                        },
                        {
                            title: "设置"
                        },
                        // 发布前想隐藏测试页，把这一行注释掉即可
                        //{
                        //    title: "测试"
                        //}
                    ]

                    delegate: Rectangle {
                        width: parent.width
                        height: 40
                        radius: 6
                        color: root.currentPage === index ? root.accentSoft : (navMouse.containsMouse ? root.hoverSoft : "transparent")

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
                                    text: network.lanDescription.length > 0 ? network.lanDescription : "未发现活动网卡"
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
                                    color: root.loginStateBg(portal.statusKind)

                                    Text {
                                        anchors.centerIn: parent
                                        text: portal.statusText
                                        color: root.loginStateColor(portal.statusKind)
                                        font.pixelSize: 12
                                    }
                                }

                                Text {
                                    x: 326
                                    width: parent.width - x
                                    height: 28
                                    verticalAlignment: Text.AlignVCenter
                                    text: portal.message.length > 0 ? portal.message : root.loginStateHint(portal.statusKind)
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
                                        text: network.checking ? "检测中…" : "重新检测"
                                        font.family: appFontFamily
                                        enabled: !network.checking
                                        onClicked: network.refresh()
                                    }

                                    // 检测中：转个圈，光靠按钮文字看不出还在动
                                    Item {
                                        width: 20
                                        height: 20
                                        visible: network.checking

                                        BusyIndicator {
                                            anchors.fill: parent
                                            running: network.checking
                                        }
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
                                    enabled: !portal.running && portal.statusKind !== "loggedIn"
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
                onShowToastRequested: function (status) {
                    toastCenter.show(status);
                }
                onHideToastRequested: toastCenter.hide()
                onShowToastWithMessageRequested: function (status, message) {
                    toastCenter.show(status, message);
                }
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
