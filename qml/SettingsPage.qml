import QtQuick
import QtQuick.Controls

Item {
    id: settingsPage

    // 配色统一来自 C++ 的 SystemTheme（和主页、Toast 共用一份）
    readonly property bool dark: systemTheme.dark
    /// 密码框是否显示明文
    property bool showPassword: false
    readonly property color cardColor: systemTheme.cardColor
    readonly property color textColor: systemTheme.textColor
    readonly property color subTextColor: systemTheme.subTextColor

    /// 校验并应用 Portal 地址，格式不合法时弹窗提示
    function applyPortalAddress(text) {
        if (controller.applyPortalAddress(text)) {
            portalField.text = controller.portalAddress;
            return true;
        }
        portalDialog.open();
        return false;
    }

    /// 按当前地址选中下拉框里对应的运营商，识别不出就落到「自定义」
    function syncPortalPreset() {
        for (var i = 0; i < portalPresets.count; ++i) {
            var address = portalPresets.get(i).address;
            if (address.length > 0 && address === controller.portalAddress) {
                portalCombo.currentIndex = i;
                return;
            }
        }
        portalCombo.currentIndex = portalPresets.count - 1;
    }

    // 设置项变多后固定高度装不下，这里做纵向滚动
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
                text: "设置"
                color: settingsPage.textColor
                font.pixelSize: 26
                font.weight: Font.DemiBold
            }

            // 校园网账号
            Rectangle {
                width: parent.width
                height: accountColumn.implicitHeight + 40
                radius: 10
                color: settingsPage.cardColor

                Column {
                    id: accountColumn
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.margins: 20
                    spacing: 9

                    Text {
                        text: "账号配置"
                        font.pixelSize: 16
                        font.bold: true
                    }

                    Text {
                        text: "账号"
                        color: settingsPage.subTextColor
                        font.pixelSize: 12
                    }

                    TextField {
                        id: usernameField
                        width: parent.width
                        placeholderText: "请输入校园网账号（学号、工号）"
                        text: controller.username
                        font.family: appFontFamily
                        onTextChanged: {
                            if (controller.username !== text)
                                controller.username = text;
                        }
                    }

                    Text {
                        text: "密码"
                        color: settingsPage.subTextColor
                        font.pixelSize: 12
                    }

                    Row {
                        width: parent.width
                        spacing: 8

                        TextField {
                            id: passwordField
                            width: parent.width - revealButton.width - 8
                            placeholderText: "请输入密码：（学生默认密码 身份证后六位）"
                            // 勾选“显示”后切换成明文
                            echoMode: settingsPage.showPassword
                                ? TextInput.Normal
                                : TextInput.Password
                            text: controller.password
                            font.family: appFontFamily
                            onTextChanged: {
                                if (controller.password !== text)
                                    controller.password = text;
                            }
                        }

                        Button {
                            id: revealButton
                            width: 64
                            height: passwordField.height
                            text: settingsPage.showPassword ? "隐藏" : "显示"
                            font.family: appFontFamily
                            onClicked: settingsPage.showPassword = !settingsPage.showPassword
                        }
                    }

                    Item {
                        width: 1
                        height: 4
                    }

                    Row {
                        spacing: 10

                        Button {
                            text: "保存设置"
                            font.family: appFontFamily
                            onClicked: {
                                // Portal 地址格式不对就不允许保存
                                if (!settingsPage.applyPortalAddress(portalField.text))
                                    return;

                                if (controller.save()) {
                                    saveStatus.text = "已保存到本地配置";
                                    saveStatus.color = "#FF107C10";
                                } else {
                                    saveStatus.text = "保存失败，请检查目录权限";
                                    saveStatus.color = "#FFC42B1C";
                                }
                            }
                        }

                        Button {
                            text: "清空"
                            font.family: appFontFamily
                            onClicked: {
                                controller.username = "";
                                controller.password = "";
                                usernameField.text = "";
                                passwordField.text = "";
                                saveStatus.text = "";
                            }
                        }

                        Text {
                            id: saveStatus
                            text: ""
                            color: "#FF107C10"
                            font.pixelSize: 12
                        }
                    }

                    Text {
                        width: parent.width
                        text: "配置文件目录：" + controller.configPath
                        color: settingsPage.subTextColor
                        font.pixelSize: 11
                        elide: Text.ElideMiddle
                    }

                    Text {
                        width: parent.width
                        text: "！！注意！！ 一定要确保你有办理校园网套餐。校园卡套餐不包含校园网套餐。否则会出现重复登录"
                        color: settingsPage.subTextColor
                        font.pixelSize: 11
                        elide: Text.ElideMiddle
                    }
                }
            }

            // Portal 地址：用下拉框选运营商预设，选完自动填进输入框
            Rectangle {
                width: parent.width
                height: portalColumn.implicitHeight + 40
                radius: 10
                color: settingsPage.cardColor

                ListModel {
                    id: portalPresets
                    ListElement { name: "移动校园网 （中区、东区）"; address: "192.168.116.8:801" }
                    ListElement { name: "电信校园网 （西区）"; address: "192.168.111.8:801" }
                    ListElement { name: "自定义"; address: "" }
                }

                Column {
                    id: portalColumn
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.margins: 20
                    spacing: 10

                    Text {
                        text: "校园网 Portal 地址配置"
                        font.bold: true
                        font.pixelSize: 16
                    }

                    ComboBox {
                        id: portalCombo
                        width: parent.width
                        model: portalPresets
                        textRole: "name"
                        font.family: appFontFamily
                        // 下拉列表的每一项也是控件，单独指定字体，否则会退回宋体
                        delegate: ItemDelegate {
                            required property string name
                            required property int index

                            width: ListView.view ? ListView.view.width : 0
                            text: name
                            font.family: appFontFamily
                            highlighted: portalCombo.highlightedIndex === index
                        }
                        // 用户选择预设后自动填入输入框，仍需点“应用”才生效
                        onActivated: function (index) {
                            var address = portalPresets.get(index).address;
                            if (address.length > 0)
                                portalField.text = address;
                            else
                                portalField.forceActiveFocus();
                        }
                    }

                    TextField {
                        id: portalField
                        width: parent.width
                        placeholderText: "通过浏览器抓包的ePortal地址 （例如：192.168.116.8:801）"
                        text: controller.portalAddress
                        font.family: appFontFamily
                    }

                    Button {
                        text: "应用"
                        font.family: appFontFamily
                        onClicked: settingsPage.applyPortalAddress(portalField.text)
                    }
                }

                // 地址变化时同步输入框和下拉框（例如从配置文件读取、或应用了自定义地址）
                Connections {
                    target: controller
                    function onPortalAddressChanged() {
                        if (portalField.text !== controller.portalAddress)
                            portalField.text = controller.portalAddress;
                        settingsPage.syncPortalPreset();
                    }
                }

                Component.onCompleted: settingsPage.syncPortalPreset()
            }

            // 启动与系统设置
            Rectangle {
                width: parent.width
                height: systemColumn.implicitHeight + 40
                radius: 10
                color: settingsPage.cardColor

                Column {
                    id: systemColumn
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.margins: 20
                    spacing: 14

                    Text {
                        text: "系统设置"
                        font.pixelSize: 16
                        font.bold: true
                    }

                    // 开机自启动
                    Item {
                        width: parent.width
                        height: 64

                        Column {
                            anchors.left: parent.left
                            anchors.right: autoStartRow.left
                            anchors.rightMargin: 16
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 5

                            Text {
                                text: "开机自启动"
                                color: settingsPage.textColor
                                font.pixelSize: 14
                            }

                            Text {
                                width: parent.width
                                text: "登录系统后自动启动本程序，并立即执行一次校园网认证"
                                color: settingsPage.subTextColor
                                font.pixelSize: 11
                                wrapMode: Text.WordWrap
                            }
                        }

                        Row {
                            id: autoStartRow
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 8

                            Item {
                                width: 18
                                height: 18
                                visible: controller.autoStartBusy

                                BusyIndicator {
                                    anchors.fill: parent
                                    running: controller.autoStartBusy
                                }
                            }

                            Switch {
                                id: autoStartSwitch
                                checked: controller.autoStart
                                // 写注册表在后台线程跑，期间禁用避免连点
                                enabled: !controller.autoStartBusy
                                // 程序化同步开关时不要再触发一次写入
                                property bool syncing: false
                                onToggled: {
                                    if (syncing)
                                        return;
                                    controller.autoStart = checked;
                                }
                            }
                        }
                    }

                    // ---- 阻止自动弹出浏览器（NCSI 是 Windows 独有的策略，别的平台没有这一项）----
                    Rectangle {
                        width: parent.width
                        height: 1
                        visible: controller.supportsBrowserLaunchPolicy
                        color: systemTheme.hoverSoft
                    }

                    Item {
                        width: parent.width
                        height: Math.max(64, blockColumn.implicitHeight)
                        visible: controller.supportsBrowserLaunchPolicy

                        Column {
                            id: blockColumn
                            anchors.left: parent.left
                            anchors.right: blockRow.left
                            anchors.rightMargin: 16
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 5

                            Text {
                                text: "阻止系统自动弹出浏览器"
                                color: settingsPage.textColor
                                font.pixelSize: 14
                            }

                            Text {
                                width: parent.width
                                text: "关闭 NCSI 自动拉起浏览器的行为"
                                    + "（组策略值 NoWebBrowserLaunch，位于 "
                                    + "HKLM\\SOFTWARE\\Policies\\Microsoft\\Windows\\NetworkConnectivityStatusIndicator），"
                                    + "Windows 就不会在检测到需要认证的网络时自动打开浏览器页面。"
                                    + "主动探测保持开启，系统判断网络状态的行为不受影响。"
                                    + "这是系统级设置，切换时会弹出 UAC 提权窗口，允许后由提权进程写入。"
                                color: settingsPage.subTextColor
                                font.pixelSize: 11
                                wrapMode: Text.WordWrap
                            }

                            Text {
                                id: blockHint
                                width: parent.width
                                // 有失败原因就直接显示原因，否则按当前状态提示
                                text: controller.blockAutoBrowserBusy
                                    ? "正在写入，弹出 UAC 窗口时请点「是」…"
                                    : (controller.blockAutoBrowserError.length > 0
                                        ? controller.blockAutoBrowserError
                                        : (controller.blockAutoBrowser
                                            ? "已关闭自动拉起浏览器，系统主动探测仍然开启"
                                            : (controller.isAdmin
                                                ? "开启后 Windows 不再自动打开认证页面"
                                                : "开启时会弹出 UAC 提权窗口，点「是」即可写入")))
                                visible: text.length > 0
                                color: controller.blockAutoBrowserBusy
                                    ? settingsPage.subTextColor
                                    : (controller.blockAutoBrowserError.length > 0
                                        ? "#FFC42B1C"
                                        : settingsPage.subTextColor)
                                font.pixelSize: 11
                                wrapMode: Text.WordWrap
                            }
                        }

                        Row {
                            id: blockRow
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 8

                            Item {
                                width: 18
                                height: 18
                                visible: controller.blockAutoBrowserBusy

                                BusyIndicator {
                                    anchors.fill: parent
                                    running: controller.blockAutoBrowserBusy
                                }
                            }

                            Switch {
                                id: blockSwitch
                                checked: controller.blockAutoBrowser
                                // UAC 提权可能要等用户点确认，期间禁用避免连点
                                enabled: !controller.blockAutoBrowserBusy
                                property bool syncing: false
                                onToggled: {
                                    if (syncing)
                                        return;
                                    // 实际写入在后台线程；失败原因由 blockAutoBrowserError 通知界面
                                    controller.blockAutoBrowser = checked;
                                }
                            }
                        }
                    }
                }
            }

            // 项目介绍
            Rectangle {
                        width: parent.width
                        height: 240
                        radius: 10
                        color: settingsPage.cardColor
                        Column {
                            anchors.fill: parent
                            anchors.margins: 20
                            spacing: 12

                            Text {
                                text: "关于"
                                font.pixelSize: 18
                                font.bold: true
                            }

                            Text {
                                text: "《莆田学院校园网自动登录》是一个为了解决莆田学院校园网无法无感登录问题的程序。"
                                font.pixelSize: 14
                            }

                            Text {
                                text: "使用本程序，可以实现开机自动登录校园网，无需手动输入验证码，解决了最后一步。"
                                font.pixelSize: 14
                            }

                            Text {
                                text: "即使如此，也有小概率原因验证码识别失败。可以试试再来一次，或者手动登录"
                                font.pixelSize: 14
                            }

                                Text {
                                    text: "Github: https://github.com/qipaozhu/PTUCampusNetLogin"
                                    font.pixelSize: 14
                                    
                                    MouseArea{
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: Qt.openUrlExternally("https://github.com/qipaozhu")
                                    }
                                }
                                Text {
                                    text: "项目主页: https://www.maplekilo.cn/project/ptulogin"
                                    font.pixelSize: 14
                                    
                                    MouseArea{
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: Qt.openUrlExternally("https://www.maplekilo.cn/project/ptulogin")
                                    }
                                }
                        }
            }
        }
    }

    // 两个系统设置的写入都在后台线程完成，结果回来后再把开关同步到真实状态。
    // 同步时置 syncing 标记，避免又一次触发 onToggled 形成回环
    Connections {
        target: controller

        function onAutoStartChanged() {
            autoStartSwitch.syncing = true;
            autoStartSwitch.checked = controller.autoStart;
            autoStartSwitch.syncing = false;
        }

        function onBlockAutoBrowserChanged() {
            blockSwitch.syncing = true;
            blockSwitch.checked = controller.blockAutoBrowser;
            blockSwitch.syncing = false;
        }
    }

    // Portal 地址格式错误时的提示
    Dialog {
        id: portalDialog
        parent: settingsPage
        anchors.centerIn: parent
        modal: true
        title: "Portal 地址格式不正确"
        width: 420
        padding: 20

        header: Label {
            text: portalDialog.title
            font.family: appFontFamily
            font.bold: true
            font.pixelSize: 20
            font.weight: Font.DemiBold
            topPadding: portalDialog.padding
            leftPadding: portalDialog.padding
            rightPadding: portalDialog.padding
            elide: Label.ElideRight
        }

        background: Rectangle {
            color: settingsPage.cardColor
            radius: 10
            border.width: 0
        }

        contentItem: Column {
            width: 360
            spacing: 14

            Text {
                width: parent.width
                text: "请填写形如 192.168.116.8:801 的地址。\n"
                    + "主机可以是 IPv4 或域名，允许带 http:// 前缀和路径；"
                    + "端口省略时按 801 处理，范围 1–65535。"
                color: settingsPage.textColor
                font.pixelSize: 13
                wrapMode: Text.WordWrap
            }

            Button {
                text: "知道了"
                font.family: appFontFamily
                onClicked: portalDialog.close()
            }
        }
    }
}
