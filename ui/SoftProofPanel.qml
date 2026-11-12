import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore

ColumnLayout {
    id: root
    property var controller
    property string language: "zh_CN"
    property bool expanded: false
    function t(zh, en) { return language === "zh_CN" ? zh : en }
    spacing: 8
    Settings { category: "SoftProofPanel"; property alias expanded: root.expanded }
    ToolButton {
        objectName: "softProofPanelToggle"
        Layout.fillWidth: true; Layout.minimumWidth: 0
        text: (root.expanded ? "▾ " : "▸ ") + root.t("软打样", "SOFT PROOF")
              + (root.controller.softProofEnabled ? root.t(" · 已开启", " · On") : "")
        onClicked: root.expanded = !root.expanded
    }
    ColumnLayout {
        visible: root.expanded
        Layout.fillWidth: true; Layout.minimumWidth: 0
        spacing: 8
        CheckBox { objectName: "softProofEnable"; Layout.fillWidth: true; Layout.minimumWidth: 0; text: root.t("启用软打样", "Enable soft proof"); checked: root.controller.softProofEnabled; onToggled: root.controller.setSoftProofEnabled(checked) }
        ComboBox {
            objectName: "softProofTarget"
            Layout.fillWidth: true; Layout.minimumWidth: 0
            textRole: "text"; valueRole: "key"
            model: [{key:"srgb",text:"sRGB"},{key:"display-p3",text:"Display P3"},{key:"adobe-rgb",text:"Adobe RGB"},{key:"prophoto-rgb",text:"ProPhoto RGB"},{key:"custom",text:root.t("自选 ICC", "Custom ICC")}]
            currentIndex: Math.max(0, model.findIndex(function(row) { return row.key === root.controller.softProofTarget }))
            onActivated: root.controller.setSoftProofTarget(currentValue)
        }
        Button { objectName: "softProofChooseProfile"; Layout.fillWidth: true; Layout.minimumWidth: 0; text: root.t("选择打印机或显示 ICC…", "Choose printer or display ICC…"); onClicked: root.controller.chooseSoftProofProfile() }
        Label { objectName: "softProofProfileName"; Layout.fillWidth: true; Layout.minimumWidth: 0; textFormat: Text.PlainText; wrapMode: Text.WordWrap; color: "#b7c3d3"; text: root.controller.softProofProfileName }
        ComboBox {
            objectName: "softProofIntent"
            Layout.fillWidth: true; Layout.minimumWidth: 0
            model: [root.t("感知", "Perceptual"),root.t("相对比色", "Relative colorimetric"),root.t("饱和度", "Saturation"),root.t("绝对比色", "Absolute colorimetric")]
            currentIndex: root.controller.softProofIntent
            onActivated: root.controller.setSoftProofIntent(currentIndex)
        }
        CheckBox { objectName: "softProofPaper"; Layout.fillWidth: true; Layout.minimumWidth: 0; text: root.t("模拟纸张白色", "Simulate paper white"); checked: root.controller.softProofPaper; onToggled: root.controller.setSoftProofPaper(checked) }
        CheckBox { objectName: "softProofBlackPoint"; Layout.fillWidth: true; Layout.minimumWidth: 0; text: root.t("黑点补偿", "Black point compensation"); checked: root.controller.softProofBlackPoint; onToggled: root.controller.setSoftProofBlackPoint(checked) }
        CheckBox { objectName: "softProofGamut"; Layout.fillWidth: true; Layout.minimumWidth: 0; text: root.t("紫色色域警告", "Magenta gamut warning"); checked: root.controller.softProofGamutWarning; onToggled: root.controller.setSoftProofGamutWarning(checked) }
        Button { objectName: "softProofRefresh"; Layout.fillWidth: true; Layout.minimumWidth: 0; enabled: root.controller.softProofEnabled && root.controller.hasImage; text: root.t("重新生成", "Refresh proof"); onClicked: root.controller.refreshSoftProof() }
        Label { objectName: "softProofStatus"; Layout.fillWidth: true; Layout.minimumWidth: 0; textFormat: Text.PlainText; wrapMode: Text.WordWrap; color: "#aebdce"; text: root.controller.softProofStatus }
        Label { Layout.fillWidth: true; Layout.minimumWidth: 0; wrapMode: Text.WordWrap; color: "#8996a7"; font.pixelSize: 10; text: root.t("仅改变屏幕预览。直方图、参考图和导出仍反映编辑结果。", "Changes the screen preview. Histograms, reference images and exports continue to use the edit result.") }
    }
}
