import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// One selection shared across the navigator, gallery and the Develop canvas.
Rectangle {
    id: root
    required property var controller
    signal editRequested()
    color: "#13171d"
    border.color: "#2b343f"
    function t(zh, en) { return controller.language === "zh_CN" ? zh : en }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        anchors.topMargin: 3
        anchors.bottomMargin: 4
        spacing: 2
        RowLayout {
            Layout.fillWidth: true
            Label {
                text: root.t("胶片条", "FILMSTRIP")
                color: "#93a5b7"
                font.bold: true
                font.pixelSize: 10
            }
            Label {
                text: root.controller.library.length + root.t(" 张", " photos")
                color: "#748496"
                font.pixelSize: 10
            }
            Item { Layout.fillWidth: true }
            Label {
                text: root.t("双击照片：修改照片", "Double-click to Develop")
                color: "#748496"
                font.pixelSize: 10
            }
        }
        ListView {
            id: strip
            objectName: "filmstripList"
            Layout.fillHeight: true
            Layout.fillWidth: true
            orientation: ListView.Horizontal
            clip: true
            spacing: 6
            boundsBehavior: Flickable.StopAtBounds
            model: root.controller.library
            currentIndex: root.controller.currentIndex
            delegate: Rectangle {
                required property int index
                required property var modelData
                width: 116
                height: strip.height
                radius: 4
                color: index === root.controller.currentIndex ? "#2e4155" : mouse.containsMouse ? "#222d38" : "#1d232b"
                border.width: index === root.controller.currentIndex ? 2 : 1
                border.color: index === root.controller.currentIndex ? "#79b6ef" : "#35404c"
                Image {
                    anchors.fill: parent
                    anchors.margins: 4
                    source: "image://thumbnails/" + encodeURIComponent(modelData.path)
                    sourceSize: Qt.size(256, 180)
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    cache: true
                }
                Label {
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.margins: 3
                    text: (modelData.rating > 0 ? "★" + modelData.rating : "")
                        + (modelData.flag === "pick" ? "  P" : modelData.flag === "reject" ? "  X" : "")
                    visible: modelData.rating > 0 || modelData.flag !== "none"
                    color: "#d5e9ff"
                    font.pixelSize: 10
                    background: Rectangle { color: "#cb1a2530"; radius: 2 }
                }
                Label {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.margins: 3
                    text: modelData.name
                    elide: Text.ElideMiddle
                    color: "#e5ecf3"
                    font.pixelSize: 9
                    horizontalAlignment: Text.AlignHCenter
                    background: Rectangle { color: "#bd14181d"; radius: 2 }
                }
                MouseArea {
                    id: mouse
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
            Connections {
                target: root.controller
                function onCurrentIndexChanged() {
                    if (root.controller.currentIndex >= 0)
                        strip.positionViewAtIndex(root.controller.currentIndex, ListView.Contain)
                }
            }
        }
    }
}
