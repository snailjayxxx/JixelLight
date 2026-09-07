import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import JixelLight.Native 1.0

Item {
    id: root
    property var controller
    property real zoom: 0
    property real centerX: .5
    property real centerY: .5
    function t(zh,en) { return controller.language === "zh_CN" ? zh : en }
    function refresh() {
        if (controller && width>0 && height>0)
            controller.setViewport(width,height,Screen.devicePixelRatio,zoom,centerX,centerY)
    }
    onWidthChanged: geometryTimer.restart()
    onHeightChanged: geometryTimer.restart()
    onZoomChanged: refresh()
    onCenterXChanged: geometryTimer.restart()
    onCenterYChanged: geometryTimer.restart()
    Component.onCompleted: refresh()
    Timer { id: geometryTimer; interval: 16; onTriggered: root.refresh() }
    Connections {
        target: root.controller
        function onCurrentIndexChanged() { root.centerX=.5;root.centerY=.5;root.refresh() }
    }
    Item {
        id: picture
        anchors.centerIn: parent
        width: controller.previewDisplaySize.width
        height: controller.previewDisplaySize.height
        Image {
            anchors.fill: parent
            source: controller.previewUrl
            cache: false; asynchronous: true; smooth: root.zoom === 0
            z: 1
            visible: !controller.gpuActive
            fillMode: Image.Stretch
        }
        // Do not instantiate an RHI item in explicit CPU/software mode. An
        // invisible RHI item can still receive updates and flood the log.
        Loader {
            anchors.fill: parent
            active: root.controller.gpuEnabled && root.controller.hasImage
            sourceComponent: Component {
                GpuPreview { controller: root.controller }
            }
            z: 0
        }
    }
    MouseArea {
        anchors.fill: parent
        enabled: root.zoom>0 && controller.previewReady
        property real oldX: 0
        property real oldY: 0
        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
        onPressed: function(mouse) { oldX=mouse.x;oldY=mouse.y }
        onPositionChanged: function(mouse) {
            if (!pressed) return
            const w=Number(controller.currentMetadata.pixelWidth || 1)
            const h=Number(controller.currentMetadata.pixelHeight || 1)
            root.centerX=Math.max(0,Math.min(1,root.centerX-(mouse.x-oldX)*Screen.devicePixelRatio/(w*root.zoom)))
            root.centerY=Math.max(0,Math.min(1,root.centerY-(mouse.y-oldY)*Screen.devicePixelRatio/(h*root.zoom)))
            oldX=mouse.x;oldY=mouse.y
        }
        onReleased: controller.finishInteraction()
    }
    Row {
        anchors.left: parent.left; anchors.top: parent.top; spacing: 5
        Button { text: root.t("适合", "Fit"); onClicked: root.zoom=0 }
        Button { text: "100%"; enabled: controller.previewReady; onClicked: root.zoom=1 }
        Button { text: "200%"; enabled: controller.previewReady; onClicked: root.zoom=2 }
    }
    BusyIndicator {
        anchors.right: parent.right; anchors.top: parent.top
        width: 32; height: 32; running: controller.loading || controller.rendering
    }
    Label {
        anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.bottom
        text: controller.processingBackend
        color: "#a9b5c4"; font.pixelSize: 10
        background: Rectangle { color: "#c011161c"; radius: 4 }
    }
}
