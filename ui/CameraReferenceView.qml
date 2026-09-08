import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
ColumnLayout {
    id: root
    required property var controller
    property var info: controller.cameraReferenceInfo
    property var counts: controller.referenceHistogram
    function t(zh,en) { return controller.language === "zh_CN" ? zh : en }
    spacing: 6
    Label { Layout.fillWidth: true; wrapMode: Text.Wrap; color: "#dce5ef"; text: root.t("相机 / 参考图 · 不参与当前 RAW 导出", "Camera / reference · not the RAW export") }
    Label { Layout.fillWidth: true; wrapMode: Text.Wrap; color: "#aebccc"; font.pixelSize: 10
        text: (root.info.kind === "paired-jpeg" ? root.t("同名 JPEG：元数据匹配，非真实性认证", "Companion JPEG: metadata match, not authentication") : root.info.kind === "embedded-preview" ? root.t("RAW 内嵌相机预览", "Embedded camera preview") : root.t("用户选择参考图，非自动认证", "User-selected reference, not authenticated"))
            + " · " + (root.info.width || 0) + " × " + (root.info.height || 0) + " · " + (root.info.colorBasis || "")
    }
    Item { Layout.fillWidth: true; Layout.fillHeight: true
        Image { objectName: "cameraReferenceImage"; anchors.fill: parent; source: root.controller.cameraReferenceUrl; fillMode: Image.PreserveAspectFit; asynchronous: true; cache: false }
        BusyIndicator { anchors.centerIn: parent; running: root.controller.referenceBusy; visible: running }
        Label { anchors.centerIn: parent; width: parent.width; wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter; color: "#ffb066"; visible: !root.controller.referenceBusy && !root.controller.cameraReferenceUrl; text: root.info.error || root.t("暂无参考预览", "No reference preview") }
    }
    Label { Layout.fillWidth: true; wrapMode: Text.Wrap; color: "#8e9aa8"; font.pixelSize: 10; text: root.t("仅参考预览像素的直方图", "Histogram of reference preview pixels only") + " · 1024 bins · " + (root.counts.pixels || 0) }
    HistogramView { Layout.fillWidth: true; Layout.preferredHeight: 100; redData: root.counts.red || []; greenData: root.counts.green || []; blueData: root.counts.blue || []; lumaData: root.counts.luma || [] }
}
