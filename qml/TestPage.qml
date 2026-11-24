import QtQuick
import QtQuick.Controls

/// 测试页：只做 Toast 预览和回归用例，不影响真实登录状态
Item {
    id: testPage

    /// 请求 main.qml 弹出指定状态的 Toast
    signal showToastRequested(string status)
    /// 请求 main.qml 弹出一条带自定义正文的 Toast（验证长文案排版）
    signal showToastWithMessageRequested(string status, string message)
    /// 请求 main.qml 收起当前 Toast
    signal hideToastRequested()

    // 配色统一来自 C++ 的 SystemTheme
    readonly property bool dark: systemTheme.dark
    readonly property color cardColor: systemTheme.cardColor
    readonly property color textColor: systemTheme.textColor
    readonly property color subTextColor: systemTheme.subTextColor
    readonly property color warnColor: dark ? "#FFFFC83D" : "#FF9D5D00"
    readonly property color warnBg: dark ? "#33FFC83D" : "#FFFFF4CE"

    /// 最近跑了哪个用例，方便对照屏幕上的 Toast
    property string lastAction: "还没有执行任何用例"

    // ==================== 用例播放器 ====================

    property var sequenceSteps: []
    property int sequenceIndex: 0

    /// 按顺序播放一串状态，每段间隔 700ms（比入场动画 260ms 长，能看清是
    /// 「原地更新」还是「重新弹入」）。播放前先收起已有 Toast，
    /// 保证这组用例从「界面上没有 toast」开始，首帧的弹出动画才真的会播。
    function playSequence(steps) {
        stopSequence();
        testPage.hideToastRequested();
        testPage.sequenceSteps = steps;
        testPage.sequenceIndex = 0;
        testPage.lastAction = "用例：" + steps.join(" → ");
        sequenceStartTimer.restart();
    }

    /// 只播一次（单状态用例）
    function playOnce(status) {
        stopSequence();
        testPage.lastAction = "单状态：" + status;
        testPage.showToastRequested(status);
    }

    function stopSequence() {
        sequenceStartTimer.stop();
        sequenceTimer.stop();
        testPage.sequenceIndex = 0;
    }

    Component.onDestruction: stopSequence()
    // 切到别的页面时把没跑完的用例停掉，免得在看不见的地方继续弹 toast
    onVisibleChanged: if (!visible) stopSequence()

    // 等退场动画（260ms）播完再开始，避免和收起动作抢动画
    Timer {
        id: sequenceStartTimer
        interval: 420
        onTriggered: sequenceTimer.restart()
    }

    Timer {
        id: sequenceTimer
        interval: 700
        repeat: true
        onTriggered: {
            if (testPage.sequenceIndex >= testPage.sequenceSteps.length) {
                testPage.stopSequence();
                return;
            }
            testPage.showToastRequested(testPage.sequenceSteps[testPage.sequenceIndex]);
            testPage.sequenceIndex += 1;
        }
    }

    // ==================== 界面 ====================

    Flickable {
        anchors.fill: parent
        clip: true
        contentWidth: width
        contentHeight: contentColumn.height + 20
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        Column {
            id: contentColumn
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

            // ---- 单状态：验证三种 Toast 各自的样子 ----
            Rectangle {
                width: parent.width
                height: singleColumn.implicitHeight + 40
                radius: 10
                color: testPage.cardColor

                Column {
                    id: singleColumn
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 12

                    Text {
                        text: "单状态弹出"
                        color: testPage.textColor
                        font.pixelSize: 16
                        font.weight: Font.DemiBold
                    }

                    Text {
                        width: parent.width
                        text: "每点一次都会重新弹出一条 Toast，用来看三种状态的配色、图标和文案。"
                              + "最后一个按钮会塞一段超长原因，用来验证正文换行和省略号。"
                        color: testPage.subTextColor
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }

                    Row {
                        spacing: 10

                        Button {
                            text: "处理中"
                            font.family: appFontFamily
                            onClicked: testPage.playOnce("processing")
                        }

                        Button {
                            text: "登录成功"
                            font.family: appFontFamily
                            onClicked: testPage.playOnce("success")
                        }

                        Button {
                            text: "登录失败"
                            font.family: appFontFamily
                            onClicked: testPage.playOnce("failed")
                        }

                        // 验证正文换行和省略号：这段故意写得比真实提示长
                        Button {
                            text: "失败 + 超长原因"
                            font.family: appFontFamily
                            onClicked: {
                                testPage.stopSequence();
                                testPage.lastAction = "长文案：应当换行显示两行，超出部分以省略号收尾";
                                testPage.showToastWithMessageRequested("failed",
                                    "登录失败：账号或密码错误，请检查后在设置里重新填写；"
                                    + "若确认无误仍无法登录，请前往网络中心办理，"
                                    + "或拨打服务电话 0594-0000000 转 2 号线咨询。");
                            }
                        }
                    }
                }
            }

            // ---- 状态更新：回归用例，已经在显示时不能重播入场动画 ----
            Rectangle {
                width: parent.width
                height: updateColumn.implicitHeight + 40
                radius: 10
                color: testPage.cardColor

                Column {
                    id: updateColumn
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 12

                    Text {
                        text: "状态原地更新"
                        color: testPage.textColor
                        font.pixelSize: 16
                        font.weight: Font.DemiBold
                    }

                    Text {
                        width: parent.width
                        text: "预期：整条 Toast 只在第一次从右侧滑入，之后停在原地，"
                              + "只有内容淡一下、图标弹一下、描边换色，不应该再滑入一次。"
                        color: testPage.subTextColor
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }

                    Row {
                        spacing: 10

                        Button {
                            text: "处理中 → 成功"
                            font.family: appFontFamily
                            onClicked: testPage.playSequence(["processing", "success"])
                        }

                        Button {
                            text: "处理中 → 失败"
                            font.family: appFontFamily
                            onClicked: testPage.playSequence(["processing", "failed"])
                        }

                        Button {
                            text: "成功 → 处理中"
                            font.family: appFontFamily
                            onClicked: testPage.playSequence(["success", "processing"])
                        }
                    }
                }
            }

            // ---- 时序与生命周期 ----
            Rectangle {
                width: parent.width
                height: timingColumn.implicitHeight + 40
                radius: 10
                color: testPage.cardColor

                Column {
                    id: timingColumn
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 12

                    Text {
                        text: "时序与生命周期"
                        color: testPage.textColor
                        font.pixelSize: 16
                        font.weight: Font.DemiBold
                    }

                    Text {
                        width: parent.width
                        text: "连续切换：5 段快速换状态，全程应该只滑入一次。\n"
                              + "重复同状态：状态没变，画面应该完全不动。\n"
                              + "收起：播右滑退出动画，播完窗口销毁；再点单状态可以重新弹出。"
                        color: testPage.subTextColor
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }

                    Row {
                        spacing: 10

                        Button {
                            text: "连续切换 5 段"
                            font.family: appFontFamily
                            onClicked: testPage.playSequence(
                                ["processing", "success", "processing", "failed", "processing"])
                        }

                        Button {
                            text: "重复同状态"
                            font.family: appFontFamily
                            onClicked: testPage.playSequence(["success", "success", "success"])
                        }

                        Button {
                            text: "收起"
                            font.family: appFontFamily
                            onClicked: {
                                testPage.stopSequence();
                                testPage.lastAction = "已收起，等自动隐藏计时或点单状态重新弹出";
                                testPage.hideToastRequested();
                            }
                        }
                    }
                }
            }

            // ---- 最近执行了什么 ----
            Text {
                width: parent.width
                text: "最近用例：" + testPage.lastAction
                color: testPage.subTextColor
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
        }
    }
}