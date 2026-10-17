import QtQuick

// M3.1: explicit, non-AI direct handles for the two existing single-mask
// tools. The plane always shows the complete post-geometry image, even when
// the prepared preview is a zoomed/panned ROI. Only individual handles accept
// events; preview navigation is suspended by the hosting PhotoCanvas.
Item {
    id: root
    required property var controller
    property string mode: ""
    readonly property bool active: mode === "linear" || mode === "radial"
    readonly property rect frameRect: controller.previewFrameRect
    readonly property var linear: controller.linearMask
    readonly property var radial: controller.radialMask
    property string dragHandle: ""
    property bool dragFine: false // Shift is latched at press, not sampled mid-gesture.
    property point dragStartImage: Qt.point(0, 0)
    property var dragStartState: ({})
    clip: true

    function clamp(value, low, high) { return Math.max(low, Math.min(high, value)) }
    function radians(angle) { return angle * Math.PI / 180 }
    function signedAngle(value) {
        const degrees = ((value + 180) % 360 + 360) % 360 - 180
        return clamp(degrees, -180, 180)
    }
    function viewPoint(u, v) {
        if (!(frameRect.width > 0 && frameRect.height > 0 && width > 0 && height > 0))
            return Qt.point(-1000, -1000)
        return Qt.point((u - frameRect.x) * width / frameRect.width,
                        (v - frameRect.y) * height / frameRect.height)
    }
    function imagePoint(x, y) {
        return controller.maskCanvasPoint(x, y, width, height)
    }
    function linearAnchor(settings) {
        const r = radians(settings.angle)
        return Qt.point(.5 + (settings.center - .5) * Math.sin(r),
                        .5 + (settings.center - .5) * Math.cos(r))
    }
    function handleImage(kind) {
        if (kind === "linearCenter") return linearAnchor(linear)
        if (kind === "linearAngle") {
            const c = linearAnchor(linear), a = radians(linear.angle)
            return Qt.point(c.x + .24 * Math.cos(a), c.y - .24 * Math.sin(a))
        }
        if (kind === "linearFeather" || kind === "linearFeatherOpposite") {
            const c = linearAnchor(linear), a = radians(linear.angle)
            const side = kind === "linearFeather" ? 1 : -1
            return Qt.point(c.x + side * linear.feather * .5 * Math.sin(a),
                            c.y + side * linear.feather * .5 * Math.cos(a))
        }
        const x = radial.centerX, y = radial.centerY
        const a = radians(radial.angle)
        if (kind === "radialCenter") return Qt.point(x,y)
        if (kind === "radialRadiusX")
            return Qt.point(x + radial.radiusX * Math.cos(a), y + radial.radiusX * Math.sin(a))
        if (kind === "radialRadiusY")
            return Qt.point(x - radial.radiusY * Math.sin(a), y + radial.radiusY * Math.cos(a))
        if (kind === "radialFeather" || kind === "radialFeatherOpposite") {
            const side = kind === "radialFeather" ? 1 : -1
            const distance = side * radial.radiusX * (1 - radial.feather)
            return Qt.point(x + distance * Math.cos(a), y + distance * Math.sin(a))
        }
        if (kind === "radialAngle")
            return Qt.point(x + (radial.radiusY + .1) * Math.sin(a),
                            y - (radial.radiusY + .1) * Math.cos(a))
        return Qt.point(-1000,-1000)
    }
    function handleView(kind) {
        const point = handleImage(kind)
        return viewPoint(point.x, point.y)
    }
    function beginDrag(kind, viewX, viewY, fine) {
        if (!active || !controller.hasImage || !controller.previewReady) return
        const p = imagePoint(viewX, viewY)
        if (!Number.isFinite(p.x) || !Number.isFinite(p.y)) return
        dragHandle = kind
        dragFine = !!fine
        dragStartImage = p
        // Snapshot all inputs for the gesture. A live controller value changes
        // as the handle moves, and must never become the next delta's origin.
        dragStartState = mode === "linear" ? {
            center: linear.center, angle: linear.angle, feather: linear.feather
        } : {
            centerX: radial.centerX, centerY: radial.centerY,
            radiusX: radial.radiusX, radiusY: radial.radiusY,
            angle: radial.angle, feather: radial.feather
        }
    }
    function moveDrag(viewX, viewY) {
        if (!dragHandle) return
        const p = imagePoint(viewX, viewY)
        if (!Number.isFinite(p.x) || !Number.isFinite(p.y)) return
        const start = dragStartState
        // Use a frozen origin and one scale for the entire gesture. This also
        // makes rotated guides precise at non-square image aspect ratios.
        const factor = dragFine ? .2 : 1
        const du = (p.x - dragStartImage.x) * factor
        const dv = (p.y - dragStartImage.y) * factor
        const target = Qt.point(dragStartImage.x + du, dragStartImage.y + dv)
        const a = radians(start.angle), c = Math.cos(a), s = Math.sin(a)
        if (dragHandle === "linearCenter") {
            controller.setLinearMaskParameter("center",
                clamp(start.center + du * s + dv * c, 0, 1))
        } else if (dragHandle === "linearFeather" || dragHandle === "linearFeatherOpposite") {
            // The boundaries remain symmetric around their frozen center.
            const side = dragHandle === "linearFeather" ? 1 : -1
            const normalShift = du*s + dv*c
            controller.setLinearMaskParameter("feather",
                clamp(start.feather + side*2*normalShift, .02, 1))
        } else if (dragHandle === "linearAngle") {
            const origin = linearAnchor(start)
            const dx = target.x - origin.x, dy = target.y - origin.y
            if (Math.hypot(dx,dy) > .03)
                controller.setLinearMaskParameter("angle",
                    signedAngle(Math.atan2(-dy,dx) * 180 / Math.PI))
        } else if (dragHandle === "radialCenter") {
            controller.setRadialMaskCenter(
                clamp(start.centerX+du,0,1),clamp(start.centerY+dv,0,1))
        } else if (dragHandle === "radialRadiusX") {
            controller.setRadialMaskParameter("radiusX",
                clamp(start.radiusX + du*c + dv*s, .05, 1))
        } else if (dragHandle === "radialRadiusY") {
            controller.setRadialMaskParameter("radiusY",
                clamp(start.radiusY - du*s + dv*c, .05, 1))
        } else if (dragHandle === "radialFeather" || dragHandle === "radialFeatherOpposite") {
            // Opposite inner-contour grips both modify the one stored feather.
            const side = dragHandle === "radialFeather" ? 1 : -1
            const localShift=(du*c + dv*s)/Math.max(.05,start.radiusX)
            controller.setRadialMaskParameter("feather",
                clamp(start.feather - side*localShift, .02, 1))
        } else if (dragHandle === "radialAngle") {
            const oldX=dragStartImage.x-start.centerX
            const oldY=dragStartImage.y-start.centerY
            const newX=target.x-start.centerX, newY=target.y-start.centerY
            if (Math.hypot(oldX,oldY)>.03 && Math.hypot(newX,newY)>.03) {
                let diff=(Math.atan2(newX,-newY)-Math.atan2(oldX,-oldY))*180/Math.PI
                controller.setRadialMaskParameter("angle",signedAngle(start.angle+diff))
            }
        }
    }
    function endDrag() {
        if (!dragHandle) return
        dragHandle = ""
        dragFine = false
        controller.finishInteraction() // coalesce the full pointer gesture into one undo step
    }
    onModeChanged: if (dragHandle) endDrag()
    onVisibleChanged: if (!visible && dragHandle) endDrag()
    onFrameRectChanged: {
        if (dragHandle) endDrag() // no stale ROI mapping mid-gesture
        if (guides) guides.requestPaint()
    }
    onLinearChanged: if (guides) guides.requestPaint()
    onRadialChanged: if (guides) guides.requestPaint()
    onWidthChanged: if (guides) guides.requestPaint()
    onHeightChanged: if (guides) guides.requestPaint()

    Canvas {
        id: guides
        anchors.fill: parent
        enabled: false
        antialiasing: true
        onPaint: {
            const ctx=getContext("2d")
            ctx.clearRect(0,0,width,height)
            if (!root.active) return
            ctx.lineWidth=1.6
            ctx.strokeStyle="#e9bd65"
            ctx.beginPath()
            if (root.mode === "linear") {
                const a=root.radians(root.linear.angle)
                const tangentX=Math.cos(a), tangentY=-Math.sin(a)
                function guide(center) {
                    const point=root.linearAnchor({center:center,angle:root.linear.angle})
                    const v1=root.viewPoint(point.x - 2*tangentX,point.y - 2*tangentY)
                    const v2=root.viewPoint(point.x + 2*tangentX,point.y + 2*tangentY)
                    ctx.moveTo(v1.x,v1.y);ctx.lineTo(v2.x,v2.y)
                }
                guide(root.linear.center)
                ctx.stroke()
                ctx.strokeStyle="#85d4ed";ctx.lineWidth=1
                ctx.beginPath()
                guide(root.linear.center-root.linear.feather/2)
                guide(root.linear.center+root.linear.feather/2)
                ctx.stroke()
            } else {
                const a=root.radians(root.radial.angle)
                const c=Math.cos(a),s=Math.sin(a)
                function ellipse(factor) {
                    for (let j=0;j<=96;++j) {
                        const theta=j*2*Math.PI/96
                        const rx=root.radial.radiusX*Math.cos(theta)*factor
                        const ry=root.radial.radiusY*Math.sin(theta)*factor
                        const point=root.viewPoint(root.radial.centerX+rx*c-ry*s,
                                                   root.radial.centerY+rx*s+ry*c)
                        if (j===0) ctx.moveTo(point.x,point.y)
                        else ctx.lineTo(point.x,point.y)
                    }
                }
                ellipse(1)
                ctx.stroke()
                ctx.strokeStyle="#85d4ed";ctx.lineWidth=1
                ctx.beginPath();ellipse(1-root.radial.feather);ctx.stroke()
            }
        }
    }

    Repeater {
        model: root.mode === "linear" ? ["linearCenter","linearAngle","linearFeather","linearFeatherOpposite"]
             : root.mode === "radial" ? ["radialCenter","radialRadiusX","radialRadiusY","radialAngle","radialFeather","radialFeatherOpposite"] : []
        delegate: Item {
            id: handle
            required property var modelData
            objectName: "maskCanvasHandle_" + modelData
            readonly property point mapped: root.handleView(modelData)
            x: mapped.x - width/2; y: mapped.y - height/2
            width: 24; height: 24
            z: 2
            Rectangle {
                anchors.centerIn: parent
                width: handle.modelData.endsWith("Center") ? 13 : 11
                height: width; radius: width/2
                // Blue: rotation, green: feather, yellow: center/radius.
                color: handle.modelData.endsWith("Angle") ? "#85d4ed"
                      : handle.modelData.includes("Feather") ? "#79dcb0" : "#f7e6ab"
                border.width: 1; border.color: "#17202b"
            }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.CrossCursor
                hoverEnabled: true
                onPressed: function(mouse) {
                    const p=mapToItem(root,mouse.x,mouse.y)
                    root.beginDrag(handle.modelData,p.x,p.y,(mouse.modifiers & Qt.ShiftModifier) !== 0)
                }
                onPositionChanged: function(mouse) {
                    if (!pressed) return
                    const p=mapToItem(root,mouse.x,mouse.y)
                    root.moveDrag(p.x,p.y)
                }
                onReleased: root.endDrag()
                onCanceled: root.endDrag()
            }
        }
    }
}
