import QtQuick

/// Toast 的显示逻辑：创建 / 复用 / 销毁独立 Toast 窗口，并管理自动隐藏计时。
/// main.qml 只需要 center.show(status) 和 center.hide()，
/// 窗口生命周期、计时、重复弹出都在这里收口。
Item {
    id: center

    // 纯逻辑对象，不参与布局也不绘制
    visible: false
    width: 0
    height: 0

    /// 成功提示停留短一些，失败 / 处理中留足时间
    readonly property int successDuration: 3500
    readonly property int defaultDuration: 7000

    /// 「处理中」至少露脸这么久才允许被结果覆盖。
    /// 本来就在线时整个流程就是一次往返（几十毫秒），Toast 窗口光是把第一帧
    /// 画出来就要一两帧，不设下限的话结果会赶在窗口绘制前把内容改掉，
    /// 用户看到的就只剩一张「登录成功」卡片，像是流程被跳过了
    readonly property int minProcessingMs: 700

    /// 当前 Toast 窗口；为 null 表示界面上现在没有 toast
    property var toastWindow: null
    /// Toast 组件，第一次显示时才加载
    property var toastComponent: null

    /// 当前 toast 出现的时刻，用于算「处理中」已经露了多久
    property double shownAt: 0
    /// 因为最短展示时长被推迟的展示请求：{ status, message }
    property var deferred: null

    /// 显示一条 Toast；已有 toast 时只更新它，不会重播入场动画。
    /// message 可选：非空时覆盖正文，只给测试页用
    function show(status, message) {
        // 已经有排队的切换：后来的状态更新直接顶掉它，保证最终显示的是最新状态
        if (deferred) {
            deferred = { status: status, message: message };
            return;
        }

        // 「处理中」还没露够时间就要被结果覆盖：先排队，到点再切
        if (toastWindow && toastWindow.status === "processing"
                && status !== "processing" && Date.now() - shownAt < minProcessingMs) {
            deferred = { status: status, message: message };
            deferTimer.interval = Math.max(0, minProcessingMs - (Date.now() - shownAt));
            deferTimer.start();
            return;
        }

        present(status, message);
    }

    /// 真正把内容推给 Toast 窗口：没有窗口就创建，有窗口就只更新内容
    function present(status, message) {
        if (!toastComponent)
            toastComponent = Qt.createComponent(Qt.resolvedUrl("Toast.qml"));

        if (toastComponent.status !== Component.Ready) {
            console.warn("Toast.qml 加载失败:", toastComponent.errorString());
            return;
        }

        if (!toastWindow) {
            toastWindow = toastComponent.createObject(null);
            if (!toastWindow)
                return;

            // toast 是独立窗口，自己销毁会留下悬空引用，所以统一由这里销毁
            toastWindow.closeRequested.connect(center.hide);
            // 退出动画播完后 toast 才发 dismissed，这时才真正销毁
            toastWindow.dismissed.connect(center.destroyWindow);
        }

        // 计时从「处理中」最后一次出现算起：流程中途再次进入 Checking
        // （比如登录后确认上网）时要重新计时，结果才不会被提前顶掉
        if (status === "processing")
            shownAt = Date.now();

        toastWindow.present(status, message);

        hideTimer.interval = status === "success" ? successDuration : defaultDuration;
        hideTimer.restart();
    }

    /// 开始隐藏：先播右滑退出动画，播完由 destroyWindow() 销毁窗口
    function hide() {
        // 用户主动收起 / 流程中止：排队的切换已经没有意义了
        deferTimer.stop();
        deferred = null;
        hideTimer.stop();
        if (toastWindow)
            toastWindow.dismiss();
    }

    /// 退出动画已经播完，可以安全销毁窗口
    function destroyWindow() {
        deferTimer.stop();
        deferred = null;
        shownAt = 0;
        hideTimer.stop();
        if (!toastWindow)
            return;

        toastWindow.close();
        toastWindow.destroy();
        toastWindow = null;
    }

    Timer {
        id: hideTimer
        interval: center.defaultDuration
        onTriggered: center.hide()
    }

    /// 最短展示时长的到期计时器：到点后把排队的展示请求补上
    Timer {
        id: deferTimer
        interval: center.minProcessingMs
        onTriggered: {
            if (!center.deferred)
                return;

            const next = center.deferred;
            center.deferred = null;
            center.present(next.status, next.message);
        }
    }
}