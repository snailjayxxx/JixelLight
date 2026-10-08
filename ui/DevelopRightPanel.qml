import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore

// Lightroom-inspired collapsible editing panels. All commands still route
// through the existing JixelLight controller and CPU/GPU processing chain.
Rectangle {
    id: root
    required property var controller
    property bool sonyExpanded: false
    property bool basicExpanded: true
    property bool colorExpanded: true
    property bool mixerExpanded: false
    property bool curveExpanded: false
    property bool exifExpanded: false
    color: "#15191f"
    border.color: "#292f37"

    Settings {
        category: "DevelopPanelSections"
        property alias sonyExpanded: root.sonyExpanded
        property alias basicExpanded: root.basicExpanded
        property alias colorExpanded: root.colorExpanded
        property alias mixerExpanded: root.mixerExpanded
        property alias curveExpanded: root.curveExpanded
        property alias exifExpanded: root.exifExpanded
    }
    function t(zh, en) { return controller.language === "zh_CN" ? zh : en }
    function meta(key) {
        const value = controller.currentMetadata[key]
        return value === undefined || value === null || value === "" ? "—" : value
    }
    function cameraName() {
        const make = meta("make")
        const model = meta("model")
        if (make === "—") return model
        if (model === "—") return make
        return make + " " + model
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        // Sticky Scopes: the existing 1024-bin GPU histogram is kept pinned.
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            Layout.topMargin: 8
            Layout.bottomMargin: 8
                Label { text: root.t("当前编辑图像 · 专业示波器", "CURRENT EDIT · SCOPES"); color: "#8e9aa8"; font.bold: true; font.pixelSize: 11; Layout.topMargin: 10 }
                RowLayout {
                    Layout.fillWidth: true
                    ComboBox {
                        objectName: "scopeModeChoice"; Layout.fillWidth: true
                        model: [root.t("直方图", "Histogram"),root.t("亮度波形", "Luma waveform"),"RGB Parade",root.t("矢量示波器", "Vectorscope")]
                        currentIndex: ["histogram","waveform","parade","vectorscope"].indexOf(root.controller.scopeMode)
                        onActivated: root.controller.scopeMode=["histogram","waveform","parade","vectorscope"][currentIndex]
                    }
                }
                RowLayout {
                    Layout.fillWidth: true; visible: root.controller.scopeMode==="histogram"
                    Button { id: rgbButton; text: "RGB"; checkable: true; checked: true; onClicked: { checked = true; lumaButton.checked = false } }
                    Button { id: lumaButton; text: root.t("亮度", "Luma"); checkable: true; onClicked: { checked = true; rgbButton.checked = false } }
                    Item { Layout.fillWidth: true }
                    Label { text: "1024 bins · " + root.controller.scopesPixelCount; color: "#738293"; font.pixelSize: 10 }
                }
                RowLayout {
                    Label { text: root.controller.scopeMode==="histogram" ? root.controller.scopesStatus : root.controller.scopePlotStatus; wrapMode: Text.WordWrap; color: "#8e9aa8"; font.pixelSize: 10; Layout.fillWidth: true }
                    CheckBox { text: root.t("全分辨率", "Full resolution"); checked: root.controller.exactScopes; onToggled: root.controller.exactScopes=checked }
                }
                HistogramView {
                    visible: root.controller.scopeMode==="histogram"
                    Layout.fillWidth: true; Layout.preferredHeight: 115
                    redData: root.controller.redHistogram; greenData: root.controller.greenHistogram; blueData: root.controller.blueHistogram
                    lumaData: root.controller.lumaHistogram; showLuma: lumaButton.checked
                }
                ScopePlotView {
                    objectName: "scopePlotView"
                    visible: root.controller.scopeMode!=="histogram"
                    Layout.fillWidth: true; Layout.preferredHeight: 170
                    mode: root.controller.scopeMode; imageUrl: root.controller.scopePlotUrl; current: root.controller.scopePlotCurrent
                }
                Label {
                    visible: root.controller.scopeMode!=="histogram"; Layout.fillWidth: true
                    text: root.controller.scopeMode==="vectorscope" ? root.t("sRGB 编码 · Cb/Cr · 100% 色标", "Encoded sRGB · Cb/Cr · 100% targets") : root.t("sRGB 编码 · 1024 级 · 水平位置", "Encoded sRGB · 1024 levels · horizontal position")
                    color: "#738293"; font.pixelSize: 10
                }
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: root.t("阴影裁切  ", "Shadows clipped  ") + root.controller.shadowClipPercent.toFixed(3) + "%"; color: root.controller.shadowClipPercent > 0.1 ? "#ffb066" : "#8b98a6"; font.pixelSize: 10 }
                    Item { Layout.fillWidth: true }
                    Label { text: root.t("高光裁切  ", "Highlights  ") + root.controller.highlightClipPercent.toFixed(3) + "%"; color: root.controller.highlightClipPercent > 0.1 ? "#ff8e8e" : "#8b98a6"; font.pixelSize: 10 }
                }

        }
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            spacing: 2
            Label { text: root.t("评分", "Rating"); color: "#a2b1c0"; font.pixelSize: 10 }
            Repeater {
                model: 5
                delegate: ToolButton {
                    required property int index
                    text: index < root.controller.currentRating ? "★" : "☆"
                    enabled: root.controller.hasImage
                    font.pixelSize: 15
                    onClicked: root.controller.setRating(index + 1)
                }
            }
            ToolButton {
                text: "P"
                enabled: root.controller.hasImage
                highlighted: root.controller.currentFlag === "pick"
                onClicked: root.controller.setFlag(root.controller.currentFlag === "pick" ? "none" : "pick")
            }
            ToolButton {
                text: "X"
                enabled: root.controller.hasImage
                highlighted: root.controller.currentFlag === "reject"
                onClicked: root.controller.setFlag(root.controller.currentFlag === "reject" ? "none" : "reject")
            }
            Item { Layout.fillWidth: true }
            ToolButton {
                text: "0"
                enabled: root.controller.hasImage
                onClicked: root.controller.setRating(0)
            }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: "#303840" }
        ScrollView {
            id: toolScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ColumnLayout {
                x: 10
                width: Math.max(280, toolScroll.availableWidth - 20)
                spacing: 6

                Label { text: root.t("裁切 / 旋转（CPU 几何准备）", "CROP / ROTATE (CPU GEOMETRY)"); color: "#a3b2c1"; font.bold: true }
                Button {
                    objectName: "beginInteractiveCrop"; Layout.fillWidth: true
                    text: root.controller.cropEditing ? root.t("取消裁剪编辑", "Cancel crop editing") : root.t("在画布上裁剪…", "Crop on canvas…")
                    enabled: root.controller.previewReady
                    onClicked: root.controller.cropEditing ? root.controller.cancelCrop() : root.controller.beginCrop()
                }
                RowLayout {
                    Layout.fillWidth: true
                    Button { Layout.fillWidth: true; Layout.minimumWidth: 0; text: "↶ 90°"; enabled: root.controller.hasImage; onClicked: root.controller.rotatePhoto(-1) }
                    Button { Layout.fillWidth: true; Layout.minimumWidth: 0; text: "↷ 90°"; enabled: root.controller.hasImage; onClicked: root.controller.rotatePhoto(1) }
                    Button { Layout.fillWidth: true; Layout.minimumWidth: 0; text: root.t("水平翻转", "Flip H"); enabled: root.controller.hasImage; onClicked: root.controller.flipPhoto(true) }
                    Button { Layout.fillWidth: true; Layout.minimumWidth: 0; text: root.t("垂直翻转", "Flip V"); enabled: root.controller.hasImage; onClicked: root.controller.flipPhoto(false) }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: root.t("居中裁切", "Center crop"); color: "#a3b2c1" }
                    Button { Layout.fillWidth: true; Layout.minimumWidth: 0; text: "1:1"; enabled: root.controller.previewReady; onClicked: root.controller.setCropAspect(1) }
                    Button { Layout.fillWidth: true; Layout.minimumWidth: 0; text: "3:2"; enabled: root.controller.previewReady; onClicked: root.controller.setCropAspect(1.5) }
                    Button { Layout.fillWidth: true; Layout.minimumWidth: 0; text: "4:3"; enabled: root.controller.previewReady; onClicked: root.controller.setCropAspect(4/3) }
                    Button { Layout.fillWidth: true; Layout.minimumWidth: 0; text: root.t("重置", "Reset"); enabled: root.controller.hasImage; onClicked: root.controller.resetGeometry() }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: "#29333e" }
                ToolButton {
                    Layout.fillWidth: true
                    text: (root.sonyExpanded ? "▾ " : "▸ ") + root.t("相机 / Sony 创意外观", "CAMERA / SONY CREATIVE LOOK")
                    onClicked: root.sonyExpanded = !root.sonyExpanded
                    font.bold: true
                }
                SonyLookPanel {
                    controller: root.controller
                    Layout.fillWidth: true
                    visible: root.sonyExpanded
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: "#29333e" }
                RowLayout {
                    Layout.fillWidth: true
                    ToolButton {
                        text: (root.basicExpanded ? "▾ " : "▸ ") + root.t("基础调整", "LIGHT")
                        onClicked: root.basicExpanded = !root.basicExpanded
                        font.bold: true
                    }
                    Item { Layout.fillWidth: true }
                    Button { text: root.t("全部重置", "Reset All"); enabled: root.controller.hasImage; onClicked: root.controller.resetAdjustments() }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    visible: root.basicExpanded
                AdjustmentSlider { Layout.fillWidth: true; label: root.t("曝光", "Exposure"); from: -5; to: 5; decimals: 2; value: root.controller.exposure; onEdited: root.controller.exposure = newValue }
                AdjustmentSlider { Layout.fillWidth: true; label: root.t("色温", "Temperature"); value: root.controller.temperature; onEdited: root.controller.temperature = newValue }
                AdjustmentSlider { Layout.fillWidth: true; label: root.t("色调", "Tint"); value: root.controller.tint; onEdited: root.controller.tint = newValue }
                AdjustmentSlider { Layout.fillWidth: true; label: root.t("对比度", "Contrast"); value: root.controller.contrast; onEdited: root.controller.contrast = newValue }
                AdjustmentSlider { Layout.fillWidth: true; label: root.t("高光", "Highlights"); value: root.controller.highlights; onEdited: root.controller.highlights = newValue }
                AdjustmentSlider { Layout.fillWidth: true; label: root.t("阴影", "Shadows"); value: root.controller.shadows; onEdited: root.controller.shadows = newValue }
                AdjustmentSlider { Layout.fillWidth: true; label: root.t("白色色阶", "Whites"); value: root.controller.whites; onEdited: root.controller.whites = newValue }
                AdjustmentSlider { Layout.fillWidth: true; label: root.t("黑色色阶", "Blacks"); value: root.controller.blacks; onEdited: root.controller.blacks = newValue }
                AdjustmentSlider { Layout.fillWidth: true; label: root.t("高光恢复", "Highlight Recovery"); from: 0; to: 100; value: root.controller.highlightRecovery; onEdited: root.controller.highlightRecovery = newValue }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: "#29333e" }
                ToolButton {
                    Layout.fillWidth: true
                    text: (root.colorExpanded ? "▾ " : "▸ ") + root.t("颜色 / RAW 工作空间", "COLOR")
                    onClicked: root.colorExpanded = !root.colorExpanded
                    font.bold: true
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    visible: root.colorExpanded
                AdjustmentSlider { Layout.fillWidth: true; label: root.t("色相", "Hue"); from: -180; to: 180; value: root.controller.hue; onEdited: root.controller.hue = newValue }
                AdjustmentSlider { Layout.fillWidth: true; label: root.t("饱和度", "Saturation"); value: root.controller.saturation; onEdited: root.controller.saturation = newValue }
                AdjustmentSlider { Layout.fillWidth: true; label: root.t("自然饱和度", "Vibrance"); value: root.controller.vibrance; onEdited: root.controller.vibrance = newValue }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: "#29333e" }
                RowLayout {
                    Layout.fillWidth: true
                    ToolButton {
                        text: (root.mixerExpanded ? "▾ " : "▸ ") + root.t("HSL 颜色混合器", "COLOR MIXER")
                        onClicked: root.mixerExpanded = !root.mixerExpanded
                        font.bold: true
                    }
                    Item { Layout.fillWidth: true }
                    ComboBox {
                        id: mixerMode; Layout.preferredWidth: 105
                        model: [root.t("色相", "Hue"), root.t("饱和度", "Sat"), root.t("明度", "Luma")]
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    visible: root.mixerExpanded
                Repeater {
                    model: 8
                    delegate: AdjustmentSlider {
                        required property int index
                        Layout.fillWidth: true
                        label: [root.t("红色", "Red"), root.t("橙色", "Orange"), root.t("黄色", "Yellow"), root.t("绿色", "Green"), root.t("青色", "Aqua"), root.t("蓝色", "Blue"), root.t("紫色", "Purple"), root.t("洋红", "Magenta")][index]
                        value: mixerMode.currentIndex === 0 ? root.controller.hslHue[index] : mixerMode.currentIndex === 1 ? root.controller.hslSaturation[index] : root.controller.hslLuminance[index]
                        onEdited: root.controller.setColorMix(index, mixerMode.currentIndex, newValue)
                    }
                }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: "#29333e" }
                RowLayout {
                    Layout.fillWidth: true
                    ToolButton {
                        text: (root.curveExpanded ? "▾ " : "▸ ") + root.t("曲线", "TONE CURVE")
                        onClicked: root.curveExpanded = !root.curveExpanded
                        font.bold: true
                    }
                    Item { Layout.fillWidth: true }
                    ComboBox { id: curveChannel; Layout.preferredWidth: 110; model: [root.t("主曲线", "Master"), "Red", "Green", "Blue"] }
                    Button { text: root.t("重置", "Reset"); onClicked: root.controller.resetCurve(curveChannel.currentIndex) }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    visible: root.curveExpanded
                CurveEditor {
                    Layout.fillWidth: true; Layout.preferredHeight: 160
                    channel: curveChannel.currentIndex
                    values: curveChannel.currentIndex === 0 ? root.controller.masterCurve : curveChannel.currentIndex === 1 ? root.controller.redCurve : curveChannel.currentIndex === 2 ? root.controller.greenCurve : root.controller.blueCurve
                    onPointEdited: function(point, value) { root.controller.setCurvePoint(channel, point, value) }
                }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: "#29333e" }
                ToolButton {
                    Layout.fillWidth: true
                    text: (root.exifExpanded ? "▾ " : "▸ ") + root.t("照片信息 / EXIF", "PHOTO INFO / EXIF")
                    onClicked: root.exifExpanded = !root.exifExpanded
                    font.bold: true
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    visible: root.exifExpanded
                GridLayout {
                    Layout.fillWidth: true; columns: 2; columnSpacing: 10; rowSpacing: 5
                    Label { text: root.t("相机", "Camera"); color: "#748394"; font.pixelSize: 10 }
                    Label { text: root.cameraName(); color: "#c4cfda"; elide: Text.ElideRight; Layout.fillWidth: true; font.pixelSize: 10 }
                    Label { text: root.t("镜头", "Lens"); color: "#748394"; font.pixelSize: 10 }
                    Label { text: root.meta("lens"); color: "#c4cfda"; elide: Text.ElideRight; Layout.fillWidth: true; font.pixelSize: 10 }
                    Label { text: root.t("快门", "Shutter"); color: "#748394"; font.pixelSize: 10 }
                    Label { text: root.meta("shutter"); color: "#c4cfda"; font.pixelSize: 10 }
                    Label { text: root.t("光圈", "Aperture"); color: "#748394"; font.pixelSize: 10 }
                    Label { text: root.meta("aperture"); color: "#c4cfda"; font.pixelSize: 10 }
                    Label { text: "ISO"; color: "#748394"; font.pixelSize: 10 }
                    Label { text: root.meta("iso"); color: "#c4cfda"; font.pixelSize: 10 }
                    Label { text: root.t("焦距", "Focal Length"); color: "#748394"; font.pixelSize: 10 }
                    Label { text: root.meta("focalLength"); color: "#c4cfda"; font.pixelSize: 10 }
                    Label { text: root.t("拍摄时间", "Captured"); color: "#748394"; font.pixelSize: 10 }
                    Label { text: root.meta("captureTime"); color: "#c4cfda"; elide: Text.ElideRight; Layout.fillWidth: true; font.pixelSize: 10 }
                    Label { text: root.t("尺寸", "Dimensions"); color: "#748394"; font.pixelSize: 10 }
                    Label { text: root.meta("pixelWidth") + " × " + root.meta("pixelHeight"); color: "#c4cfda"; font.pixelSize: 10 }
                    Label { visible: root.controller.currentIsRaw; text: root.t("RAW 深度", "RAW Depth"); color: "#748394"; font.pixelSize: 10 }
                    Label { visible: root.controller.currentIsRaw; text: root.meta("bitDepth") + "-bit"; color: "#88ead0"; font.pixelSize: 10 }
                    Label { visible: root.controller.currentIsRaw; text: root.t("工作空间", "Working Space"); color: "#748394"; font.pixelSize: 10 }
                    Label { visible: root.controller.currentIsRaw; text: root.meta("workingSpace"); color: "#88ead0"; font.pixelSize: 10 }
                    Label { visible: root.controller.currentIsRaw; text: root.t("去马赛克", "Demosaic"); color: "#748394"; font.pixelSize: 10 }
                    Label { visible: root.controller.currentIsRaw; text: root.meta("demosaic"); color: "#88ead0"; font.pixelSize: 10 }
                }

                Label {
                    Layout.fillWidth: true
                    text: root.t("处理顺序：RAW → Camera WB/Matrix → Linear ProPhoto → HSL/Color → Curves → ICC sRGB Preview", "Graph: RAW → Camera WB/Matrix → Linear ProPhoto → HSL/Color → Curves → ICC sRGB Preview")
                    wrapMode: Text.WordWrap; color: "#627180"; font.pixelSize: 10; Layout.bottomMargin: 18
                }
                }
            }
        }
    }
}
