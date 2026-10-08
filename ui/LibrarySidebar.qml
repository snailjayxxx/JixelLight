import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Library navigator. No RAW decoding takes place in its QML delegates.
Rectangle {
    id: root
    required property var controller
    property int workspaceIndex: 1
    signal editRequested()
    color: "#15191f"
    border.color: "#2a3038"
    function t(zh, en) { return controller.language === "zh_CN" ? zh : en }

    function historyTitle(action) {
        var titles = {
            geometry_crop: ["裁切", "Crop"], geometry_rotate: ["旋转", "Rotate"],
            geometry_flip: ["翻转", "Flip"], geometry_reset: ["重置几何", "Reset geometry"],
            named_preset: ["命名预设", "Named preset"], original: ["原始状态", "Original"], adjustment: ["基本调整", "Basic adjustment"],
            color_mixer: ["混色器", "Color mixer"], curve_point: ["曲线", "Curve"], curve_reset: ["重置曲线", "Reset curve"],
            look_mode: ["外观模式", "Look mode"], look_preset: ["Sony 外观", "Sony Look"],
            look_strength: ["外观强度", "Look strength"], look_parameter: ["外观微调", "Look adjustment"],
            reset_adjustments: ["重置调整", "Reset adjustments"], paste_adjustments: ["粘贴调整", "Paste adjustments"],
            sync_adjustments: ["同步调整", "Sync adjustments"], reference_look_applied: ["应用参考外观", "Apply reference Look"],
            look_profile_imported: ["导入外观配置", "Import Look profile"]
        }
        var title = titles[action]
        return title ? root.t(title[0], title[1]) : root.t("编辑", "Edit")
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8

        Label {
            text: root.t("导航器", "NAVIGATOR")
            color: "#a3b2c1"
            font.bold: true
            font.pixelSize: 11
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 114
            color: "#0c0f14"
            radius: 5
            Image {
                anchors.fill: parent
                anchors.margins: 5
                source: root.controller.hasImage ? "image://thumbnails/" + encodeURIComponent(root.controller.currentFile) : ""
                sourceSize: Qt.size(260, 175)
                asynchronous: true
                fillMode: Image.PreserveAspectFit
                cache: true
            }
            Label {
                anchors.centerIn: parent
                visible: !root.controller.hasImage
                text: root.t("尚未导入照片", "No photo imported")
                color: "#677585"
                font.pixelSize: 12
            }
        }
        Label {
            Layout.fillWidth: true
            text: root.controller.hasImage ? root.controller.currentFile.split(/[\\/]/).pop() : ""
            elide: Text.ElideMiddle
            color: "#bfc9d4"
            font.pixelSize: 11
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: "#303740" }
        RowLayout {
            Layout.fillWidth: true
            Label {
                text: root.t("照片库", "LIBRARY")
                color: "#a3b2c1"
                font.pixelSize: 11
                font.bold: true
            }
            Item { Layout.fillWidth: true }
            Label {
                text: String(root.controller.library.length)
                color: "#8291a2"
                font.pixelSize: 11
            }
        }
        ListView {
            id: photoList
            objectName: "librarySidebarList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 3
            model: root.controller.library
            currentIndex: root.controller.currentIndex
            delegate: Rectangle {
                required property int index
                required property var modelData
                width: photoList.width
                height: 38
                radius: 4
                color: index === root.controller.currentIndex ? "#2e4054" : hovered.containsMouse ? "#212832" : "transparent"
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 6
                    spacing: 5
                    Label {
                        text: modelData.virtual ? root.t("副本", "COPY") : modelData.raw ? "RAW" : modelData.type
                        color: modelData.raw ? "#7bd8bd" : "#8fa0b1"
                        font.bold: true
                        font.pixelSize: 9
                        Layout.preferredWidth: 32
                    }
                    Label {
                        text: modelData.name
                        color: "#d0d9e3"
                        elide: Text.ElideMiddle
                        font.pixelSize: 11
                        Layout.fillWidth: true
                    }
                }
                MouseArea {
                    id: hovered
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton
                    onClicked: root.controller.selectPhoto(index)
                    onDoubleClicked: {
                        root.controller.selectPhoto(index)
                        root.editRequested()
                    }
                }
            }
        }
        ColumnLayout {
            visible: root.workspaceIndex === 1
            Layout.fillWidth: true
            spacing: 3
            Label { text: root.t("显影预设（此用户）", "DEVELOP PRESETS (THIS USER)"); color: "#a3b2c1"; font.pixelSize: 11 }
            ComboBox { id: presetChoice; Layout.fillWidth: true; model: root.controller.presetNames }
            RowLayout {
                Layout.fillWidth: true
                Button { text: root.t("应用", "Apply"); Layout.fillWidth: true; Layout.minimumWidth: 0; enabled: root.controller.hasImage && presetChoice.currentIndex >= 0; onClicked: root.controller.applyNamedPreset(presetChoice.currentText) }
                Button { text: root.t("删除", "Delete"); Layout.fillWidth: true; Layout.minimumWidth: 0; enabled: presetChoice.currentIndex >= 0; onClicked: root.controller.removeNamedPreset(presetChoice.currentText) }
            }
            RowLayout {
                Layout.fillWidth: true
                TextField { id: presetName; Layout.fillWidth: true; Layout.minimumWidth: 0; maximumLength: 80; placeholderText: root.t("新预设名称", "New preset name") }
                Button { text: root.t("保存", "Save"); enabled: root.controller.hasImage && presetName.text.trim().length > 0; onClicked: { if (root.controller.saveNamedPreset(presetName.text)) presetName.clear() } }
            }
            Label { Layout.fillWidth: true; text: root.t("包含 Sony 外观；不含裁切和评分", "Includes Sony Look; excludes crop and ratings"); color: "#8291a2"; font.pixelSize: 10; wrapMode: Text.WordWrap }
        }
        Label {
            visible: root.workspaceIndex === 1
            text: root.t("历史（随项目保存）", "HISTORY (SAVED IN PROJECT)")
            color: "#a3b2c1"; font.pixelSize: 11; font.bold: true
        }
        ListView {
            visible: root.workspaceIndex === 1
            Layout.fillWidth: true
            Layout.preferredHeight: 110
            clip: true
            model: root.controller.editHistory.slice().reverse()
            onModelChanged: Qt.callLater(function() {
                for (var i = 0; i < model.length; ++i) {
                    if (model[i].current) { currentIndex = i; positionViewAtIndex(i, ListView.Contain); break }
                }
            })
            delegate: Label {
                required property var modelData
                width: ListView.view.width
                text: (modelData.current ? "● " : "  ") + root.historyTitle(modelData.action)
                color: modelData.current ? "#7bd8bd" : "#8291a2"
                font.pixelSize: 11
                elide: Text.ElideRight
            }
        }
    }
}
