import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import JixelLight.Native 1.0

Item {
    id: root
    clip: true
    property var controller
    property var brushEditor: null
    readonly property bool brushDisplayReady: controller && (controller.gpuActive ||
        (processedImage.status === Image.Ready && String(processedImage.source) === String(controller.previewUrl)))
    property real zoom: 0
    property real centerX: .5
    property real centerY: .5
    // Edit-mode state is UI-only, never persisted in a photo or a project.
    property string maskTool: ""
    onMaskToolChanged: if (brushEditor && brushEditor.active !== (maskTool === "brush")) brushEditor.active = maskTool === "brush"
    function t(zh,en) { return controller.language === "zh_CN" ? zh : en }
    function brushBlockHint(reason) {
        if (reason === "disabled") return root.t("请先启用所选笔刷图层。", "Enable the selected brush layer first.")
        if (reason === "layers") return root.t("已达笔刷图层上限，请删除图层后再新建。", "Brush layer limit reached. Remove a layer to create another.")
        if (reason === "strokes") return root.t("该图层已达笔触上限，请删除一笔后继续。", "Stroke limit reached for this layer. Remove a stroke to continue.")
        if (reason === "dabs") return root.t("笔刷文档已满，请删除笔触或图层后继续。", "Brush document full. Remove a stroke or layer to continue.")
        if (reason === "missing") return root.t("请重新选择笔刷图层。", "Select a brush layer again.")
        if (reason === "invalid") return root.t("笔刷文档不可用，暂不能绘制。", "The brush document is unavailable for painting.")
        return root.controller ? root.controller.brushStatus : ""
    }
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
    onVisibleChanged: {
        if (!visible) {
            root.maskTool = ""
            if (controller.cropEditing) controller.cancelCrop()
        }
    }
    Shortcut {
        sequence: "Escape"
        enabled: root.visible && root.maskTool !== "" && !controller.cropEditing
        onActivated: {
            if (root.maskTool === "brush" && controller.brushDrawing) controller.cancelBrushStroke()
            else root.maskTool = ""
        }
    }
    Shortcut { sequence: "Escape"; enabled: root.visible && controller.cropEditing; onActivated: controller.cancelCrop() }
    Shortcut { sequence: "Return"; enabled: root.visible && controller.cropEditing; onActivated: root.applyCrop() }
    function applyCrop() {
        const r=cropOverlay.selection
        controller.applyCrop(r.x,r.y,r.width,r.height)
    }
    Timer { id: geometryTimer; interval: 16; onTriggered: root.refresh() }
    Component.onDestruction: {
        controller.cancelBrushStroke()
        if (brushEditor) brushEditor.active = false
    }
    Connections {
        target: root.brushEditor
        function onActiveChanged() {
            if (root.brushEditor.active) root.maskTool = "brush"
            else if (root.maskTool === "brush") root.maskTool = ""
        }
    }
    Connections {
        target: root.controller
        function onCurrentIndexChanged() {
            root.maskTool=""
            if (root.brushEditor) { root.brushEditor.strokeSelection=({}); root.brushEditor.layerId="" }
            root.centerX=.5;root.centerY=.5;root.refresh()
        }
        function onAdjustmentsChanged() {
            if (root.brushEditor) root.brushEditor.strokeSelection=({})
            if (!root.brushEditor || root.brushEditor.layerId === "") return
            const layers=root.controller.brushLayers
            for (const layer of layers) if (layer.id === root.brushEditor.layerId) return
            root.brushEditor.layerId = ""
        }
        function onCropEditingChanged() {
            if (root.controller.cropEditing) {
                root.maskTool=""
                root.zoom=0; cropOverlay.aspectRatio=0; cropAspect.currentIndex=0
                cropOverlay.selection=root.controller.cropOverlay
            }
        }
    }
    Item {
        id: picture
        anchors.centerIn: parent
        width: controller.previewDisplaySize.width
        height: controller.previewDisplaySize.height
        Image {
            id: processedImage
            objectName: "processedPreviewImage"
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
            active: root.controller.gpuEnabled && root.controller.currentSourceAvailable
            sourceComponent: Component {
                GpuPreview { controller: root.controller }
            }
            z: 0
        }
        MaskCanvasOverlay {
            id: maskOverlay
            objectName: "maskCanvasOverlay"
            anchors.fill: parent; z: 4
            controller: root.controller
            mode: root.maskTool
            visible: (root.maskTool === "linear" || root.maskTool === "radial") && !controller.cropEditing
                     && controller.previewReady && !controller.loading
            enabled: visible
        }
        BrushCanvasOverlay {
            id: brushOverlay
            objectName: "brushCanvasOverlay"
            anchors.fill: parent; z: 5
            controller: root.controller; editor: root.brushEditor
            visible: root.maskTool === "brush" && !controller.cropEditing && controller.previewReady
            enabled: visible && controller.brushReady && root.brushDisplayReady
        }
        CropOverlay {
            id: cropOverlay
            objectName: "interactiveCropOverlay"
            anchors.fill: parent; z: 3
            visible: controller.cropEditing && !controller.rendering
            enabled: visible
        }
    }
    MouseArea {
        anchors.fill: parent
        enabled: root.zoom>0 && controller.previewReady && !controller.cropEditing && root.maskTool === ""
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
    // Keep the two mask tools in their own row. A long one-row toolbar
    // overflowed narrow canvas layouts and left the Oval button outside
    // the clipped hit-test surface even when isVisible() returned true.
    Column {
        anchors.left: parent.left; anchors.top: parent.top; spacing: 4; z: 6
        visible: !controller.cropEditing
        Row {
            spacing: 5
            Button { objectName: "fitPreviewButton"; text: root.t("适合", "Fit"); enabled: !controller.cropEditing; onClicked: root.zoom=0 }
            Button { text: "100%"; enabled: controller.previewReady && !controller.cropEditing; onClicked: root.zoom=1 }
            Button { text: "200%"; enabled: controller.previewReady && !controller.cropEditing; onClicked: root.zoom=2 }
        }
        Row {
            spacing: 5
            Button {
                objectName: "editLinearMaskCanvas"
                enabled: controller.hasImage && controller.previewReady && !controller.loading
                highlighted: root.maskTool === "linear"
                text: root.t("线性蒙版手柄", "Linear handles")
                onClicked: root.maskTool = root.maskTool === "linear" ? "" : "linear"
            }
            Button {
                objectName: "editRadialMaskCanvas"
                enabled: controller.hasImage && controller.previewReady && !controller.loading
                highlighted: root.maskTool === "radial"
                text: root.t("椭圆蒙版手柄", "Oval handles")
                onClicked: root.maskTool = root.maskTool === "radial" ? "" : "radial"
            }
        }
        Row {
            spacing: 5
            Button {
                objectName: "editBrushMaskCanvas"
                enabled: controller.currentSourceAvailable && controller.previewReady && !controller.loading
                highlighted: root.maskTool === "brush"
                text: root.t("绘制笔刷", "Paint brush")
                onClicked: root.maskTool = root.maskTool === "brush" ? "" : "brush"
            }
            Button {
                objectName: "newBrushLayerCanvas"
                visible: root.maskTool === "brush"; enabled: controller.brushReady && controller.brushCapacity.new_layer_allowed
                text: root.t("新图层", "New layer")
                onClicked: if (root.brushEditor) { root.brushEditor.layerId=""; root.brushEditor.exposure=1 }
            }
        }
    }
    Label {
        objectName: "maskToolHint"
        anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.bottom
        anchors.bottomMargin: 44; z: 6
        visible: root.maskTool !== ""
        text: root.maskTool === "brush" ? (!brushOverlay.layerPaintable
              ? root.brushBlockHint(brushOverlay.paintBlock) : root.controller.brushReady && !root.brushDisplayReady
              ? root.t("正在更新预览显示，完成后可绘制。", "Wait for the displayed preview before painting.") : root.controller.brushStatus) : root.maskTool === "linear"
              ? root.t("黄色：移动 · 蓝色：旋转 · 两侧绿色：羽化 · Shift：精细拖动；曝光在右侧",
                       "Yellow: move · Blue: rotate · Two green grips: feather · Shift: fine; exposure in panel")
              : root.t("黄色：中心／半径 · 蓝色：旋转 · 两侧绿色：羽化 · Shift：精细拖动",
                       "Yellow: center/radius · Blue: rotate · Two green grips: feather · Shift: fine")
        font.pixelSize: 11; color: "#e5edf5"
        background: Rectangle { color: "#dd101820"; radius: 4 }
    }
    Row {
        anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.bottom; anchors.bottomMargin: 24
        spacing: 4; visible: controller.cropEditing; z: 5
        ComboBox {
            id: cropAspect; width: 90
            model: [root.t("自由", "Free"),"1:1","3:2","4:3","16:9","2:3"]
            onActivated: cropOverlay.setAspect([0,1,1.5,4/3,16/9,2/3][currentIndex])
        }
        Button { text: root.t("全图", "Full"); onClicked: { cropAspect.currentIndex=0; cropOverlay.aspectRatio=0; cropOverlay.selection=Qt.rect(0,0,1,1) } }
        Button { text: root.t("取消", "Cancel"); onClicked: controller.cancelCrop() }
        Button { objectName: "applyInteractiveCrop"; text: root.t("应用", "Apply"); enabled: !controller.rendering; onClicked: root.applyCrop() }
    }
    BusyIndicator {
        anchors.right: parent.right; anchors.top: parent.top
        width: 32; height: 32; running: controller.loading || controller.rendering
    }
    Label {
        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
        elide: Text.ElideMiddle; horizontalAlignment: Text.AlignHCenter
        text: controller.processingBackend
        color: "#a9b5c4"; font.pixelSize: 10
        background: Rectangle { color: "#c011161c"; radius: 4 }
    }
}
