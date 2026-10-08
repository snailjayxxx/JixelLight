import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Responsive, virtualized library grid with an async thumbnail provider.
// Selecting photos keeps the same PhotoController state as Develop mode.
Rectangle {
    id: root
    required property var controller
    signal editRequested()
    color: "#0d1014"
    function t(zh, en) { return controller.language === "zh_CN" ? zh : en }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 10
        RowLayout {
            Layout.fillWidth: true
            Label {
                text: root.t("所有照片", "All Photos")
                font.pixelSize: 20
                font.bold: true
                color: "#e1e8f0"
            }
            Label {
                text: root.controller.library.length + root.t(" 张", " photos")
                color: "#8795a5"
                font.pixelSize: 12
            }
            Item { Layout.fillWidth: true }
            Button {
                text: root.t("导入照片", "Import Photos")
                onClicked: root.controller.openImportDialog()
            }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: "#2c343e" }
        GridView {
            id: grid
            objectName: "libraryPhotoGrid"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: root.controller.library
            currentIndex: root.controller.currentIndex
            cellWidth: Math.max(180, width / Math.max(1, Math.floor(width / 225)))
            cellHeight: 224
            delegate: Item {
                required property int index
                required property var modelData
                width: grid.cellWidth
                height: grid.cellHeight
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 5
                    radius: 6
                    color: "#1b2129"
                    border.width: index === root.controller.currentIndex ? 2 : 1
                    border.color: index === root.controller.currentIndex ? "#74aee9" : "#303944"
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 7
                        spacing: 5
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            color: "#11151a"
                            radius: 3
                            Image {
                                anchors.fill: parent
                                anchors.margins: 2
                                source: "image://thumbnails/" + encodeURIComponent(modelData.path)
                                sourceSize: Qt.size(384, 280)
                                fillMode: Image.PreserveAspectFit
                                asynchronous: true
                                cache: true
                            }
                            Label {
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.margins: 4
                                text: modelData.type
                                color: modelData.raw ? "#a7f1d6" : "#d1dbe5"
                                font.bold: true
                                font.pixelSize: 9
                                padding: 3
                                background: Rectangle { color: "#b51c2530"; radius: 3 }
                            }
                        }
                        Label {
                            Layout.fillWidth: true
                            text: modelData.name
                            color: "#dce5ee"
                            elide: Text.ElideMiddle
                            font.pixelSize: 11
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
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
        Label {
            visible: root.controller.library.length === 0
            Layout.alignment: Qt.AlignHCenter
            text: root.t("导入照片以建立图库，双击缩略图开始编辑。", "Import photos to start a library. Double-click a thumbnail to develop.")
            color: "#8b99a8"
        }
    }
}
