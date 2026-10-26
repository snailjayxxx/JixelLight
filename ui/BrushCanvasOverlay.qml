import QtQuick

Item {
    id: root
    required property var controller
    property var editor: null
    readonly property bool layerPaintable: {
        if (!controller || !editor) return false
        if (editor.layerId === "") return true
        for (const layer of controller.brushLayers)
            if (layer.id === editor.layerId) return layer.enabled
        return false
    }
    property bool drawing: false
    property var points: []
    property point cursorPoint: Qt.point(-1000, -1000)
    readonly property rect frame: controller ? controller.previewFrameRect : Qt.rect(0,0,1,1)
    readonly property size pixels: controller ? controller.brushFrameSize : Qt.size(0,0)
    readonly property real radius: editor ? editor.radius : 0.05
    readonly property real radiusX: pixels.width > 0 && frame.width > 0 ? radius * Math.min(pixels.width,pixels.height) / pixels.width * width / frame.width : 0
    readonly property real radiusY: pixels.height > 0 && frame.height > 0 ? radius * Math.min(pixels.width,pixels.height) / pixels.height * height / frame.height : 0
    clip: true
    Keys.onEscapePressed: function(event) { root.cancel(); event.accepted=true }
    function imagePoint(x,y) {
        return controller.maskCanvasPoint(Math.max(0,Math.min(width,x)),Math.max(0,Math.min(height,y)),width,height)
    }
    function clearGuide() { drawing=false; points=[]; if (guide) guide.requestPaint() }
    function cancel() { if (controller) controller.cancelBrushStroke(); clearGuide() }
    function addGuide(x,y) {
        let next=points.slice()
        if (next.length >= 1024) next=next.filter(function(p,i) { return i % 2 === 0 })
        next.push({x:x,y:y}); points=next; guide.requestPaint()
    }
    onVisibleChanged: if (!visible) cancel()
    onEnabledChanged: if (!enabled) cancel()
    onLayerPaintableChanged: { if (!layerPaintable) cancel(); if (guide) guide.requestPaint() }
    onRadiusXChanged: if (guide) guide.requestPaint()
    onRadiusYChanged: if (guide) guide.requestPaint()
    Component.onDestruction: if (controller) controller.cancelBrushStroke()
    Connections {
        target: root.controller
        function onBrushDrawingChanged() { if (!root.controller.brushDrawing) root.clearGuide() }
    }
    Connections {
        target: root.editor
        function onLayerIdChanged() { root.cancel() }
        function onRadiusChanged() { root.cancel() }
        function onHardnessChanged() { root.cancel() }
        function onOpacityChanged() { root.cancel() }
        function onExposureChanged() { root.cancel() }
    }
    Canvas {
        id: guide; anchors.fill: parent
        onPaint: {
            const c=getContext("2d"); c.clearRect(0,0,width,height)
            c.strokeStyle="#f4d992"; c.lineWidth=2
            if (root.drawing && root.points.length > 0) {
                c.beginPath(); c.moveTo(root.points[0].x,root.points[0].y)
                for (let i=1;i<root.points.length;++i) c.lineTo(root.points[i].x,root.points[i].y)
                c.stroke()
            }
            if (pointer.containsMouse || root.drawing) {
                c.strokeStyle="#bce7f2"; c.lineWidth=1
                c.beginPath(); c.ellipse(root.cursorPoint.x-root.radiusX,root.cursorPoint.y-root.radiusY,root.radiusX*2,root.radiusY*2); c.stroke()
            }
        }
    }
    MouseArea {
        id: pointer; objectName: "brushPointerArea"
        anchors.fill: parent; hoverEnabled: true; preventStealing: true; acceptedButtons: Qt.LeftButton
        cursorShape: root.layerPaintable ? Qt.CrossCursor : Qt.ForbiddenCursor
        onContainsMouseChanged: guide.requestPaint()
        onPressed: function(mouse) {
            root.forceActiveFocus()
            const p=root.imagePoint(mouse.x,mouse.y);root.cursorPoint=Qt.point(mouse.x,mouse.y)
            if (!root.layerPaintable || !root.editor || !root.controller.beginBrushStroke(root.editor.layerId,p.x,p.y,root.editor.radius,root.editor.hardness,root.editor.opacity,root.editor.exposure)) return
            root.drawing=true;root.points=[];root.addGuide(mouse.x,mouse.y)
        }
        onPositionChanged: function(mouse) {
            root.cursorPoint=Qt.point(mouse.x,mouse.y)
            if (root.drawing) {
                const p=root.imagePoint(mouse.x,mouse.y)
                if (!root.controller.appendBrushStroke(p.x,p.y,1)) { root.clearGuide(); return }
                root.addGuide(mouse.x,mouse.y)
            }
            guide.requestPaint()
        }
        onReleased: function(mouse) {
            if (!root.drawing) return
            const p=root.imagePoint(mouse.x,mouse.y)
            if (root.controller.appendBrushStroke(p.x,p.y,1)) {
                const layer=root.controller.endBrushStroke()
                if (layer !== "" && root.editor) root.editor.layerId=layer
            }
            root.clearGuide()
        }
        onCanceled: root.cancel()
    }
}
