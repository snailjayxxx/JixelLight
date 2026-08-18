import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ApplicationWindow {
    id: window
    visible: true
    width: 1540; height: 920; minimumWidth: 1180; minimumHeight: 720
    title: "JixelLight " + appVersion + (photoController.projectName ? " — " + photoController.projectName : "")
    color: "#0b0d10"

    function t(zh, en) { return photoController.language === "zh_CN" ? zh : en }
    function meta(key) {
        const value = photoController.currentMetadata[key]
        return value === undefined || value === null || value === "" ? "—" : value
    }
    function cameraName() {
        const make = meta("make")
        const model = meta("model")
        if (make === "—") return model
        if (model === "—") return make
        return make + " " + model
    }
    function exportSpaceKey(index) {
        return ["srgb", "display-p3", "adobe-rgb", "prophoto-rgb"][Math.max(0, Math.min(3, index))]
    }

    Shortcut { sequence: StandardKey.Open; onActivated: photoController.openImportDialog() }

    DropArea {
        anchors.fill: parent
        onDropped: function(drop) {
            if (!drop.urls || drop.urls.length === 0) return
            for (let i = 0; i < drop.urls.length; ++i) photoController.importFile(drop.urls[i])
            drop.acceptProposedAction()
        }
    }

    Dialog {
        id: exportSettingsDialog
        title: window.t("JPEG 导出设置", "JPEG Export Settings")
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        anchors.centerIn: parent
        onAccepted: exportDialog.open()
        ColumnLayout {
            width: 430; spacing: 12
            Label { text: window.t("输出色彩空间 / ICC", "Output Color Space / ICC"); color: "#d6dee8"; font.bold: true }
            ComboBox {
                id: exportSpaceBox
                Layout.fillWidth: true
                model: ["sRGB", "Display P3", "Adobe RGB (1998)", "ProPhoto RGB"]
                currentIndex: 0
            }
            RowLayout {
                Layout.fillWidth: true
                Label { text: window.t("JPEG 质量", "JPEG Quality"); color: "#d6dee8" }
                Item { Layout.fillWidth: true }
                SpinBox { id: exportQualityBox; from: 1; to: 100; value: 92; editable: true }
            }
            Label {
                Layout.fillWidth: true
                text: window.t(
                    "alpha.6 会嵌入目标 ICC 配置文件。当前预览仍以 ICC sRGB 为显示基准；原生宽色域工作数据直出将在后续 RAW Pipeline 阶段接入。",
                    "alpha.6 embeds the destination ICC profile. Preview remains ICC sRGB; native wide-gamut working-data export will be connected in a later RAW pipeline stage.")
                wrapMode: Text.WordWrap; color: "#7f8e9e"; font.pixelSize: 10
            }
        }
    }

    FileDialog {
        id: exportDialog
        title: window.t("导出 JPEG", "Export JPEG")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "jpg"
        nameFilters: ["JPEG (*.jpg *.jpeg)"]
        onAccepted: photoController.exportCurrent(selectedFile, window.exportSpaceKey(exportSpaceBox.currentIndex), exportQualityBox.value)
    }
    FolderDialog { id: projectFolder; title: window.t("选择项目上级文件夹", "Choose parent folder for the project"); onAccepted: projectNameDialog.open() }
    Dialog {
        id: projectNameDialog
        title: window.t("创建 JixelLight 项目", "Create JixelLight project")
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        anchors.centerIn: parent
        ColumnLayout {
            width: 360
            Label { text: window.t("项目名称", "Project name") }
            TextField { id: projectNameField; text: window.t("我的 JixelLight 项目", "My JixelLight Project"); Layout.fillWidth: true }
        }
        onAccepted: photoController.createProject(projectFolder.selectedFolder, projectNameField.text)
    }

    header: ToolBar {
        height: 54
        background: Rectangle { color: "#11161c"; border.color: "#27313c" }
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; spacing: 8
            Label { text: "JixelLight"; font.bold: true; font.pixelSize: 18; color: "#edf2f7"; Layout.rightMargin: 12 }
            Button { text: window.t("新建项目", "New Project"); onClicked: projectFolder.open() }
            Button { text: window.t("导入 RAW / 照片", "Import RAW / Photos"); onClicked: photoController.openImportDialog() }
            ToolSeparator {}
            Button { text: window.t("复制调整", "Copy"); enabled: photoController.hasImage; onClicked: photoController.copyAdjustments() }
            Button { text: window.t("粘贴调整", "Paste"); enabled: photoController.hasImage; onClicked: photoController.pasteAdjustments() }
            Button { text: window.t("同步全部", "Sync All"); enabled: photoController.hasImage; onClicked: photoController.syncAdjustmentsToAll() }
            ToolSeparator {}
            Button { text: window.t("导出 JPEG", "Export JPEG"); enabled: photoController.hasImage; onClicked: exportSettingsDialog.open() }
            Item { Layout.fillWidth: true }
            ComboBox {
                id: languageBox; Layout.preferredWidth: 105
                model: ["中文", "English"]
                currentIndex: photoController.language === "zh_CN" ? 0 : 1
                onActivated: photoController.setLanguage(currentIndex === 0 ? "zh_CN" : "en_US")
            }
            Button { text: window.t("🐞 报告当前问题", "🐞 Report problem"); onClicked: photoController.reportBugWithDialog() }
        }
    }

    footer: Rectangle {
        height: 34; color: "#11161c"; border.color: "#27313c"
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10; spacing: 10
            Label { text: photoController.statusMessage; color: "#9eabb9"; elide: Text.ElideMiddle; Layout.fillWidth: true; font.pixelSize: 11 }
            Label { visible: photoController.hasImage; text: photoController.pipelineDescription; color: "#687b8d"; elide: Text.ElideMiddle; Layout.maximumWidth: 600; font.pixelSize: 10 }
            Label { visible: photoController.hasImage; text: photoController.currentFormat; color: photoController.currentIsRaw ? "#7ee2c3" : "#8ca1b5"; font.bold: true; font.pixelSize: 11 }
        }
    }

    RowLayout {
        anchors.fill: parent; spacing: 1

        Rectangle {
            Layout.preferredWidth: 230; Layout.fillHeight: true; color: "#11161c"; border.color: "#252e38"
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 10; spacing: 8
                Label { text: window.t("图片库", "LIBRARY"); color: "#8e9aa8"; font.bold: true; font.pixelSize: 11 }
                Label { text: window.t(photoController.library.length + " 张照片", photoController.library.length + " photos"); color: "#c8d1dc"; font.pixelSize: 12 }
                ListView {
                    Layout.fillWidth: true; Layout.fillHeight: true; clip: true; model: photoController.library; spacing: 3
                    delegate: Rectangle {
                        required property var modelData
                        required property int index
                        width: ListView.view.width; height: 46; radius: 5
                        color: modelData.current ? "#24364d" : ma.containsMouse ? "#1b232c" : "transparent"
                        RowLayout {
                            anchors.fill: parent; anchors.margins: 7; spacing: 6
                            Rectangle {
                                Layout.preferredWidth: 34; Layout.preferredHeight: 20; radius: 4
                                color: modelData.raw ? "#173a34" : "#25303b"
                                Text { anchors.centerIn: parent; text: modelData.type; color: modelData.raw ? "#7ee2c3" : "#aebbc8"; font.pixelSize: 9; font.bold: true }
                            }
                            Text { Layout.fillWidth: true; text: modelData.name; color: modelData.current ? "#ffffff" : "#c2ccd7"; elide: Text.ElideMiddle; verticalAlignment: Text.AlignVCenter; font.pixelSize: 12 }
                        }
                        MouseArea { id: ma; anchors.fill: parent; hoverEnabled: true; onClicked: photoController.selectPhoto(index) }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true; Layout.fillHeight: true; color: "#080a0d"
            Item {
                anchors.fill: parent; anchors.margins: 18
                Image {
                    id: preview; anchors.fill: parent; source: photoController.previewUrl; cache: false; asynchronous: false
                    fillMode: Image.PreserveAspectFit; smooth: true; visible: photoController.hasImage
                }
                Rectangle {
                    visible: photoController.hasImage && photoController.currentIsRaw
                    anchors.left: parent.left; anchors.top: parent.top; width: 225; height: 30; radius: 6
                    color: "#142f2b"; border.color: "#2d7569"
                    Text { anchors.centerIn: parent; text: "RAW · Linear ProPhoto · 16-bit"; color: "#88ead0"; font.pixelSize: 11; font.bold: true }
                }
                Column {
                    anchors.centerIn: parent; visible: !photoController.hasImage; spacing: 10
                    Label { anchors.horizontalCenter: parent.horizontalCenter; text: "JixelLight"; color: "#dce5ef"; font.pixelSize: 28; font.bold: true }
                    Label { text: window.t("导入 RAW 照片开始后期", "Import RAW photos to start editing"); color: "#778594"; font.pixelSize: 14 }
                    Label { text: window.t("RAW 使用线性宽色域处理，不先渲染成 sRGB 成片", "RAW stays linear wide-gamut before display rendering"); color: "#657586"; font.pixelSize: 11 }
                    Label { text: window.t("支持拖放，或使用 Ctrl/Cmd + O", "Drop photos here or use Ctrl/Cmd + O"); color: "#4d5966"; font.pixelSize: 10 }
                }
            }
        }

        ScrollView {
            Layout.preferredWidth: 405; Layout.fillHeight: true; clip: true
            background: Rectangle { color: "#11161c"; border.color: "#252e38" }
            ColumnLayout {
                width: 386; x: 9; spacing: 10

                Label { text: window.t("专业示波器", "SCOPES"); color: "#8e9aa8"; font.bold: true; font.pixelSize: 11; Layout.topMargin: 10 }
                RowLayout {
                    Layout.fillWidth: true
                    Button { id: rgbButton; text: "RGB"; checkable: true; checked: true; onClicked: { checked = true; lumaButton.checked = false } }
                    Button { id: lumaButton; text: window.t("亮度", "Luma"); checkable: true; onClicked: { checked = true; rgbButton.checked = false } }
                    Item { Layout.fillWidth: true }
                    Label { text: "1024 bins · ICC sRGB Preview"; color: "#738293"; font.pixelSize: 10 }
                }
                HistogramView {
                    Layout.fillWidth: true; Layout.preferredHeight: 180
                    redData: photoController.redHistogram; greenData: photoController.greenHistogram; blueData: photoController.blueHistogram
                    lumaData: photoController.lumaHistogram; showLuma: lumaButton.checked
                }
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: window.t("阴影裁切  ", "Shadows clipped  ") + photoController.shadowClipPercent.toFixed(3) + "%"; color: photoController.shadowClipPercent > 0.1 ? "#ffb066" : "#8b98a6"; font.pixelSize: 10 }
                    Item { Layout.fillWidth: true }
                    Label { text: window.t("高光裁切  ", "Highlights  ") + photoController.highlightClipPercent.toFixed(3) + "%"; color: photoController.highlightClipPercent > 0.1 ? "#ff8e8e" : "#8b98a6"; font.pixelSize: 10 }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: "#29333e" }
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: window.t("RAW / 基础调整", "RAW / BASIC"); color: "#8e9aa8"; font.bold: true; font.pixelSize: 11 }
                    Item { Layout.fillWidth: true }
                    Button { text: window.t("全部重置", "Reset All"); enabled: photoController.hasImage; onClicked: photoController.resetAdjustments() }
                }
                AdjustmentSlider { Layout.fillWidth: true; label: window.t("曝光", "Exposure"); from: -5; to: 5; decimals: 2; value: photoController.exposure; onEdited: photoController.exposure = newValue }
                AdjustmentSlider { Layout.fillWidth: true; label: window.t("色温", "Temperature"); value: photoController.temperature; onEdited: photoController.temperature = newValue }
                AdjustmentSlider { Layout.fillWidth: true; label: window.t("色调", "Tint"); value: photoController.tint; onEdited: photoController.tint = newValue }
                AdjustmentSlider { Layout.fillWidth: true; label: window.t("对比度", "Contrast"); value: photoController.contrast; onEdited: photoController.contrast = newValue }
                AdjustmentSlider { Layout.fillWidth: true; label: window.t("高光", "Highlights"); value: photoController.highlights; onEdited: photoController.highlights = newValue }
                AdjustmentSlider { Layout.fillWidth: true; label: window.t("阴影", "Shadows"); value: photoController.shadows; onEdited: photoController.shadows = newValue }
                AdjustmentSlider { Layout.fillWidth: true; label: window.t("白色色阶", "Whites"); value: photoController.whites; onEdited: photoController.whites = newValue }
                AdjustmentSlider { Layout.fillWidth: true; label: window.t("黑色色阶", "Blacks"); value: photoController.blacks; onEdited: photoController.blacks = newValue }
                AdjustmentSlider { Layout.fillWidth: true; label: window.t("高光恢复", "Highlight Recovery"); from: 0; to: 100; value: photoController.highlightRecovery; onEdited: photoController.highlightRecovery = newValue }

                Rectangle { Layout.fillWidth: true; height: 1; color: "#29333e" }
                Label { text: window.t("颜色 / RAW 工作空间", "COLOR / RAW WORKING SPACE"); color: "#8e9aa8"; font.bold: true; font.pixelSize: 11 }
                AdjustmentSlider { Layout.fillWidth: true; label: window.t("色相", "Hue"); from: -180; to: 180; value: photoController.hue; onEdited: photoController.hue = newValue }
                AdjustmentSlider { Layout.fillWidth: true; label: window.t("饱和度", "Saturation"); value: photoController.saturation; onEdited: photoController.saturation = newValue }
                AdjustmentSlider { Layout.fillWidth: true; label: window.t("自然饱和度", "Vibrance"); value: photoController.vibrance; onEdited: photoController.vibrance = newValue }

                Rectangle { Layout.fillWidth: true; height: 1; color: "#29333e" }
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: window.t("HSL 颜色混合器", "HSL COLOR MIXER"); color: "#8e9aa8"; font.bold: true; font.pixelSize: 11 }
                    Item { Layout.fillWidth: true }
                    ComboBox {
                        id: mixerMode; Layout.preferredWidth: 105
                        model: [window.t("色相", "Hue"), window.t("饱和度", "Sat"), window.t("明度", "Luma")]
                    }
                }
                Repeater {
                    model: 8
                    delegate: AdjustmentSlider {
                        required property int index
                        Layout.fillWidth: true
                        label: [window.t("红色", "Red"), window.t("橙色", "Orange"), window.t("黄色", "Yellow"), window.t("绿色", "Green"), window.t("青色", "Aqua"), window.t("蓝色", "Blue"), window.t("紫色", "Purple"), window.t("洋红", "Magenta")][index]
                        value: mixerMode.currentIndex === 0 ? photoController.hslHue[index] : mixerMode.currentIndex === 1 ? photoController.hslSaturation[index] : photoController.hslLuminance[index]
                        onEdited: photoController.setColorMix(index, mixerMode.currentIndex, newValue)
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: "#29333e" }
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: window.t("曲线", "CURVES"); color: "#8e9aa8"; font.bold: true; font.pixelSize: 11 }
                    Item { Layout.fillWidth: true }
                    ComboBox { id: curveChannel; Layout.preferredWidth: 110; model: [window.t("主曲线", "Master"), "Red", "Green", "Blue"] }
                    Button { text: window.t("重置", "Reset"); onClicked: photoController.resetCurve(curveChannel.currentIndex) }
                }
                CurveEditor {
                    Layout.fillWidth: true; Layout.preferredHeight: 160
                    channel: curveChannel.currentIndex
                    values: curveChannel.currentIndex === 0 ? photoController.masterCurve : curveChannel.currentIndex === 1 ? photoController.redCurve : curveChannel.currentIndex === 2 ? photoController.greenCurve : photoController.blueCurve
                    onPointEdited: function(point, value) { photoController.setCurvePoint(channel, point, value) }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: "#29333e" }
                Label { text: window.t("照片信息 / EXIF", "PHOTO INFO / EXIF"); color: "#8e9aa8"; font.bold: true; font.pixelSize: 11 }
                GridLayout {
                    Layout.fillWidth: true; columns: 2; columnSpacing: 10; rowSpacing: 5
                    Label { text: window.t("相机", "Camera"); color: "#748394"; font.pixelSize: 10 }
                    Label { text: window.cameraName(); color: "#c4cfda"; elide: Text.ElideRight; Layout.fillWidth: true; font.pixelSize: 10 }
                    Label { text: window.t("镜头", "Lens"); color: "#748394"; font.pixelSize: 10 }
                    Label { text: window.meta("lens"); color: "#c4cfda"; elide: Text.ElideRight; Layout.fillWidth: true; font.pixelSize: 10 }
                    Label { text: window.t("快门", "Shutter"); color: "#748394"; font.pixelSize: 10 }
                    Label { text: window.meta("shutter"); color: "#c4cfda"; font.pixelSize: 10 }
                    Label { text: window.t("光圈", "Aperture"); color: "#748394"; font.pixelSize: 10 }
                    Label { text: window.meta("aperture"); color: "#c4cfda"; font.pixelSize: 10 }
                    Label { text: "ISO"; color: "#748394"; font.pixelSize: 10 }
                    Label { text: window.meta("iso"); color: "#c4cfda"; font.pixelSize: 10 }
                    Label { text: window.t("焦距", "Focal Length"); color: "#748394"; font.pixelSize: 10 }
                    Label { text: window.meta("focalLength"); color: "#c4cfda"; font.pixelSize: 10 }
                    Label { text: window.t("拍摄时间", "Captured"); color: "#748394"; font.pixelSize: 10 }
                    Label { text: window.meta("captureTime"); color: "#c4cfda"; elide: Text.ElideRight; Layout.fillWidth: true; font.pixelSize: 10 }
                    Label { text: window.t("尺寸", "Dimensions"); color: "#748394"; font.pixelSize: 10 }
                    Label { text: window.meta("pixelWidth") + " × " + window.meta("pixelHeight"); color: "#c4cfda"; font.pixelSize: 10 }
                    Label { visible: photoController.currentIsRaw; text: window.t("RAW 深度", "RAW Depth"); color: "#748394"; font.pixelSize: 10 }
                    Label { visible: photoController.currentIsRaw; text: window.meta("bitDepth") + "-bit"; color: "#88ead0"; font.pixelSize: 10 }
                    Label { visible: photoController.currentIsRaw; text: window.t("工作空间", "Working Space"); color: "#748394"; font.pixelSize: 10 }
                    Label { visible: photoController.currentIsRaw; text: window.meta("workingSpace"); color: "#88ead0"; font.pixelSize: 10 }
                    Label { visible: photoController.currentIsRaw; text: window.t("去马赛克", "Demosaic"); color: "#748394"; font.pixelSize: 10 }
                    Label { visible: photoController.currentIsRaw; text: window.meta("demosaic"); color: "#88ead0"; font.pixelSize: 10 }
                }

                Label {
                    Layout.fillWidth: true
                    text: window.t("处理顺序：RAW → Camera WB/Matrix → Linear ProPhoto → HSL/Color → Curves → ICC sRGB Preview", "Graph: RAW → Camera WB/Matrix → Linear ProPhoto → HSL/Color → Curves → ICC sRGB Preview")
                    wrapMode: Text.WordWrap; color: "#627180"; font.pixelSize: 10; Layout.bottomMargin: 18
                }
            }
        }
    }
}
