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
                        text: modelData.raw ? "RAW" : modelData.type
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
    }
}
