import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root
    required property var controller
    objectName: "adjustmentTransferDialog"
    modal: true
    anchors.centerIn: parent
    width: Math.min(560, parent.width - 32)
    standardButtons: Dialog.Ok | Dialog.Cancel
    property bool syncMode: false
    property var groups: []
    readonly property var definitions: [
        { key: "exposure", zh: "曝光", en: "Exposure" },
        { key: "white_balance", zh: "白平衡偏移", en: "White balance deltas" },
        { key: "tone", zh: "明暗与高光恢复", en: "Tone / highlight recovery" },
        { key: "color", zh: "整体色相与饱和度", en: "Global color" },
        { key: "hsl", zh: "HSL 混色器", en: "HSL mixer" },
        { key: "curves", zh: "主曲线与 RGB 曲线", en: "Master / RGB curves" },
        { key: "sony_look", zh: "Sony 创意外观", en: "Sony Creative Look" },
        { key: "geometry", zh: "裁切、拉直与方向", en: "Crop, straighten / orientation" }
    ]
    readonly property int targetCount: {
        if (!controller.hasImage) return 0
        if (!syncMode) return 1
        if (targetScope.currentIndex === 1) return Math.max(0, controller.library.length - 1)
        return controller.selectedIndices.filter(function(i) { return i !== controller.currentIndex }).length
    }
    function t(zh, en) { return controller.language === "zh_CN" ? zh : en }
    function setAll(includeGeometry) {
        groups = definitions.filter(function(g) { return includeGeometry || g.key !== "geometry" }).map(function(g) { return g.key })
    }
    function openFor(sync) {
        syncMode = sync
        targetScope.currentIndex = 0
        setAll(false)
        open()
    }
    function toggleGroup(key, enabled) {
        let updated = groups.filter(function(g) { return g !== key })
        if (enabled) updated.push(key)
        groups = updated
    }
    title: syncMode ? t("同步调整", "Sync adjustments") : t("选择粘贴参数", "Paste selected settings")
    onOpened: {
        const ok = standardButton(Dialog.Ok)
        ok.objectName = "transferApplyButton"
        ok.text = Qt.binding(function() { return root.syncMode ? root.t("同步", "Sync") : root.t("粘贴", "Paste") })
        ok.enabled = Qt.binding(function() { return root.groups.length > 0 && root.targetCount > 0 && (root.syncMode || root.controller.hasAdjustmentClipboard) })
        standardButton(Dialog.Cancel).text = Qt.binding(function() { return root.t("取消", "Cancel") })
    }
    onAccepted: {
        if (syncMode) controller.syncAdjustmentGroups(groups, targetScope.currentIndex === 0)
        else controller.pasteAdjustmentGroups(groups)
    }
    contentItem: ColumnLayout {
        implicitWidth: 520
        spacing: 12
        Label {
            Layout.fillWidth: true
            text: root.t("来源：", "Source: ") + (root.syncMode
                ? (root.controller.hasImage ? root.controller.library[root.controller.currentIndex].name : "")
                : root.controller.adjustmentClipboardName)
            textFormat: Text.PlainText; elide: Text.ElideMiddle
        }
        ComboBox {
            id: targetScope
            objectName: "transferTargetScope"
            visible: root.syncMode
            Layout.fillWidth: true
            model: [root.t("已选版本（不含来源）", "Selected versions, excluding source"),
                root.t("图库全部版本（不含来源）", "All library versions, excluding source")]
        }
        Label {
            Layout.fillWidth: true
            text: root.syncMode ? root.t("目标：%1 个版本；包含已选择但被筛选隐藏的项目。", "%1 target version(s); selection includes items hidden by filters.").arg(root.targetCount)
                : root.t("粘贴到当前版本；复制后的来源更改不会影响此快照。", "Paste into the current version using the snapshot taken at Copy.")
            wrapMode: Text.WordWrap; color: "#a3b2c1"
        }
        RowLayout {
            Button { text: root.t("全选", "All"); onClicked: root.setAll(true) }
            Button { text: root.t("全不选", "None"); objectName: "transferSelectNone"; onClicked: root.groups = [] }
            Button { text: root.t("显影参数", "Develop only"); onClicked: root.setAll(false) }
        }
        GridLayout {
            columns: 2
            Layout.fillWidth: true
            columnSpacing: 12
            Repeater {
                model: root.definitions
                CheckBox {
                    required property var modelData
                    objectName: "transferGroup_" + modelData.key
                    text: root.t(modelData.zh, modelData.en)
                    checked: root.groups.indexOf(modelData.key) >= 0
                    onToggled: root.toggleGroup(modelData.key, checked)
                }
            }
        }
        Label {
            Layout.fillWidth: true
            text: root.t("仅替换勾选的参数组；每个目标版本可独立撤销。评分、关键词和相册保持原值。",
                "Only checked groups are replaced. Undo is independent for each target. Ratings, keywords and albums are preserved.")
            wrapMode: Text.WordWrap; color: "#a3b2c1"
        }
    }
}
