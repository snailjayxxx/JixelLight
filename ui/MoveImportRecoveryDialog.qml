import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Dialog {
    id: root
    required property var controller
    required property real hostWidth
    required property real hostHeight
    property string journal: ""
    readonly property var preview: controller.moveImportRecoveryPreview
    onJournalChanged: if (visible) previewDelay.restart()
    Timer { id: previewDelay; interval: 150; onTriggered: root.controller.inspectMoveImportRecovery(root.journal) }
    objectName: "moveImportRecoveryDialog"
    modal: true
    x: Math.round((hostWidth - width) / 2)
    y: Math.max(16, Math.round((hostHeight - height) / 2))
    width: Math.min(660, hostWidth - 32)
    standardButtons: Dialog.Ok | Dialog.Cancel
    function t(zh, en) { return controller.language === "zh_CN" ? zh : en }
    function stateText(state) {
        if (state === "copied") return t("已复制", "Copied")
        if (state === "retiring") return t("正在移动", "Moving")
        if (state === "retained") return t("已保留", "Retained")
        if (state === "trashed") return t("已移至废纸篓", "In trash")
        return t("需要核对", "Review needed")
    }
    title: t("恢复移动导入", "Resume move import")
    onOpened: {
        if (journal.length === 0) journal = controller.moveImportJournal
        previewDelay.restart()
        const ok = standardButton(Dialog.Ok)
        ok.objectName = "moveImportResume"
        ok.text = Qt.binding(function() { return root.t("核对并继续移动", "Verify and resume move") })
        ok.enabled = Qt.binding(function() { return root.journal.length > 0 && !!root.preview.valid && !root.preview.pending && !root.controller.copyImportBusy })
        standardButton(Dialog.Cancel).text = Qt.binding(function() { return root.t("取消", "Cancel") })
    }
    onAccepted: controller.resumeMoveImport(journal)
    onClosed: { previewDelay.stop(); controller.cancelMoveImportRecoveryPreview() }
    FileDialog {
        id: chooseJournal
        title: root.t("选择移动恢复记录 move.json", "Choose move recovery record move.json")
        nameFilters: ["Move recovery (move.json)"]
        onAccepted: root.journal = selectedFile.toString()
    }
    contentItem: ColumnLayout {
        spacing: 10
        Label {
            Layout.fillWidth: true; Layout.minimumWidth: 0
            textFormat: Text.PlainText; wrapMode: Text.WordWrap
            text: root.t("打开记录所属的项目后，核对现有副本及已保存身份，再将尚未处理的来源移至废纸篓。已完成来源不会再次处理。读取记录本身不会移动文件。",
                         "Open the record's project. Verify the copies and saved identities, then move remaining sources to trash. Completed sources are never processed again. Reading a record does not move files.")
        }
        RowLayout {
            Layout.fillWidth: true
            TextField {
                objectName: "moveImportJournalPath"
                Layout.fillWidth: true; Layout.minimumWidth: 0
                text: root.journal; selectByMouse: true
                onTextEdited: root.journal = text
                placeholderText: root.t("移动恢复记录的完整路径", "Full move recovery record path")
            }
            Button { text: root.t("选择…", "Choose…"); onClicked: chooseJournal.open() }
        }
        Label {
            Layout.fillWidth: true; Layout.minimumWidth: 0
            textFormat: Text.PlainText; wrapMode: Text.WordWrap
            text: root.preview.pending ? root.t("正在读取记录…", "Reading record…") : root.preview.error ||
                (root.preview.valid ? root.t("%1 个副本 · 已完成 %2 个来源", "%1 copies · %2 sources completed").arg(root.preview.rows.length).arg(root.preview.retired) : "")
        }
        ScrollView {
            Layout.fillWidth: true; Layout.preferredHeight: 170
            ListView {
                model: root.preview.rows || []
                clip: true
                delegate: Label {
                    required property var modelData
                    width: ListView.view.width
                    textFormat: Text.PlainText; wrapMode: Text.WrapAnywhere
                    padding: 6
                    text: modelData.source + "\n→ " + modelData.destination + " · " + root.stateText(modelData.state)
                }
            }
        }
    }
}
