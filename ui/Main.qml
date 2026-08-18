import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ApplicationWindow {
    id: window; visible: true; width: 1480; height: 900; minimumWidth: 1120; minimumHeight: 700
    title: "JixelLight " + appVersion + (photoController.projectName ? " — " + photoController.projectName : "")
    color: "#0b0d10"

    FileDialog {
        id: importDialog; title: "Import photos"; fileMode: FileDialog.OpenFiles
        nameFilters: ["Images (*.jpg *.jpeg *.png *.bmp *.tif *.tiff *.webp)", "All files (*)"]
        onAccepted: photoController.importFiles(selectedFiles)
    }
    FileDialog {
        id: exportDialog; title: "Export JPEG"; fileMode: FileDialog.SaveFile; defaultSuffix: "jpg"; nameFilters: ["JPEG (*.jpg *.jpeg)"]
        onAccepted: photoController.exportCurrent(selectedFile)
    }
    FolderDialog { id: projectFolder; title: "Choose parent folder for the project"; onAccepted: projectNameDialog.open() }
    Dialog {
        id: projectNameDialog; title: "Create JixelLight project"; modal: true; standardButtons: Dialog.Ok | Dialog.Cancel
        anchors.centerIn: parent
        ColumnLayout { width: 360; Label { text: "Project name" } TextField { id: projectNameField; text: "My JixelLight Project"; Layout.fillWidth: true } }
        onAccepted: photoController.createProject(projectFolder.selectedFolder, projectNameField.text)
    }

    header: ToolBar {
        height: 54; background: Rectangle { color: "#11161c"; border.color: "#27313c" }
        RowLayout { anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; spacing: 8
            Label { text: "JixelLight"; font.bold: true; font.pixelSize: 18; color: "#edf2f7"; Layout.rightMargin: 12 }
            Button { text: "New Project"; onClicked: projectFolder.open() }
            Button { text: "Import"; onClicked: importDialog.open() }
            ToolSeparator {}
            Button { text: "Copy"; enabled: photoController.hasImage; onClicked: photoController.copyAdjustments() }
            Button { text: "Paste"; enabled: photoController.hasImage; onClicked: photoController.pasteAdjustments() }
            Button { text: "Sync All"; enabled: photoController.hasImage; onClicked: photoController.syncAdjustmentsToAll() }
            ToolSeparator {}
            Button { text: "Export JPEG"; enabled: photoController.hasImage; onClicked: exportDialog.open() }
            Item { Layout.fillWidth: true }
            Button { text: "🐞 Report current problem"; onClicked: photoController.reportBug() }
        }
    }

    footer: Rectangle {
        height: 30; color: "#11161c"; border.color: "#27313c"
        RowLayout { anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10
            Label { text: photoController.statusMessage; color: "#9eabb9"; elide: Text.ElideMiddle; Layout.fillWidth: true; font.pixelSize: 11 }
            Label { text: photoController.currentFile; color: "#6f7d8b"; elide: Text.ElideMiddle; Layout.maximumWidth: 520; font.pixelSize: 11 }
        }
    }

    RowLayout {
        anchors.fill: parent; spacing: 1

        Rectangle {
            Layout.preferredWidth: 220; Layout.fillHeight: true; color: "#11161c"; border.color: "#252e38"
            ColumnLayout { anchors.fill: parent; anchors.margins: 10; spacing: 8
                Label { text: "LIBRARY"; color: "#8e9aa8"; font.bold: true; font.pixelSize: 11 }
                Label { text: photoController.library.length + " photos"; color: "#c8d1dc"; font.pixelSize: 12 }
                ListView {
                    Layout.fillWidth: true; Layout.fillHeight: true; clip: true; model: photoController.library; spacing: 3
                    delegate: Rectangle {
                        required property var modelData; required property int index
                        width: ListView.view.width; height: 42; radius: 5; color: modelData.current ? "#24364d" : ma.containsMouse ? "#1b232c" : "transparent"
                        Text { anchors.fill: parent; anchors.margins: 8; text: modelData.name; color: modelData.current ? "#ffffff" : "#c2ccd7"; elide: Text.ElideMiddle; verticalAlignment: Text.AlignVCenter; font.pixelSize: 12 }
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
                Column { anchors.centerIn: parent; visible: !photoController.hasImage; spacing: 10
                    Label { anchors.horizontalCenter: parent.horizontalCenter; text: "JixelLight"; color: "#dce5ef"; font.pixelSize: 28; font.bold: true }
                    Label { text: "Import photos to start editing"; color: "#778594"; font.pixelSize: 14 }
                }
            }
        }

        ScrollView {
            Layout.preferredWidth: 340; Layout.fillHeight: true; clip: true
            background: Rectangle { color: "#11161c"; border.color: "#252e38" }
            ColumnLayout {
                width: 322; x: 9; spacing: 10
                Label { text: "SCOPES"; color: "#8e9aa8"; font.bold: true; font.pixelSize: 11; Layout.topMargin: 10 }
                RowLayout { Layout.fillWidth: true
                    Button { id: rgbButton; text: "RGB"; checkable: true; checked: true; onClicked: { checked=true; lumaButton.checked=false } }
                    Button { id: lumaButton; text: "Luma"; checkable: true; onClicked: { checked=true; rgbButton.checked=false } }
                    Item { Layout.fillWidth: true }
                    Label { text: "1024 bins"; color: "#738293"; font.pixelSize: 10 }
                }
                HistogramView { Layout.fillWidth: true; Layout.preferredHeight: 180; redData: photoController.redHistogram; greenData: photoController.greenHistogram; blueData: photoController.blueHistogram; lumaData: photoController.lumaHistogram; showLuma: lumaButton.checked }
                RowLayout { Layout.fillWidth: true
                    Label { text: "Shadows clipped  " + photoController.shadowClipPercent.toFixed(3) + "%"; color: photoController.shadowClipPercent > 0.1 ? "#ffb066" : "#8b98a6"; font.pixelSize: 10 }
                    Item { Layout.fillWidth: true }
                    Label { text: "Highlights  " + photoController.highlightClipPercent.toFixed(3) + "%"; color: photoController.highlightClipPercent > 0.1 ? "#ff8e8e" : "#8b98a6"; font.pixelSize: 10 }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: "#29333e" }
                RowLayout { Layout.fillWidth: true
                    Label { text: "BASIC"; color: "#8e9aa8"; font.bold: true; font.pixelSize: 11 }
                    Item { Layout.fillWidth: true }
                    Button { text: "Reset"; enabled: photoController.hasImage; onClicked: photoController.resetAdjustments() }
                }
                AdjustmentSlider { Layout.fillWidth: true; label: "Exposure"; from: -5; to: 5; decimals: 2; value: photoController.exposure; onEdited: photoController.exposure = newValue }
                AdjustmentSlider { Layout.fillWidth: true; label: "Temp"; value: photoController.temperature; onEdited: photoController.temperature = newValue }
                AdjustmentSlider { Layout.fillWidth: true; label: "Tint"; value: photoController.tint; onEdited: photoController.tint = newValue }
                AdjustmentSlider { Layout.fillWidth: true; label: "Contrast"; value: photoController.contrast; onEdited: photoController.contrast = newValue }
                AdjustmentSlider { Layout.fillWidth: true; label: "Highlights"; value: photoController.highlights; onEdited: photoController.highlights = newValue }
                AdjustmentSlider { Layout.fillWidth: true; label: "Shadows"; value: photoController.shadows; onEdited: photoController.shadows = newValue }
                AdjustmentSlider { Layout.fillWidth: true; label: "Whites"; value: photoController.whites; onEdited: photoController.whites = newValue }
                AdjustmentSlider { Layout.fillWidth: true; label: "Blacks"; value: photoController.blacks; onEdited: photoController.blacks = newValue }
                Item { Layout.preferredHeight: 20 }
            }
        }
    }
}
