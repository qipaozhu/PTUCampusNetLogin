import QtQuick
import QtQuick.Controls
import QtQuick.Window

ApplicationWindow {
    id: toast

    /// processing = 正在自动处理，success = 登录成功，failed = 无法登录、需要手动操作
    property string status: "processing"
    readonly property bool failed: status === "failed"
    readonly property bool success: status === "success"

    // 接近 Windows 通知卡片的大小
    readonly property int cardWidth: 380
    // 高度跟着正文行数走：正文最多两行，短消息不会白占一大块
    readonly property int cardHeight: Math.max(104, textColumn.implicitHeight + 32)

    // 窗口比卡片宽出 marginRight：卡片向右滑出时正好消失在屏幕右边缘
    width: cardWidth + marginRight
    height: cardHeight

    // 无边框、置顶、不抢焦点、不显示在任务栏
    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Tool | Qt.WindowDoesNotAcceptFocus
    color: "transparent"
    background: null
    title: "莆田学院校园网自动登录"

    // 右下角留白
    property int marginRight: 24
    property int marginBottom: 72

    // 跟随系统主题：中性色和正文色统一从 SystemTheme 取，
    // 这里只保留 Toast 自己的半透明卡片色和状态色
    readonly property bool dark: systemTheme.dark
    readonly property color cardColor: dark ? "#F2202020" : "#F2F9F9F9"
    readonly property color titleColor: systemTheme.textColor
    readonly property color contentColor: systemTheme.subTextColor
    readonly property color iconColor: dark ? "#26FFFFFF" : "#14000000"
    readonly property color spinnerTrack: dark ? "#33FFFFFF" : "#22000000"
    readonly property color hoverSoft: systemTheme.hoverSoft

    // 用户点了关闭按钮；真正的销毁交给 ToastCenter
    signal closeRequested()
    // 退出动画播完了，ToastCenter 可以安全销毁这个窗口
    signal dismissed()

    // 已经在播放退出动画，避免重复触发
    property bool dismissing: false

    // 处理中为蓝色，失败为红色，成功为绿色
    readonly property color failAccent: dark ? "#FFFF99A4" : "#FFC42B1C"
    readonly property color successAccent: dark ? "#FF6CCB5F" : "#FF107C10"
    readonly property color accent: failed
        ? failAccent
        : (success ? successAccent : systemTheme.accentColor)
    // 深色主题下绿色偏亮，对勾用深色才看得清
    readonly property color checkColor: dark ? "#FF0B1F0A" : "#FFFFFFFF"

    // 卡片描边：跟着状态色走（处理中蓝 / 失败红 / 成功绿），和背景色区分开
    readonly property color borderColor: Qt.rgba(accent.r, accent.g, accent.b, 0.6)
    readonly property real borderWidth: 1.5

    x: Screen.width - width
    y: Screen.height - height - marginBottom

    // 窗口是否已经处于显示状态：决定这一次要不要播入场动画
    property bool shown: false

    /// 非空时覆盖正文：测试页用它塞超长文案进来验证两行排版
    property string messageOverride: ""

    // 由 ToastCenter 调用。界面上本来就有 toast 时只更新内容，
    // 不再播入场动画——否则「处理中 → 成功」会再弹一次，看起来像闪了一下
    function present(newStatus, message) {
        const firstTime = !shown;
        const statusChanged = toast.status !== newStatus;

        exitAnim.stop();
        dismissing = false;
        toast.status = newStatus;
        toast.messageOverride = (message === undefined || message === null) ? "" : message;

        if (firstTime) {
            // 先摆到入场动画的起点，避免窗口出现的第一帧闪一下
            card.x = 40;
            card.opacity = 0;
            shown = true;
            show();
            raise();
            enterAnim.restart();
            return;
        }

        // 已经显示：保持当前位置，只让内容淡一下表示更新
        raise();
        if (statusChanged && !enterAnim.running)
            contentFade.restart();
    }

    // 由 ToastCenter 调用：右滑退出，结束后发 dismissed 让它销毁窗口
    function dismiss() {
        if (dismissing)
            return;

        dismissing = true;
        shown = false;
        enterAnim.stop();
        exitAnim.restart();
    }

    // 入场：从屏幕右侧轻轻滑入并淡入
    ParallelAnimation {
        id: enterAnim

        NumberAnimation {
            target: card
            property: "x"
            to: 0
            duration: 260
            easing.type: Easing.OutCubic
        }

        NumberAnimation {
            target: card
            property: "opacity"
            to: 1
            duration: 200
            easing.type: Easing.OutCubic
        }
    }

    // 退出：向右滑出屏幕并淡出
    ParallelAnimation {
        id: exitAnim

        NumberAnimation {
            target: card
            property: "x"
            to: toast.width
            duration: 260
            easing.type: Easing.InCubic
        }

        NumberAnimation {
            target: card
            property: "opacity"
            to: 0
            duration: 220
            easing.type: Easing.InCubic
        }

        onFinished: toast.dismissed()
    }

    // 失败 / 成功时图标弹一下
    SequentialAnimation {
        id: iconPop

        NumberAnimation {
            target: iconPlaceholder
            property: "scale"
            from: 0.55
            to: 1.0
            duration: 340
            easing.type: Easing.OutBack
        }
    }

    // 已经显示时状态变化：内容轻轻淡一下，代替重播入场动画
    SequentialAnimation {
        id: contentFade

        NumberAnimation {
            target: contentRoot
            property: "opacity"
            to: 0.35
            duration: 90
            easing.type: Easing.OutQuad
        }

        NumberAnimation {
            target: contentRoot
            property: "opacity"
            to: 1.0
            duration: 160
            easing.type: Easing.OutQuad
        }
    }

    onDarkChanged: {
        spinner.requestPaint();
        checkMark.requestPaint();
    }
    onFailedChanged: {
        spinner.requestPaint();
        if (failed)
            iconPop.restart();
    }
    onSuccessChanged: {
        spinner.requestPaint();
        if (success)
            iconPop.restart();
    }

    // 整张卡片：入场 / 退出动画作用在它身上，窗口本身不动，滑动更顺
    Item {
        id: card
        width: toast.cardWidth
        height: toast.cardHeight
        x: 0
        y: 0
        opacity: 0

        // 圆角背景 + 状态色描边
        Rectangle {
            anchors.fill: parent
            radius: 8
            color: toast.cardColor
            border.width: toast.borderWidth
            border.color: toast.borderColor

            // 状态切换时描边颜色平滑过渡
            Behavior on border.color {
                ColorAnimation { duration: 180 }
            }
        }

        Item {
            id: contentRoot
            anchors.fill: parent
            anchors.margins: 16
            // 退出动画期间不再响应点击
            enabled: !toast.dismissing

            // 左侧图标区：处理中转圈、失败感叹号、成功对勾
            Rectangle {
                id: iconPlaceholder
                width: 48
                height: 48
                radius: 10
                anchors.verticalCenter: parent.verticalCenter
                color: toast.failed
                    ? (toast.dark ? "#26FF99A4" : "#1AC42B1C")
                    : toast.success
                        ? (toast.dark ? "#266CCB5F" : "#1A107C10")
                        : toast.iconColor

                // 处理中的加载转圈
                Canvas {
                    id: spinner
                    width: 26
                    height: 26
                    anchors.centerIn: parent
                    visible: !toast.failed && !toast.success

                    onPaint: {
                        var ctx = getContext("2d");
                        var c = width / 2;
                        var r = width / 2 - 2;

                        ctx.clearRect(0, 0, width, height);

                        ctx.lineWidth = 2.5;
                        ctx.lineCap = "round";

                        ctx.strokeStyle = toast.spinnerTrack;
                        ctx.beginPath();
                        ctx.arc(c, c, r, 0, Math.PI * 2, false);
                        ctx.stroke();

                        ctx.strokeStyle = toast.accent;
                        ctx.beginPath();
                        ctx.arc(c, c, r, -Math.PI / 2, Math.PI * 0.9, false);
                        ctx.stroke();
                    }

                    RotationAnimator on rotation {
                        from: 0
                        to: 360
                        duration: 900
                        loops: Animation.Infinite
                        running: !toast.failed && !toast.success
                    }
                }

                // 失败图标
                Rectangle {
                    width: 26
                    height: 26
                    radius: 13
                    anchors.centerIn: parent
                    visible: toast.failed
                    color: toast.accent

                    Text {
                        anchors.centerIn: parent
                        text: "!"
                        color: toast.checkColor
                        font.pixelSize: 16
                        font.weight: Font.Bold
                    }
                }

                // 成功图标：绿色圆 + 白色对勾
                Rectangle {
                    width: 26
                    height: 26
                    radius: 13
                    anchors.centerIn: parent
                    visible: toast.success
                    color: toast.accent

                    Canvas {
                        id: checkMark
                        anchors.fill: parent
                        visible: toast.success

                        onPaint: {
                            var ctx = getContext("2d");
                            var w = width;
                            var h = height;

                            ctx.clearRect(0, 0, w, h);
                            ctx.strokeStyle = toast.checkColor;
                            ctx.lineWidth = 2.6;
                            ctx.lineCap = "round";
                            ctx.lineJoin = "round";

                            ctx.beginPath();
                            ctx.moveTo(w * 0.26, h * 0.53);
                            ctx.lineTo(w * 0.43, h * 0.70);
                            ctx.lineTo(w * 0.75, h * 0.33);
                            ctx.stroke();
                        }

                        onVisibleChanged: if (visible) requestPaint()
                    }
                }
            }

            Column {
                id: textColumn
                anchors.left: iconPlaceholder.right
                anchors.leftMargin: 14
                // 右侧给关闭按钮留出位置，避免文字被按钮压住
                anchors.right: closeButton.left
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                spacing: 6

                // 标题：靠左上
                Text {
                    width: parent.width
                    text: toast.failed
                        ? "无法自动登录网络"
                        : toast.success
                            ? "校园网登录成功"
                            : "正在自动处理中"
                    color: toast.titleColor
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }

                // 内容 / 状态：失败时直接显示登录流程给出的原因
                // （没连网、验证码识别失败、非校园网……）
                // 最多两行：再长就省略号收尾，卡片高度会自动跟上
                Text {
                    width: parent.width
                    text: {
                        if (toast.messageOverride.length > 0)
                            return toast.messageOverride;

                        if (toast.failed) {
                            if (typeof portal !== "undefined" && portal.message.length > 0)
                                return portal.message;
                            return "请打开浏览器完成认证";
                        }

                        return toast.success ? "认证完成，已可正常上网" : "正在登录莆田学院校园网";
                    }
                    color: toast.contentColor
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    lineHeight: 1.15
                }
            }

            // 成功/失败后的手动关闭按钮（处理中不显示）
            Rectangle {
                id: closeButton
                width: 24
                height: 24
                radius: 12
                anchors.right: parent.right
                anchors.top: parent.top
                visible: toast.failed || toast.success
                color: closeMouse.containsMouse ? toast.hoverSoft : "transparent"

                Text {
                    anchors.centerIn: parent
                    text: "\u2715"
                    color: toast.contentColor
                    font.pixelSize: 11
                }

                MouseArea {
                    id: closeMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: toast.closeRequested()
                }
            }
        }
    }
}
