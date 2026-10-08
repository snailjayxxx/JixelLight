import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtCore

ApplicationWindow {
    id: window
    visible: true
    width: 1540; height: 920; minimumWidth: 1180; minimumHeight: 720
    title: "JixelLight " + appVersion + (photoController.projectName ? " — " + photoController.projectName : "")
    color: "#0b0d10"
    palette.window: "#11161c"
    palette.base: "#19232e"
    palette.text: "#d7dde6"
    palette.windowText: "#d7dde6"
    palette.button: "#263442"
    palette.buttonText: "#d7dde6"
    palette.highlight: "#507aa5"
    palette.highlightedText: "#ffffff"
    palette.placeholderText: "#8b98a6"
    onClosing: function(close) { if (!photoController.flushEdits()) close.accepted=false }

    // "Develop" remains the default to keep existing GPU/RAW smoke behavior.
    property int workspaceIndex: 1 // 0 Library, 1 Develop
    property bool leftPanelVisible: true
    property bool rightPanelVisible: true
    property bool filmstripVisible: true

    Settings {
        id: workspaceSettings
        category: "WorkspaceLayout"
        property alias workspaceIndex: window.workspaceIndex
        property alias leftPanelVisible: window.leftPanelVisible
        property alias rightPanelVisible: window.rightPanelVisible
        property alias filmstripVisible: window.filmstripVisible
    }
    Component.onCompleted: workspaceSplit.restoreState(workspaceSettings.value("splitViewState"))
    Component.onDestruction: workspaceSettings.setValue("splitViewState", workspaceSplit.saveState())
    Shortcut { sequence: "G"; context: Qt.ApplicationShortcut; onActivated: window.workspaceIndex = 0 }
    Shortcut { sequence: "D"; context: Qt.ApplicationShortcut; onActivated: window.workspaceIndex = 1 }


    function t(zh, en) { return photoController.language === "zh_CN" ? zh : en }
    function localizeDialogButtons(dialog) {
        const ok = dialog.standardButton(Dialog.Ok)
        const cancel = dialog.standardButton(Dialog.Cancel)
        if (ok) ok.text = t("确定", "OK")
        if (cancel) cancel.text = t("取消", "Cancel")
    }
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

    Shortcut { sequences: [StandardKey.Undo]; enabled: photoController.canUndo; onActivated: photoController.undo() }
    Shortcut { sequences: [StandardKey.Redo]; enabled: photoController.canRedo; onActivated: photoController.redo() }
    Shortcut { sequence: StandardKey.Open; onActivated: photoController.openImportDialog() }

    DropArea {
        anchors.fill: parent
        onDropped: function(drop) {
            if (!drop.urls || drop.urls.length === 0) return
            photoController.importFiles(drop.urls)
            drop.acceptProposedAction()
        }
    }

    Dialog {
        id: exportSettingsDialog
        title: window.t("导出设置", "Export Settings")
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        anchors.centerIn: parent
        property bool batchMode: false
        onOpened: window.localizeDialogButtons(exportSettingsDialog)
        onAccepted: batchMode ? batchFolder.open() : exportDialog.open()
        ColumnLayout {
            width: 430; spacing: 12
            Label { text: window.t("输出色彩空间 / ICC", "Output Color Space / ICC"); color: "#d6dee8"; font.bold: true }
            ComboBox {
                id: exportSpaceBox
                Layout.fillWidth: true
                model: ["sRGB", "Display P3", "Adobe RGB (1998)", "ProPhoto RGB"]
                currentIndex: 0
            }
            Label { visible: exportSettingsDialog.batchMode; text: window.t("批量输出格式", "Batch output format"); color: "#d6dee8" }
            ComboBox {
                id: batchFormatBox
                visible: exportSettingsDialog.batchMode
                Layout.fillWidth: true
                model: ["JPEG", "PNG 16-bit"]
            }
            RowLayout {
                Layout.fillWidth: true
                Label { text: window.t("JPEG 质量", "JPEG Quality"); color: "#d6dee8" }
                Item { Layout.fillWidth: true }
                SpinBox {
                    id: exportQualityBox
                    from: 1; to: 100; value: 92; editable: true
                    Layout.preferredWidth: 132
                    Layout.minimumWidth: 132
                }
            }
            Label {
                Layout.fillWidth: true
                text: window.t(
                    "从线性宽色域数据直接导出，嵌入目标 ICC。JPEG 使用分块导出；16-bit PNG 使用完整帧内存。不覆盖原始照片。",
                    "Export directly from linear wide-gamut data with a target ICC. JPEG is tiled; 16-bit PNG uses full-frame memory. Originals are protected.")
                wrapMode: Text.WordWrap; color: "#7f8e9e"; font.pixelSize: 10
            }
        }
    }

    FileDialog {
        id: exportDialog
        title: window.t("导出照片", "Export Photo")
        fileMode: FileDialog.SaveFile
        defaultSuffix: selectedNameFilter.index === 1 ? "png" : "jpg"
        nameFilters: ["JPEG (*.jpg *.jpeg)", "PNG 16-bit (*.png)"]
        onAccepted: photoController.exportCurrent(selectedFile, window.exportSpaceKey(exportSpaceBox.currentIndex), exportQualityBox.value)
    }
    FolderDialog { id: batchFolder; title: window.t("批量导出文件夹", "Batch export folder"); onAccepted: photoController.exportAll(selectedFolder, window.exportSpaceKey(exportSpaceBox.currentIndex), exportQualityBox.value, batchFormatBox.currentIndex === 1 ? "png" : "jpeg") }
    FolderDialog { id: projectFolder; title: window.t("选择项目上级文件夹", "Choose parent folder for the project"); onAccepted: projectNameDialog.open() }
    FolderDialog { id: openProjectFolder; title: window.t("选择现有 .jlp 项目文件夹", "Select an existing .jlp project folder"); onAccepted: photoController.openProject(selectedFolder) }
    Dialog {
        id: projectNameDialog
        title: window.t("创建 JixelLight 项目", "Create JixelLight project")
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        anchors.centerIn: parent
        onOpened: window.localizeDialogButtons(projectNameDialog)
        ColumnLayout {
            width: 360
            Label { text: window.t("项目名称", "Project name") }
            TextField { id: projectNameField; text: window.t("我的 JixelLight 项目", "My JixelLight Project"); Layout.fillWidth: true }
        }
        onAccepted: photoController.createProject(projectFolder.selectedFolder, projectNameField.text)
    }


    header: ToolBar {
        height: 56
        background: Rectangle { color: "#171c23"; border.color: "#303943" }
        Menu {
            id: projectActions
            MenuItem { text: window.t("新建项目", "New Project"); onTriggered: projectFolder.open() }
            MenuItem { text: window.t("打开已有项目", "Open Existing Project"); onTriggered: openProjectFolder.open() }
        }
        Menu {
            id: exportActions
            MenuItem {
                text: window.t("导出当前照片", "Export Current Photo")
                enabled: photoController.hasImage && !photoController.exportBusy
                onTriggered: { exportSettingsDialog.batchMode = false; exportSettingsDialog.open() }
            }
            MenuItem {
                text: window.t("批量导出照片", "Batch Export Photos")
                enabled: photoController.hasImage && !photoController.exportBusy
                onTriggered: { exportSettingsDialog.batchMode = true; exportSettingsDialog.open() }
            }
        }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            spacing: 5
            Label { text: "JixelLight"; color: "#e8eff7"; font.pixelSize: 18; font.bold: true; Layout.rightMargin: 8 }
            ToolButton {
                objectName: "libraryModeButton"
                text: window.t("图库", "Library")
                checkable: true
                checked: window.workspaceIndex === 0
                onClicked: window.workspaceIndex = 0
            }
            ToolButton {
                objectName: "developModeButton"
                text: window.t("修改照片", "Develop")
                checkable: true
                checked: window.workspaceIndex === 1
                onClicked: window.workspaceIndex = 1
            }
            ToolSeparator {}
            ToolButton {
                text: window.t("项目 ▾", "Project ▾")
                onClicked: projectActions.popup()
            }
            ToolButton {
                text: window.t("导入", "Import")
                onClicked: photoController.openImportDialog()
            }
            ToolSeparator {}
            ToolButton { text: window.t("撤销", "Undo"); enabled: photoController.canUndo; onClicked: photoController.undo() }
            ToolButton { text: window.t("重做", "Redo"); enabled: photoController.canRedo; onClicked: photoController.redo() }
            ToolButton {
                text: window.t("复制", "Copy")
                enabled: photoController.hasImage
                onClicked: photoController.copyAdjustments()
            }
            ToolButton {
                text: window.t("粘贴", "Paste")
                enabled: photoController.hasImage
                onClicked: photoController.pasteAdjustments()
            }
            ToolButton {
                text: window.t("同步全部", "Sync All")
                enabled: photoController.hasImage
                onClicked: photoController.syncAdjustmentsToAll()
            }
            Item { Layout.fillWidth: true }
            ToolButton { text: window.t("导出 ▾", "Export ▾"); onClicked: exportActions.popup() }
            ToolButton {
                visible: photoController.exportBusy
                text: window.t("取消导出", "Cancel Export")
                onClicked: photoController.cancelExport()
            }
            ToolButton {
                text: window.leftPanelVisible ? "◧" : "◨"
                onClicked: window.leftPanelVisible = !window.leftPanelVisible
                ToolTip.visible: hovered
                ToolTip.text: window.t("切换左侧边栏", "Toggle left sidebar")
            }
            ToolButton {
                text: window.filmstripVisible ? "▤" : "▥"
                onClicked: window.filmstripVisible = !window.filmstripVisible
                ToolTip.visible: hovered
                ToolTip.text: window.t("切换底部胶片条", "Toggle filmstrip")
            }
            ToolButton {
                visible: window.workspaceIndex === 1
                text: window.rightPanelVisible ? "◨" : "◧"
                onClicked: window.rightPanelVisible = !window.rightPanelVisible
                ToolTip.visible: hovered
                ToolTip.text: window.t("切换调色面板", "Toggle develop panel")
            }
            ComboBox {
                Layout.preferredWidth: 92
                model: ["中文", "English"]
                currentIndex: photoController.language === "zh_CN" ? 0 : 1
                onActivated: photoController.setLanguage(currentIndex === 0 ? "zh_CN" : "en_US")
            }
            ToolButton {
                text: window.t("🐞 报告问题", "🐞 Report")
                onClicked: photoController.reportBugWithDialog()
            }
        }
    }

    footer: Rectangle {
        height: 32; color: "#15191f"; border.color: "#303943"
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10; spacing: 10
            ProgressBar { visible: photoController.exportBusy; value: photoController.exportProgress; Layout.preferredWidth: 120 }
            CheckBox { text: "GPU"; checked: photoController.gpuEnabled; onToggled: photoController.gpuEnabled = checked }
            Label { text: photoController.statusMessage; color: "#9eabb9"; elide: Text.ElideMiddle; Layout.fillWidth: true; font.pixelSize: 11 }
            Label { visible: photoController.hasImage; text: photoController.pipelineDescription; color: "#687b8d"; elide: Text.ElideMiddle; Layout.maximumWidth: 500; font.pixelSize: 10 }
            Label { visible: photoController.hasImage; text: photoController.currentFormat; color: photoController.currentIsRaw ? "#7ee2c3" : "#8ca1b5"; font.bold: true; font.pixelSize: 11 }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        SplitView {
            id: workspaceSplit
            objectName: "workspaceSplit"
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal

            LibrarySidebar {
                objectName: "librarySidebar"
                controller: photoController
                workspaceIndex: window.workspaceIndex
                visible: window.leftPanelVisible
                SplitView.preferredWidth: 228
                SplitView.minimumWidth: 182
                SplitView.maximumWidth: 390
                onEditRequested: window.workspaceIndex = 1
            }

            Item {
                SplitView.fillWidth: true
                SplitView.minimumWidth: 400
                Loader {
                    id: libraryPage
                    anchors.fill: parent
                    active: window.workspaceIndex === 0
                    sourceComponent: Component {
                        LibraryWorkspace {
                            controller: photoController
                            onEditRequested: window.workspaceIndex = 1
                        }
                    }
                }
                Loader {
                    id: developPage
                    anchors.fill: parent
                    active: window.workspaceIndex === 1
                    sourceComponent: Component {
                        Rectangle {
                            anchors.fill: parent
                            color: "#080a0d"
            Item {
                anchors.fill: parent; anchors.margins: 18
                RowLayout {
                    anchors.fill: parent; spacing: 12; visible: photoController.hasImage
                    ColumnLayout {
                        Layout.fillWidth: true; Layout.fillHeight: true; Layout.preferredWidth: 1; Layout.minimumWidth: 0
                        Label { Layout.fillWidth: true; wrapMode: Text.Wrap; text: window.t("当前 RAW / 编辑结果", "Current RAW / edit result"); color: "#aebccc" }
                        PhotoCanvas { objectName: "photoCanvas"; Layout.fillWidth: true; Layout.fillHeight: true; controller: photoController }
                    }
                    CameraReferenceView { controller: photoController; visible: photoController.showCameraReference; Layout.fillWidth: true; Layout.fillHeight: true; Layout.preferredWidth: 1; Layout.minimumWidth: 0 }
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
                    }
                }
            }

            DevelopRightPanel {
                id: rightDevelop
                objectName: "developRightPanel"
                controller: photoController
                visible: window.workspaceIndex === 1 && window.rightPanelVisible
                SplitView.preferredWidth: 390
                SplitView.minimumWidth: 320
                SplitView.maximumWidth: 540
            }
        }
        Filmstrip {
            id: filmstrip
            objectName: "jixelMainFilmstrip"
            controller: photoController
            visible: window.filmstripVisible && photoController.library.length > 0
            Layout.fillWidth: true
            Layout.preferredHeight: 124
            onEditRequested: window.workspaceIndex = 1
        }
    }
}
