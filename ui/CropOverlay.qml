import QtQuick

Item {
    id: root
    property rect selection: Qt.rect(0,0,1,1)
    property real aspectRatio: 0
    readonly property real boxLeft: selection.x * width
    readonly property real boxTop: selection.y * height
    readonly property real boxWidth: selection.width * width
    readonly property real boxHeight: selection.height * height
    function clamp(value, low, high) { return Math.max(low,Math.min(high,value)) }
    function setAspect(ratio) {
        aspectRatio=ratio
        if (ratio <= 0 || width <= 0 || height <= 0) return
        const r=ratio*height/width
        let w=selection.width, h=w/r
        if (h>selection.height) { h=selection.height; w=h*r }
        selection=Qt.rect(selection.x+(selection.width-w)/2,selection.y+(selection.height-h)/2,w,h)
    }
    function resizeBox(start, hx, hy, dx, dy) {
        const minW=Math.min(.05,12/Math.max(1,width)), minH=Math.min(.05,12/Math.max(1,height))
        let l=start.x, t=start.y, r=start.x+start.width, b=start.y+start.height
        if (hx<0) l=clamp(l+dx,0,r-minW)
        if (hx>0) r=clamp(r+dx,l+minW,1)
        if (hy<0) t=clamp(t+dy,0,b-minH)
        if (hy>0) b=clamp(b+dy,t+minH,1)
        if (aspectRatio>0) {
            const ratio=aspectRatio*height/width
            const ax=hx<0 ? start.x+start.width : hx>0 ? start.x : start.x+start.width/2
            const ay=hy<0 ? start.y+start.height : hy>0 ? start.y : start.y+start.height/2
            const maxW=hx===0 ? 2*Math.min(ax,1-ax) : hx<0 ? ax : 1-ax
            const maxH=hy===0 ? 2*Math.min(ay,1-ay) : hy<0 ? ay : 1-ay
            let w=(hx===0 || (hy!==0 && Math.abs(dy*height)>Math.abs(dx*width))) ? (b-t)*ratio : r-l
            w=Math.min(maxW,maxH*ratio,Math.max(minW,minH*ratio,w))
            const h=w/ratio
            l=hx<0 ? ax-w : hx>0 ? ax : ax-w/2
            t=hy<0 ? ay-h : hy>0 ? ay : ay-h/2
            r=l+w; b=t+h
        }
        selection=Qt.rect(l,t,r-l,b-t)
    }
    Rectangle { x: 0; y: 0; width: parent.width; height: root.boxTop; color: "#99000000" }
    Rectangle { x: 0; y: root.boxTop+root.boxHeight; width: parent.width; height: Math.max(0,parent.height-y); color: "#99000000" }
    Rectangle { x: 0; y: root.boxTop; width: root.boxLeft; height: root.boxHeight; color: "#99000000" }
    Rectangle { x: root.boxLeft+root.boxWidth; y: root.boxTop; width: Math.max(0,parent.width-x); height: root.boxHeight; color: "#99000000" }
    Rectangle {
        x: root.boxLeft; y: root.boxTop; width: root.boxWidth; height: root.boxHeight
        color: "transparent"; border.color: "white"; border.width: 1
        Repeater {
            model: 2
            Rectangle { required property int index; x: (index+1)*root.boxWidth/3; width: 1; height: root.boxHeight; color: "#88ffffff" }
        }
        Repeater {
            model: 2
            Rectangle { required property int index; y: (index+1)*root.boxHeight/3; height: 1; width: root.boxWidth; color: "#88ffffff" }
        }
    }
    MouseArea {
        x: root.boxLeft; y: root.boxTop; width: root.boxWidth; height: root.boxHeight
        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
        property point origin
        property rect start
        onPressed: function(mouse) { origin=mapToItem(root,mouse.x,mouse.y); start=root.selection }
        onPositionChanged: function(mouse) {
            if (!pressed) return
            const position=mapToItem(root,mouse.x,mouse.y)
            root.selection=Qt.rect(root.clamp(start.x+(position.x-origin.x)/root.width,0,1-start.width),
                root.clamp(start.y+(position.y-origin.y)/root.height,0,1-start.height),start.width,start.height)
        }
    }
    Repeater {
        model: [{x:-1,y:-1},{x:0,y:-1},{x:1,y:-1},{x:1,y:0},{x:1,y:1},{x:0,y:1},{x:-1,y:1},{x:-1,y:0}]
        Rectangle {
            id: handle
            required property var modelData
            required property int index
            objectName: "cropHandle"+index
            x: root.boxLeft+(modelData.x+1)*root.boxWidth/2-width/2
            y: root.boxTop+(modelData.y+1)*root.boxHeight/2-height/2
            width: 12; height: 12; radius: 2; color: "#f5f7fa"; border.color: "#293440"
            MouseArea {
                anchors.fill: parent; anchors.margins: -5
                cursorShape: handle.modelData.x===0 ? Qt.SizeVerCursor : handle.modelData.y===0 ? Qt.SizeHorCursor : handle.modelData.x===handle.modelData.y ? Qt.SizeFDiagCursor : Qt.SizeBDiagCursor
                property point origin
                property rect start
                onPressed: function(mouse) { origin=mapToItem(root,mouse.x,mouse.y); start=root.selection }
                onPositionChanged: function(mouse) {
                    if (!pressed) return
                    const p=mapToItem(root,mouse.x,mouse.y)
                    root.resizeBox(start,handle.modelData.x,handle.modelData.y,(p.x-origin.x)/root.width,(p.y-origin.y)/root.height)
                }
            }
        }
    }
}
