import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ApplicationWindow {
    id: window
    visible: true
    width: 1480; height: 900; minimumWidth: 1120; minimumHeight: 700
    title: "JixelLight " + appVersion + (photoController.projectName ? " — " + photoController.projectName : "")
    color: "#0b0d10"

    function t(zh, en) { return photoController.language === "zh_CN" ? zh : en }

    Shortcut {
        sequence: StandardKey.Open
        onActivated: photoController.openImportDialog()
    }

    DropArea {
        anchors.fill: parent
        onDropped: function(drop) {
            if (!drop.urls || drop.urls.length === 0)
                return
            for (let i = 0; i < drop.urls.length; ++i)
                photoController.importFile(drop.urls[i])
            drop.acceptProposedAction()
        }
    }

    FileDialog {
        id: exportDialog; title: window.t("导出 JPEG", "Export JPEG"); fileMode: FileDialog.SaveFile; defaultSuffix: "jpg"; nameFilters: ["JPEG (*.jpg *.jpeg)"]
        onAccepted: photoController.exportCurrent(selectedFile)
    }
    FolderDialog { id: projectFolder; title: window.t("选择项目上级文件夹", "Choose parent folder for the project"); onAccepted: projectNameDialog.open() }
    Dialog {
        id: projectNameDialog; title: window.t("创建 JixelLight 项目", "Create JixelLight project"); modal: true; standardButtons: Dialog.Ok | Dialog.Cancel
        anchors.centerIn: parent
        ColumnLayout { width: 360; Label { text: window.t("项目名称", "Project name") } TextField { id: projectNameField; text: window.t("我的 JixelLight 项目", "My JixelLight Project"); Layout.fillWidth: true } }
        onAccepted: photoController.createProject(projectFolder.selectedFolder, projectNameField.text)
    }

    header: ToolBar {
        height: 54; background: Rectangle { color: "#11161c"; border.color: "#27313c" }
        RowLayout { anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; spacing: 8
            Label { text: "JixelLight"; font.bold: true; font.pixelSize: 18; color: "#edf2f7"; Layout.rightMargin: 12 }
            Button { text: window.t("新建项目", "New Project"); onClicked: projectFolder.open() }
            Button { text: window.t("导入 RAW / 照片", "Import RAW / Photos"); onClicked: photoController.openImportDialog() }
            ToolSeparator {}
            Button { text: window.t("复制调整", "Copy"); enabled: photoController.hasImage; onClicked: photoController.copyAdjustments() }
            Button { text: window.t("粘贴调整", "Paste"); enabled: photoController.hasImage; onClicked: photoController.pasteAdjustments() }
            Button { text: window.t("同步全部", "Sync All"); enabled: photoController.hasImage; onClicked: photoController.syncAdjustmentsToAll() }
            ToolSeparator {}
            Button { text: window.t("导出 JPEG", "Export JPEG"); enabled: photoController.hasImage; onClicked: exportDialog.open() }
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
        height: 30; color: "#11161c"; border.color: "#27313c"
        RowLayout { anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10
            Label { text: photoController.statusMessage; color: "#9eabb9"; elide: Text.ElideMiddle; Layout.fillWidth: true; font.pixelSize: 11 }
            Label { visible: photoController.hasImage; text: photoController.currentFormat; color: photoController.currentIsRaw ? "#7ee2c3" : "#8ca1b5"; font.bold: true; font.pixelSize: 11 }
            Label { text: photoController.currentFile; color: "#6f7d8b"; elide: Text.ElideMiddle; Layout.maximumWidth: 520; font.pixelSize: 11 }
        }
    }

    RowLayout {
        anchors.fill: parent; spacing: 1

        Rectangle {
            Layout.preferredWidth: 230; Layout.fillHeight: true; color: "#11161c"; border.color: "#252e38"
            ColumnLayout { anchors.fill: parent; anchors.margins: 10; spacing: 8
                Label { text: window.t("图片库", "LIBRARY"); color: "#8e9aa8"; font.bold: true; font.pixelSize: 11 }
                Label { text: window.t(photoController.library.length + " 张照片", photoController.library.length + " photos"); color: "#c8d1dc"; font.pixelSize: 12 }
                ListView {
                    Layout.fillWidth: true; Layout.fillHeight: true; clip: true; model: photoController.library; spacing: 3
                    delegate: Rectangle {
                        required property var modelData; required property int index
                        width: ListView.view.width; height: 46; radius: 5; color: modelData.current ? "#24364d" : ma.containsMouse ? "#1b232c" : "transparent"
                        RowLayout { anchors.fill: parent; anchors.margins: 7; spacing: 6
                            Rectangle { Layout.preferredWidth: 34; Layout.preferredHeight: 20; radius: 4; color: modelData.raw ? "#173a34" : "#25303b"
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
            Item { anchors.fill: parent; anchors.margins: 18
                Image {
                    id: preview; anchors.fill: parent; source: photoController.previewUrl; cache: false; asynchronous: false
                    fillMode: Image.PreserveAspectFit; smooth: true; visible: photoController.hasImage
                }
                Rectangle {
                    visible: photoController.hasImage && photoController.currentIsRaw
                    anchors.left: parent.left; anchors.top: parent.top; width: 82; height: 28; radius: 6; color: "#142f2b"; border.color: "#2d7569"
                    Text { anchors.centerIn: parent; text: "RAW · 16-bit"; color: "#88ead0"; font.pixelSize: 11; font.bold: true }
                }
                Column { anchors.centerIn: parent; visible: !photoController.hasImage; spacing: 10
                    Label { anchors.horizontalCenter: parent.horizontalCenter; text: "JixelLight"; color: "#dce5ef"; font.pixelSize: 28; font.bold: true }
                    Label { text: window.t("导入 RAW 照片开始后期", "Import RAW photos to start editing"); color: "#778594"; font.pixelSize: 14 }
                    Label { text: window.t("支持 ARW / CR2 / CR3 / NEF / RAF / RW2 / ORF / DNG 等", "ARW / CR2 / CR3 / NEF / RAF / RW2 / ORF / DNG and more"); color: "#586675"; font.pixelSize: 11 }
                    Label { text: window.t("也可以拖放照片到窗口，或使用 Ctrl/Cmd + O", "You can also drop photos here or use Ctrl/Cmd + O"); color: "#4d5966"; font.pixelSize: 10 }
                }
            }
        }

        ScrollView {
            Layout.preferredWidth: 350; Layout.fillHeight: true; clip: true
            background: Rectangle { color: "#11161c"; border.color: "#252e38" }
            ColumnLayout {
                width: 332; x: 9; spacing: 10
                Label { text: window.t("专业示波器", "SCOPES"); color: "#8e9aa8"; font.bold: true; font.pixelSize: 11; Layout.topMargin: 10 }
                RowLayout { Layout.fillWidth: true
                    Button { id: rgbButton; text: "RGB"; checkable: true; checked: true; onClicked: { checked=true; lumaButton.checked=false } }
                    Button { id: lumaButton; text: window.t("亮度", "Luma"); checkable: true; onClicked: { checked=true; rgbButton.checked=false } }
                    Item { Layout.fillWidth: true }
                    Label { text: "1024 bins · 16-bit"; color: "#738293"; font.pixelSize: 10 }
                }
                HistogramView { Layout.fillWidth: true; Layout.preferredHeight: 180; redData: photoController.redHistogram; greenData: photoController.greenHistogram; blueData: photoController.blueHistogram; lumaData: photoController.lumaHistogram; showLuma: lumaButton.checked }
                RowLayout { Layout.fillWidth: true
                    Label { text: window.t("阴影裁切  ", "Shadows clipped  ") + photoController.shadowClipPercent.toFixed(3) + "%"; color: photoController.shadowClipPercent > 0.1 ? "#ffb066" : "#8b98a6"; font.pixelSize: 10 }
                    Item { Layout.fillWidth: true }
                    Label { text: window.t("高光裁切  ", "Highlights  ") + photoController.highlightClipPercent.toFixed(3) + "%"; color: photoController.highlightClipPercent > 0.1 ? "#ff8e8e" : "#8b98a6"; font.pixelSize: 10 }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: "#29333e" }
                RowLayout { Layout.fillWidth: true
                    Label { text: window.t("基础调整", "BASIC"); color: "#8e9aa8"; font.bold: true; font.pixelSize: 11 }
                    Item { Layout.fillWidth: true }
                    Button { text: window.t("重置", "Reset"); enabled: photoController.hasImage; onClicked: photoController.resetAdjustments() }
                }
                AdjustmentSlider { Layout.fillWidth: true; label: window.t("曝光", "Exposure"); from: -5; to: 5; decimals: 2; value: photoController.exposure; onEdited: photoController.exposure = newValue }
                AdjustmentSlider { Layout.fillWidth: true; label: window.t("色温", "Temp"); value: photoController.temperature; onEdited: photoController.temperature = newValue }
                AdjustmentSlider { Layout.fillWidth: true; label: window.t("色调", "Tint"); value: photoController.tint; onEdited: photoController.tint = newValue }
                AdjustmentSlider { Layout.fillWidth: true; label: window.t("对比度", "Contrast"); value: photoController.contrast; onEdited: photoController.contrast = newValue }
                AdjustmentSlider { Layout.fillWidth: true; label: window.t("高光", "Highlights"); value: photoController.highlights; onEdited: photoController.highlights = newValue }
                AdjustmentSlider { Layout.fillWidth: true; label: window.t("阴影", "Shadows"); value: photoController.shadows; onEdited: photoController.shadows = newValue }
                AdjustmentSlider { Layout.fillWidth: true; label: window.t("白色色阶", "Whites"); value: photoController.whites; onEdited: photoController.whites = newValue }
                AdjustmentSlider { Layout.fillWidth: true; label: window.t("黑色色阶", "Blacks"); value: photoController.blacks; onEdited: photoController.blacks = newValue }
                Item { Layout.preferredHeight: 20 }
            }
        }
    }
}
