import QtQuick
import QtQuick.Controls

/// 测试页：只做 Toast 预览，不影响真实登录状态
Item {
    id: testPage

    /// 请求 main.qml 弹出指定状态的 Toast
    signal showToastRequested(string status)

    // 配色统一来自 C++ 的 SystemTheme
    readonly property bool dark: systemTheme.dark
    readonly property color cardColor: systemTheme.cardColor
    readonly property color textColor: systemTheme.textColor
    readonly property color subTextColor: systemTheme.subTextColor
    readonly property color warnColor: dark ? "#FFFFC83D" : "#FF9D5D00"
    readonly property color warnBg: dark ? "#33FFC83D" : "#FFFFF4CE"

    Column {
        width: parent.width
        spacing: 16

        Text {
            text: "测试"
            color: testPage.textColor
            font.pixelSize: 26
            font.weight: Font.DemiBold
        }

        // 醒目提示：这是测试页
        Rectangle {
            width: parent.width
            height: 84
            radius: 10
            color: testPage.warnBg

            Item {
                anchors.fill: parent
                anchors.margins: 16

                Rectangle {
                    id: warnBadge
                    width: 24
                    height: 24
                    radius: 12
                    anchors.verticalCenter: parent.verticalCenter
                    color: testPage.warnColor

                    Text {
                        anchors.centerIn: parent
                        text: "!"
                        color: testPage.dark ? "#FF1E1E1E" : "#FFFFFFFF"
                        font.pixelSize: 15
                        font.weight: Font.Bold
                    }
                }

                Column {
                    anchors.left: warnBadge.right
                    anchors.leftMargin: 12
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 4

                    Text {
                        width: parent.width
                        text: "这是测试选项，正常情况请勿动"
                        color: testPage.warnColor
                        font.pixelSize: 15
                        font.weight: Font.DemiBold
                        wrapMode: Text.WordWrap
                    }

                    Text {
                        width: parent.width
                        text: "下面的按钮只会弹出对应的提示，不会真的登录，也不会修改配置。"
                        color: testPage.subTextColor
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }

        // Toast 预览
        Rectangle {
            width: parent.width
            height: 196
            radius: 10
            color: testPage.cardColor

            Column {
                anchors.fill: parent
                anchors.margins: 20
                spacing: 12

                Text {
                    text: "Toast 提示"
                    color: testPage.textColor
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                }

                Text {
                    width: parent.width
                    text: "点击下面三个按钮，分别在右下角显示「处理中 / 登录成功 / 登录失败」三种状态。"
                    color: testPage.subTextColor
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }

                Row {
                    spacing: 10

                    Button {
                        text: "处理中"
                        font.family: appFontFamily
                        onClicked: testPage.showToastRequested("processing")
                    }

                    Button {
                        text: "登录成功"
                        font.family: appFontFamily
                        onClicked: testPage.showToastRequested("success")
                    }

                    Button {
                        text: "登录失败"
                        font.family: appFontFamily
                        onClicked: testPage.showToastRequested("failed")
                    }
                }
            }
        }
    }
}
