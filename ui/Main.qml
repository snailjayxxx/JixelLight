import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtCore

ApplicationWindow {
    id: window
    // Private candidate: production clipboard reader; tests may substitute a race proxy.
    property var recipeClipboardAdapter: recipeClipboard
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
    onClosing: function(close) { if (!photoController.prepareToClose()) close.accepted=false }

    // "Develop" remains the default to keep existing GPU/RAW smoke behavior.
    property int workspaceIndex: 1 // 0 Library, 1 Develop
    property bool leftPanelVisible: true
    property bool rightPanelVisible: true
    property bool filmstripVisible: true
    // Editor choices are UI state; only completed strokes enter photo history.
    QtObject {
        id: brushEditorState
        property bool active: false
        property string layerId: ""
        property string mode: "paint"
        property var strokeSelection: ({})
        onActiveChanged: if (!active) strokeSelection=({})
        property real radius: 0.05
        property real hardness: 0.6
        property real opacity: 1
        property real exposure: 1
    }

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
    Shortcut { sequence: "G"; context: Qt.ApplicationShortcut; enabled: !editRecipeDialog.visible; onActivated: window.workspaceIndex = 0 }
    Shortcut { sequence: "D"; context: Qt.ApplicationShortcut; enabled: !editRecipeDialog.visible; onActivated: window.workspaceIndex = 1 }


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

    Dialog {
        id: editRecipeDialog
        objectName: "editRecipeDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(window.width - 32, 720)
        height: Math.min(window.height - 40, 560)
        modal: true
        focus: true
        title: window.t("应用编辑配方", "Apply edit recipe")
        closePolicy: Popup.CloseOnEscape
        property bool targetCurrent: false
        property string targetProject: ""
        property string targetFile: ""
        property string errorText: ""
        // Private prototype: retain the exact clipboard snapshot separately from the read-only preview.
        property string recipeText: ""
        function previewText(value) {
            let end = Math.min(value.length, 4096)
            if (end < value.length && end > 0) {
                const last = value.charCodeAt(end - 1), next = value.charCodeAt(end)
                if (last >= 0xd800 && last <= 0xdbff && next >= 0xdc00 && next <= 0xdfff) --end
            }
            return value.slice(0, end).replace(/\u000d/g, "\\u000D").replace(/\u00a0/g, "\\u00A0").replace(/\u2028/g, "\\u2028").replace(/\u2029/g, "\\u2029")
        }
        function pasteRecipe() {
            if (!window.recipeClipboardAdapter) {
                errorText = window.t("剪贴板不可用。", "Clipboard unavailable.")
                return
            }
            const result = window.recipeClipboardAdapter.snapshot()
            if (result.status !== "ok") {
                errorText = result.status === "too-long"
                    ? window.t("配方文本过长。", "The recipe text is too long.")
                    : window.t("剪贴板不可用。", "Clipboard unavailable.")
                return
            }
            recipeText = result.text
            errorText = ""
        }
        function applyRecipe() {
            errorText = ""
            if (!targetCurrent || !photoController.currentSourceAvailable) {
                errorText = window.t("当前版本已变更，请重新打开此窗口。", "The current version changed. Reopen this dialog.")
                return
            }
            if (recipeText.length > 1048576) {
                errorText = window.t("配方文本过长。", "The recipe text is too long.")
                return
            }
            let commands
            try {
                commands = JSON.parse(recipeText)
            } catch (error) {
                errorText = window.t("无法读取 JSON 配方：", "Cannot read the JSON recipe: ") + error.message
                return
            }
            if (!Array.isArray(commands) || commands.length < 1 || commands.length > 10000) {
                errorText = window.t("配方必须是包含 1 至 10000 条编辑命令的数组。", "Use an array of 1 to 10000 edit commands.")
                return
            }
            if (!photoController.executeEditCommands(commands)) {
                errorText = photoController.statusMessage
                return
            }
            close()
        }
        onOpened: {
            targetProject = photoController.projectPath
            targetFile = photoController.currentFile
            targetCurrent = photoController.currentSourceAvailable
            errorText = ""
            recipeText = ""
            editRecipeText.forceActiveFocus()
        }
        Connections {
            target: photoController
            function onCurrentIndexChanged() { if (editRecipeDialog.visible) editRecipeDialog.targetCurrent = false }
            function onProjectChanged() {
                if (editRecipeDialog.visible && photoController.projectPath !== editRecipeDialog.targetProject)
                    editRecipeDialog.targetCurrent = false
            }
        }
        contentItem: ColumnLayout {
            spacing: 10
            Label {
                Layout.fillWidth: true
                text: window.t("粘贴离线命令行使用的 JSON 编辑命令数组。所有命令成功后一次应用，可一步撤销。",
                    "Paste a JSON edit-command array used by the offline CLI. All commands apply together and can be undone in one step.")
                wrapMode: Text.WordWrap
            }
            Label {
                Layout.fillWidth: true
                text: editRecipeDialog.targetFile
                wrapMode: Text.WrapAnywhere
                maximumLineCount: 2
                elide: Text.ElideMiddle
                color: "#a3b2c1"
            }
            RowLayout {
                Layout.fillWidth: true
                Button {
                    objectName: "editRecipePaste"
                    text: window.t("粘贴配方", "Paste recipe")
                    onClicked: editRecipeDialog.pasteRecipe()
                }
                Label {
                    objectName: "editRecipePreviewNote"
                    Layout.fillWidth: true
                    text: editRecipeDialog.recipeText.length > 4096
                        ? window.t("只读预览已截断；回车、不换行空格及 Unicode 行/段分隔符以转义形式显示。请在原处查看完整配方。", "Read-only preview truncated; carriage returns, non-breaking spaces, and Unicode line/paragraph separators are escaped. Review the full recipe in its source.")
                        : window.t("只读预览；回车、不换行空格及 Unicode 行/段分隔符以转义形式显示。", "Read-only preview; carriage returns, non-breaking spaces, and Unicode line/paragraph separators are escaped.")
                    wrapMode: Text.WordWrap
                }
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                TextArea {
                    id: editRecipeText
                    objectName: "editRecipeText"
                    readOnly: true
                    text: editRecipeDialog.previewText(editRecipeDialog.recipeText)
                    Keys.priority: Keys.BeforeItem
                    Keys.onPressed: function(event) {
                        if (event.matches(StandardKey.Paste)) {
                            event.accepted = true
                            editRecipeDialog.pasteRecipe()
                        }
                    }
                    wrapMode: TextEdit.NoWrap
                    selectByMouse: true
                    placeholderText: '[{"command":"develop.set","parameter":"exposure","value":0.5}]'
                    font.family: "monospace"
                }
            }
            Label {
                objectName: "editRecipeError"
                Layout.fillWidth: true
                visible: text.length > 0
                text: !editRecipeDialog.targetCurrent
                    ? window.t("当前版本已变更，请重新打开此窗口。", "The current version changed. Reopen this dialog.")
                    : editRecipeDialog.errorText
                wrapMode: Text.WrapAnywhere
                color: "#ffb4a9"
                Layout.maximumHeight: 100
                elide: Text.ElideRight
                maximumLineCount: 4
            }
        }
        footer: DialogButtonBox {
            Button {
                objectName: "editRecipeCancel"
                text: window.t("取消", "Cancel")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
                onClicked: editRecipeDialog.close()
            }
            Button {
                objectName: "editRecipeApply"
                text: window.t("应用配方", "Apply recipe")
                enabled: editRecipeDialog.targetCurrent && photoController.currentSourceAvailable && editRecipeDialog.recipeText.trim().length > 0
                DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
                onClicked: editRecipeDialog.applyRecipe()
            }
        }
    }


    Shortcut { sequences: [StandardKey.Undo]; enabled: photoController.canUndo; onActivated: photoController.undo() }
    Dialog {
        id: shortcutHelpDialog
        objectName: "keyboardShortcutsDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(window.width - 32, 680)
        height: Math.min(window.height - 40, 620)
        modal: true
        focus: true
        title: window.t("键盘快捷键", "Keyboard shortcuts")
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onOpened: shortcutGuide.forceActiveFocus()
        contentItem: ScrollView {
            id: shortcutGuide
            objectName: "keyboardShortcutsGuide"
            clip: true
            focus: true
            KeyNavigation.priority: KeyNavigation.BeforeItem
            KeyNavigation.tab: shortcutHelpCloseButton
            KeyNavigation.backtab: shortcutHelpCloseButton
            contentWidth: availableWidth
            contentHeight: shortcutGuideColumn.implicitHeight
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            // The modal guide owns keys even when the canvas has an active tool.
            Keys.onShortcutOverride: function(event) { event.accepted = true }
            ColumnLayout {
                id: shortcutGuideColumn
                width: shortcutGuide.availableWidth
                spacing: 14
                Label {
                    Layout.fillWidth: true
                    text: window.t("输入文字时，字母键和撤销/重做由输入框处理。画布快捷键需要先聚焦相应工具。", "While typing, letter keys and Undo/Redo belong to the text field. Canvas shortcuts require focus in the corresponding tool.")
                    textFormat: Text.PlainText
                    wrapMode: Text.WordWrap
                    color: "#aebed0"
                }
                Repeater {
                    model: [
                        { title: window.t("页面与导入", "Workspace and import"), rows: [
                            { keyText: "G", action: window.t("打开图库", "Open Library") },
                            { keyText: "D", action: window.t("打开修改照片页面", "Open Develop") },
                            { keyText: standardShortcutLabels.open.join(" / "), action: window.t("添加现有照片", "Add existing photos") }
                        ] },
                        { title: window.t("编辑历史", "Edit history"), rows: [
                            { keyText: standardShortcutLabels.undo.join(" / "), action: window.t("撤销", "Undo") },
                            { keyText: standardShortcutLabels.redo.join(" / "), action: window.t("重做", "Redo") }
                        ] },
                        { title: window.t("笔刷画布", "Brush canvas"), rows: [
                            { keyText: "[ / ]", action: window.t("缩小/放大笔刷半径", "Decrease/increase brush radius") },
                            { keyText: window.t("空格 + 拖动", "Space + drag"), action: window.t("在 100% / 200% 视图中平移；松开空格后可继续拖动到鼠标抬起。", "Pan a 100% / 200% view; dragging can continue after releasing Space until mouse lift.") },
                            { keyText: "Esc", action: window.t("取消正在进行的笔触或平移；空闲时退出笔刷工具。", "Cancel an active stroke or pan; leave the brush tool when idle.") }
                        ] },
                        { title: window.t("裁剪画布", "Crop canvas"), rows: [
                            { keyText: "Return", action: window.t("应用裁剪", "Apply crop") },
                            { keyText: "Esc", action: window.t("取消裁剪", "Cancel crop") }
                        ] }
                    ]
                    delegate: ColumnLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 6
                        Label { text: modelData.title; textFormat: Text.PlainText; font.bold: true; color: "#e0e8f2" }
                        Repeater {
                            model: modelData.rows
                            delegate: GridLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                columns: 2
                                columnSpacing: 18
                                Label {
                                    text: modelData.keyText
                                    textFormat: Text.PlainText
                                    Layout.preferredWidth: 160
                                    Layout.alignment: Qt.AlignTop
                                    wrapMode: Text.WrapAnywhere
                                    color: "#c3d8ec"
                                }
                                Label {
                                    text: modelData.action
                                    textFormat: Text.PlainText
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                    color: "#d7dde6"
                                }
                            }
                        }
                    }
                }
            }
        }
        footer: DialogButtonBox {
            Keys.onShortcutOverride: function(event) { event.accepted = true }
            Button {
                id: shortcutHelpCloseButton
                KeyNavigation.priority: KeyNavigation.BeforeItem
                KeyNavigation.tab: shortcutGuide
                KeyNavigation.backtab: shortcutGuide
                objectName: "keyboardShortcutsClose"
                text: window.t("关闭", "Close")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
            onRejected: shortcutHelpDialog.close()
        }
    }

    function chooseRelinkSource() {
        if (!photoController.hasImage || !photoController.projectPath) return
        relinkFilePicker.versionKey = photoController.library[photoController.currentIndex].id
        relinkFilePicker.catalogToken = photoController.relinkCatalogToken()
        relinkFilePicker.open()
    }
    FileDialog {
        id: relinkFilePicker
        objectName: "relinkFilePicker"
        property string versionKey: ""
        property string catalogToken: ""
        title: window.t("选择该原片的新位置", "Choose the original's new location")
        fileMode: FileDialog.OpenFile
        onAccepted: { if (photoController.beginRelink(versionKey, selectedFile, catalogToken)) relinkDialog.open() }
    }
    Dialog {
        id: relinkDialog
        objectName: "relinkSourceDialog"
        anchors.centerIn: parent
        width: Math.min(window.width-40,620)
        modal: true
        closePolicy: Popup.NoAutoClose
        title: window.t("重新关联源文件", "Relink source")
        readonly property var preview: photoController.relinkPreview
        onAboutToShow: unknownIdentityAcknowledgement.checked = false
        onRejected: photoController.cancelRelink()
        contentItem: ColumnLayout {
            spacing: 12
            Label {
                Layout.fillWidth: true; wrapMode: Text.WrapAnywhere
                text: window.t("原位置：", "Previous location: ") + (relinkDialog.preview.oldPath || "")
                    + "\n\n" + window.t("新位置：", "New location: ") + (relinkDialog.preview.candidatePath || "")
                    + "\n\n" + window.t("将更新共享此源文件的 %1 个照片/版本，保留各自的编辑、历史、评分和标签。", "Update %1 photos/versions sharing this source, preserving their edits, history, ratings and tags.").arg(relinkDialog.preview.versionCount || 0)
                color: "#d6dee8"
            }
            BusyIndicator { Layout.alignment: Qt.AlignHCenter; running: photoController.relinkBusy; visible: running }
            Label {
                objectName: "relinkIdentityVerdict"
                Layout.fillWidth: true; wrapMode: Text.WordWrap
                text: relinkDialog.preview.status === "error" ? (relinkDialog.preview.error || "")
                    : photoController.relinkBusy ? window.t("正在后台校验完整文件和解码，请稍候…", "Verifying full file content and decoding in background…")
                    : relinkDialog.preview.identityKnown ? window.t("完整 SHA-256 与目录记录一致，文件可解码。", "Full SHA-256 matches the catalog record; the file decodes successfully.")
                    : window.t("旧目录没有完整内容校验记录，无法证明这是原来的照片。请自行核对新位置后确认。", "This older catalog has no full content identity. It cannot prove this is the original photograph. Check the new source before confirming.")
                color: relinkDialog.preview.status === "error" ? "#ef9b85" : "#a8bed4"
            }
            CheckBox {
                id: unknownIdentityAcknowledgement
                objectName: "relinkUnknownIdentityAcknowledgement"
                visible: !relinkDialog.preview.identityKnown && relinkDialog.preview.status === "ready"
                Layout.fillWidth: true
                text: window.t("我已核对该文件，同意将其作为这些版本的源文件。", "I checked this file and accept it as the source of these versions.")
                contentItem: Label { text: unknownIdentityAcknowledgement.text; leftPadding: unknownIdentityAcknowledgement.indicator.width + 8; wrapMode: Text.WordWrap; color: "#d6dee8"; verticalAlignment: Text.AlignVCenter }
            }
        }
        footer: DialogButtonBox {
            Button {
                objectName: "confirmRelinkSource"
                text: window.t("确认关联", "Confirm relink")
                enabled: !photoController.relinkBusy && relinkDialog.preview.status === "ready"
                    && (relinkDialog.preview.identityKnown || unknownIdentityAcknowledgement.checked)
                onClicked: photoController.confirmRelink(relinkDialog.preview.requestId,unknownIdentityAcknowledgement.checked)
            }
            Button { objectName: "cancelRelinkSource"; text: window.t("取消", "Cancel"); onClicked: relinkDialog.reject() }
        }
        Connections {
            target: photoController
            function onRelinkChanged() { if (photoController.relinkPreview.status === "complete") relinkDialog.accept() }
        }
    }
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

    AdjustmentTransferDialog { id: transferDialog; controller: photoController }

    Dialog {
        id: exportSettingsDialog
        objectName: "exportSettingsDialog"
        width: 540
        title: selectedOnly ? window.t("导出所选照片/版本", "Export Selected Photos/Versions") : window.t("导出设置", "Export Settings")
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        anchors.centerIn: parent
        property bool batchMode: false
        property bool selectedOnly: false
        property var capturedVersionKeys: []
        property alias nameTemplate: batchNameTemplate.text
        property alias sequenceStart: batchSequence.value
        readonly property var namingPreview: {
            const photos = photoController.library
            const language = photoController.language
            const format = ["jpeg", "png", "tiff", "webp"][batchFormatBox.currentIndex]
            return selectedOnly
                ? photoController.exportVersionNamePreview(capturedVersionKeys, nameTemplate, sequenceStart, format)
                : photoController.exportNamePreview(nameTemplate, sequenceStart, format)
        }
        Settings {
            category: "BatchExportNaming"
            property alias pattern: exportSettingsDialog.nameTemplate
            property alias sequenceStart: exportSettingsDialog.sequenceStart
        }
        onOpened: {
            window.localizeDialogButtons(exportSettingsDialog)
            const ok = standardButton(Dialog.Ok)
            ok.objectName = "exportSettingsContinue"
            ok.text = Qt.binding(function() { return exportSettingsDialog.batchMode ? window.t("选择文件夹…", "Choose folder…") : window.t("选择文件…", "Choose file…") })
            ok.enabled = Qt.binding(function() { return !exportSettingsDialog.batchMode || exportSettingsDialog.namingPreview.valid })
            standardButton(Dialog.Cancel).objectName = "exportSettingsCancel"
            standardButton(Dialog.Cancel).text = Qt.binding(function() { return window.t("取消", "Cancel") })
        }
        onAccepted: {
            if (batchMode) {
                batchFolder.selectedOnly = selectedOnly
                batchFolder.capturedVersionKeys = capturedVersionKeys.slice()
                batchFolder.open()
            } else exportDialog.open()
        }
        contentItem: ColumnLayout {
            implicitWidth: 500; spacing: 6
            Label {
                objectName: "selectedExportSummary"
                visible: exportSettingsDialog.selectedOnly
                Layout.fillWidth: true
                text: window.t(
                    "已固定 %1 个所选照片/版本，按图库顺序导出。改选或清空选择不会改变本次范围；排队后保留当时的编辑快照。",
                    "%1 selected photos/versions captured in catalog order. Changing or clearing selection keeps this scope; queued exports retain their captured edits.")
                    .arg(exportSettingsDialog.capturedVersionKeys.length)
                wrapMode: Text.WordWrap; color: "#a5bed6"; font.pixelSize: 11
            }
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
                objectName: "batchExportFormat"
                visible: exportSettingsDialog.batchMode
                Layout.fillWidth: true
                model: ["JPEG", "PNG 16-bit", "TIFF 16-bit", "WebP 8-bit"]
            }
            Label {
                visible: exportSettingsDialog.batchMode
                text: window.t("文件名模板（留空使用默认命名）", "Filename template (empty keeps default naming)")
                color: "#d6dee8"
            }
            TextField {
                id: batchNameTemplate
                objectName: "batchExportTemplate"
                visible: exportSettingsDialog.batchMode
                Layout.fillWidth: true
                maximumLength: 160
                placeholderText: "{name}_{version}_{seq:4}"
                selectByMouse: true
            }
            Label {
                visible: exportSettingsDialog.batchMode
                Layout.fillWidth: true
                text: "{name}  {version}  {seq}  {seq:1}…{seq:9}  {capture_date}  {capture_time}"
                wrapMode: Text.WordWrap; color: "#7f8e9e"; font.pixelSize: 10
            }
            RowLayout {
                visible: exportSettingsDialog.batchMode
                Layout.fillWidth: true
                Label { text: window.t("起始序号", "First sequence number"); color: "#d6dee8" }
                SpinBox {
                    id: batchSequence
                    objectName: "batchExportSequence"
                    from: 1; to: 999999999; value: 1; editable: true
                    enabled: batchNameTemplate.text.length > 0
                    Layout.preferredWidth: 170
                }
                Item { Layout.fillWidth: true }
                Button { text: window.t("默认命名", "Default naming"); onClicked: { batchNameTemplate.text = ""; batchSequence.value = 1 } }
            }
            Label {
                objectName: "batchExportNamePreview"
                visible: exportSettingsDialog.batchMode
                Layout.fillWidth: true
                textFormat: Text.PlainText
                text: exportSettingsDialog.namingPreview.valid
                    ? window.t("导出 %1 个版本 · 名称预览", "Export %1 versions · Name preview").arg(exportSettingsDialog.namingPreview.count)
                        + "\n" + exportSettingsDialog.namingPreview.names.join("\n")
                    : exportSettingsDialog.namingPreview.error
                wrapMode: Text.WrapAnywhere
                maximumLineCount: 4; elide: Text.ElideRight
                color: exportSettingsDialog.namingPreview.valid ? "#d6dee8" : "#e59a7c"
                font.pixelSize: 11
                Layout.maximumHeight: 100
                ToolTip.visible: exportNamesHover.hovered; ToolTip.text: text
                HoverHandler { id: exportNamesHover }
            }
            Label {
                visible: exportSettingsDialog.batchMode
                Layout.fillWidth: true
                text: window.t(
                    "拍摄日期为相机记录的 yyyyMMdd，时间为 HHmmss；原版名称为 Original。默认命名自动避开重名；自定义模板重名或缺少拍摄时间时，整批拒绝导出。",
                    "Capture date is yyyyMMdd and time is HHmmss from the camera record. Original versions use Original. Default names avoid collisions; custom collisions or missing capture times reject the whole batch.")
                wrapMode: Text.WordWrap; color: "#7f8e9e"; font.pixelSize: 10
            }
            RowLayout {
                Layout.fillWidth: true
                Label { text: window.t("JPEG / WebP 质量", "JPEG / WebP Quality"); color: "#d6dee8" }
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
                    "嵌入目标 ICC。PNG / TIFF 保留 16-bit；WebP 为 8-bit，质量 100 时无损。TIFF 使用无损 LZW 压缩。",
                    "Target ICC is embedded. PNG / TIFF retain 16-bit channels. WebP is 8-bit; quality 100 is lossless. TIFF uses lossless LZW compression.")
                wrapMode: Text.WordWrap; color: "#7f8e9e"; font.pixelSize: 10
            }
        }
    }

    FileDialog {
        id: exportDialog
        title: window.t("导出照片", "Export Photo")
        fileMode: FileDialog.SaveFile
        defaultSuffix: ["jpg","png","tif","webp"][Math.max(0,selectedNameFilter.index)]
        nameFilters: ["JPEG (*.jpg *.jpeg)", "PNG 16-bit (*.png)", "TIFF 16-bit (*.tif *.tiff)", "WebP 8-bit (*.webp)"]
        onAccepted: photoController.exportCurrent(selectedFile, window.exportSpaceKey(exportSpaceBox.currentIndex), exportQualityBox.value)
    }
    CopyImportDialog {
        id: copyImportDialog; controller: photoController; parent: Overlay.overlay
        hostWidth: window.width; hostHeight: window.height
    }
    Connections {
        target: photoController
        function onCopyImportRequested(urls) { copyImportDialog.openFor(urls) }
    }
    FolderDialog {
        id: batchFolder
        objectName: "batchExportFolderDialog"
        property bool selectedOnly: false
        property var capturedVersionKeys: []
        title: selectedOnly ? window.t("导出所选照片/版本到文件夹", "Export selected photos/versions to folder") : window.t("批量导出文件夹", "Batch export folder")
        onAccepted: {
            const format = ["jpeg", "png", "tiff", "webp"][batchFormatBox.currentIndex]
            if (selectedOnly)
                photoController.exportVersions(capturedVersionKeys, selectedFolder, window.exportSpaceKey(exportSpaceBox.currentIndex), exportQualityBox.value, format, exportSettingsDialog.nameTemplate, exportSettingsDialog.sequenceStart)
            else
                photoController.exportAll(selectedFolder, window.exportSpaceKey(exportSpaceBox.currentIndex), exportQualityBox.value, format, exportSettingsDialog.nameTemplate, exportSettingsDialog.sequenceStart)
        }
    }
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
            MenuItem { text: window.t("新建项目", "New Project"); enabled: !photoController.copyImportBusy; onTriggered: projectFolder.open() }
            MenuItem { text: window.t("打开已有项目", "Open Existing Project"); enabled: !photoController.copyImportBusy; onTriggered: openProjectFolder.open() }
            MenuSeparator {}
            MenuItem { text: window.t("导入 XMP 到当前版本…", "Import XMP into current version…"); enabled: photoController.hasImage; onTriggered: photoController.openXmpImportDialog() }
            MenuItem { objectName: "editRecipeMenuAction"; text: window.t("应用编辑配方…", "Apply edit recipe…"); enabled: photoController.currentSourceAvailable && !photoController.cropEditing && !photoController.brushDrawing; onTriggered: editRecipeDialog.open() }
            MenuItem { objectName: "relinkSourceMenuAction"; text: window.t("重新关联源文件…", "Relink source…"); enabled: photoController.hasImage && photoController.projectPath.length > 0 && !photoController.relinkBusy && !photoController.exportBusy; onTriggered: window.chooseRelinkSource() }
        }
        Menu {
            id: exportActions
            objectName: "exportActionsMenu"
            MenuItem {
                text: window.t("导出当前照片", "Export Current Photo")
                enabled: photoController.hasImage && !photoController.exportBusy
                onTriggered: { exportSettingsDialog.selectedOnly = false; exportSettingsDialog.capturedVersionKeys = []; exportSettingsDialog.batchMode = false; exportSettingsDialog.open() }
            }
            MenuItem {
                objectName: "batchExportAction"
                text: window.t("导出全部照片/版本…", "Export All Photos/Versions…")
                enabled: photoController.hasImage && !photoController.exportBusy
                onTriggered: { exportSettingsDialog.selectedOnly = false; exportSettingsDialog.capturedVersionKeys = []; exportSettingsDialog.batchMode = true; exportSettingsDialog.open() }
            }
            MenuItem {
                objectName: "selectedBatchExportAction"
                text: window.t("导出所选照片/版本…", "Export Selected Photos/Versions…")
                enabled: photoController.selectedIndices.length > 0 && !photoController.exportBusy
                onTriggered: {
                    exportSettingsDialog.capturedVersionKeys = photoController.selectedVersionKeys().slice()
                    exportSettingsDialog.selectedOnly = true
                    exportSettingsDialog.batchMode = true
                    exportSettingsDialog.open()
                }
            }
            MenuSeparator {}
            MenuItem { text: window.t("导出 XMP 侧车文件…", "Export XMP sidecar…"); enabled: photoController.hasImage; onTriggered: photoController.openXmpExportDialog() }
        }
        Menu {
            id: importActions
            parent: importButton; y: importButton.height
            objectName: "importActionsMenu"
            MenuItem { text: window.t("添加现有照片…", "Add existing photos…"); onTriggered: photoController.openImportDialog() }
            MenuItem { objectName: "copyImportAction"; text: window.t("复制到文件夹并导入…", "Copy to folder and import…"); enabled: !photoController.copyImportBusy; onTriggered: photoController.openCopyImportDialog() }
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
                objectName: "projectMenuButton"
                text: window.t("项目 ▾", "Project ▾")
                onClicked: projectActions.popup()
            }
            ToolButton {
                id: importButton
                text: window.t("导入 ▾", "Import ▾")
                onClicked: importActions.open()
            }
            ToolSeparator {}
            ToolButton { objectName: "undoEditButton"; text: window.t("撤销", "Undo"); enabled: photoController.canUndo; onClicked: photoController.undo() }
            ToolButton { objectName: "redoEditButton"; text: window.t("重做", "Redo"); enabled: photoController.canRedo; onClicked: photoController.redo() }
            ToolButton {
                text: window.t("复制", "Copy")
                enabled: photoController.hasImage
                onClicked: photoController.copyAdjustments()
            }
            ToolButton {
                text: window.t("粘贴…", "Paste…")
                enabled: photoController.hasImage && photoController.hasAdjustmentClipboard
                onClicked: transferDialog.openFor(false)
            }
            ToolButton {
                text: window.t("同步…", "Sync…")
                objectName: "openTransferSync"
                enabled: photoController.hasImage
                onClicked: transferDialog.openFor(true)
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
                objectName: "keyboardShortcutsButton"
                // Keep editor focus when opening by mouse; remain reachable by Tab.
                focusPolicy: Qt.TabFocus
                text: "?"
                Accessible.name: window.t("键盘快捷键", "Keyboard shortcuts")
                ToolTip.visible: hovered
                ToolTip.text: window.t("键盘快捷键", "Keyboard shortcuts")
                onClicked: shortcutHelpDialog.open()
            }
            ToolButton {
                text: photoController.diagnosticBusy ? window.t("诊断中…", "Capturing…") : window.t("🐞 报告问题", "🐞 Report")
                enabled: !photoController.diagnosticBusy
                onClicked: photoController.reportBugWithDialog()
            }
        }
    }

    footer: Rectangle {
        height: 32; color: "#15191f"; border.color: "#303943"
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10; spacing: 10
            ProgressBar { visible: photoController.exportBusy; value: photoController.exportProgress; Layout.preferredWidth: 120 }
            Label { visible: photoController.copyImportBusy; text: photoController.copyImportStatus; textFormat: Text.PlainText; color: "#aeb9c7" }
            ProgressBar { visible: photoController.copyImportBusy; value: photoController.copyImportProgress; Layout.preferredWidth: 90 }
            ToolButton { visible: photoController.copyImportBusy; text: window.t("取消复制", "Cancel copy"); onClicked: photoController.cancelCopyImport() }
            CheckBox { text: "GPU"; checked: photoController.gpuEnabled; onToggled: photoController.gpuEnabled = checked }
            Label { text: photoController.statusMessage; textFormat: Text.PlainText; color: "#9eabb9"; elide: Text.ElideMiddle; Layout.fillWidth: true; font.pixelSize: 11 }
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
                            onRelinkRequested: window.chooseRelinkSource()
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
                    anchors.fill: parent; spacing: 12; visible: photoController.currentSourceAvailable
                    ColumnLayout {
                        Layout.fillWidth: true; Layout.fillHeight: true; Layout.preferredWidth: 1; Layout.minimumWidth: 0
                        Label { Layout.fillWidth: true; wrapMode: Text.Wrap; text: window.t("当前 RAW / 编辑结果", "Current RAW / edit result"); color: "#aebccc" }
                        PhotoCanvas { objectName: "photoCanvas"; enabled: !shortcutHelpDialog.visible; Layout.fillWidth: true; Layout.fillHeight: true; controller: photoController; brushEditor: brushEditorState }
                    }
                    CameraReferenceView { controller: photoController; visible: photoController.showCameraReference; Layout.fillWidth: true; Layout.fillHeight: true; Layout.preferredWidth: 1; Layout.minimumWidth: 0 }
                }
                Column {
                    anchors.centerIn: parent; visible: photoController.hasImage && !photoController.currentSourceAvailable; spacing: 14
                    Label { anchors.horizontalCenter: parent.horizontalCenter; text: window.t("源文件缺失或不可读", "Source missing or unreadable"); color: "#e6b078"; font.pixelSize: 22 }
                    Label { text: window.t("照片版本、编辑历史、评分和标签仍保留在项目中。", "Versions, edit history, ratings and tags remain in the project."); color: "#a2b2c3" }
                    Button { anchors.horizontalCenter: parent.horizontalCenter; text: window.t("重新关联源文件…", "Relink source…"); enabled: !photoController.relinkBusy && !photoController.exportBusy; onClicked: window.chooseRelinkSource() }
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
                brushEditor: brushEditorState
                enabled: photoController.currentSourceAvailable
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
