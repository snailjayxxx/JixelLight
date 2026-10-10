import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtCore

Dialog {
    id: root
    required property var controller
    required property real hostWidth
    required property real hostHeight
    objectName: "copyImportDialog"
    modal: true
    x: Math.round((hostWidth - width) / 2)
    y: Math.max(16, Math.round((hostHeight - height) / 2))
    width: Math.min(700, hostWidth - 32)
    standardButtons: Dialog.Ok | Dialog.Cancel
    property var sources: []
    property url destination: ""
    property bool customNames: false
    property string pattern: "{name}_{seq:4}"
    property int sequenceStart: 1
    readonly property var preview: {
        const revision = controller.importNameRevision
        const language = controller.language
        if (!visible) return { valid: false, pending: false, rows: [], error: "" }
        if (customNames && pattern.length === 0)
            return { valid: false, pending: false, rows: [], error: t("请输入名称模板", "Enter a filename template") }
        return controller.previewImportNames(sources, customNames ? pattern : "", sequenceStart)
    }
    function t(zh, en) { return controller.language === "zh_CN" ? zh : en }
    function openFor(urls) { sources = urls; open() }
    Settings {
        category: "CopyImportNaming"
        property alias destination: root.destination
        property alias customNames: root.customNames
        property alias pattern: root.pattern
        property alias sequenceStart: root.sequenceStart
    }
    FolderDialog {
        id: folder
        title: root.t("选择复制目标文件夹", "Choose the copy destination folder")
        currentFolder: root.destination
        onAccepted: root.destination = selectedFolder
    }
    title: t("复制并导入 · 名称预览", "Copy and import · Name preview")
    onOpened: {
        const ok = standardButton(Dialog.Ok)
        ok.objectName = "copyImportApply"
        ok.text = Qt.binding(function() { return root.t("开始复制", "Start copy") })
        ok.enabled = Qt.binding(function() { return root.preview.valid && root.destination.toString().length > 0 && !root.controller.copyImportBusy })
        standardButton(Dialog.Cancel).text = Qt.binding(function() { return root.t("取消", "Cancel") })
    }
    onAccepted: controller.copyImport(sources, destination, customNames ? pattern : "", sequenceStart)
    // Accepted queues its immutable metadata snapshot before the cache clears.
    onClosed: Qt.callLater(function() { if (!root.visible) root.controller.cancelImportNamePreview() })
    contentItem: ColumnLayout {
        implicitWidth: 660
        spacing: 10
        RowLayout {
            Layout.fillWidth: true
            Label { text: root.t("目标文件夹", "Destination folder") }
            Button { text: root.t("选择…", "Choose…"); onClicked: folder.open() }
        }
        Label {
            Layout.fillWidth: true
            text: root.destination.toString().length ? root.destination.toString() : root.t("请先选择已存在的文件夹", "Choose an existing folder first")
            textFormat: Text.PlainText; elide: Text.ElideMiddle
            ToolTip.visible: destinationHover.hovered; ToolTip.text: text
            HoverHandler { id: destinationHover }
        }
        CheckBox {
            objectName: "copyImportCustomNames"
            text: root.t("使用自定义名称（关闭时保留原名）", "Use custom names (off keeps original names)")
            checked: root.customNames
            onToggled: root.customNames = checked
        }
        TextField {
            objectName: "copyImportPattern"
            Layout.fillWidth: true
            enabled: root.customNames
            text: root.pattern
            maximumLength: 160
            selectByMouse: true
            onTextEdited: root.pattern = text
        }
        RowLayout {
            Layout.fillWidth: true
            Label { text: root.t("起始序号", "Starting sequence") }
            SpinBox {
                enabled: root.customNames
                from: 1; to: 999999999; value: root.sequenceStart; editable: true
                Layout.preferredWidth: 170
                onValueModified: root.sequenceStart = value
            }
        }
        Label {
            Layout.fillWidth: true
            text: root.t("{name}：原名（不含扩展名）；{seq}：序号；{seq:4}：补零至 4 位（1–9 位）。扩展名自动保留，按选择顺序编号。",
                "{name}: original stem; {seq}: sequence; {seq:4}: pad to 4 digits (1–9). Extensions are preserved. Numbers follow selection order.")
            wrapMode: Text.WordWrap; color: "#a3b2c1"
        }
        Label {
            Layout.fillWidth: true
            text: root.t("{capture_date}：yyyyMMdd；{capture_time}：HHmmss。使用相机记录的拍摄时间，缺失时整批拒绝。",
                "{capture_date}: yyyyMMdd; {capture_time}: HHmmss. Camera capture time is required for every file.")
            wrapMode: Text.WordWrap; color: "#a3b2c1"
        }
        Label {
            Layout.fillWidth: true
            text: root.preview.valid ? root.t("名称预览 · %1 个文件", "Name preview · %1 file(s)").arg(root.sources.length)
                : root.preview.pending ? root.preview.error : root.t("名称计划错误：", "Name plan error: ") + root.preview.error
            textFormat: Text.PlainText; wrapMode: Text.WrapAnywhere
            color: root.preview.valid || root.preview.pending ? "#d7dde6" : "#f0a58c"
        }
        Frame {
            Layout.fillWidth: true
            Layout.preferredHeight: 150
            ListView {
                anchors.fill: parent
                clip: true
                model: root.preview.rows || []
                ScrollBar.vertical: ScrollBar { }
                delegate: RowLayout {
                    required property var modelData
                    width: ListView.view.width - 16
                    height: 28
                    Label {
                        Layout.fillWidth: true; Layout.preferredWidth: 1
                        text: modelData.source; textFormat: Text.PlainText; elide: Text.ElideMiddle
                        ToolTip.visible: sourceHover.hovered; ToolTip.text: text
                        HoverHandler { id: sourceHover }
                    }
                    Label { text: "→"; color: "#8b98a6" }
                    Label {
                        Layout.fillWidth: true; Layout.preferredWidth: 1
                        text: modelData.destination; textFormat: Text.PlainText; elide: Text.ElideMiddle
                        ToolTip.visible: targetHover.hovered; ToolTip.text: text
                        HoverHandler { id: targetHover }
                    }
                }
            }
        }
        Label {
            Layout.fillWidth: true
            text: root.t("开始后会检查目标冲突并校验内容。同名文件整批拒绝；取消保留已完成副本。原始文件保持只读。",
                "Start checks destination conflicts and verifies content. Existing names reject the whole plan. Cancel retains completed copies. Source files remain read-only.")
            wrapMode: Text.WordWrap; color: "#a3b2c1"
        }
    }
}
