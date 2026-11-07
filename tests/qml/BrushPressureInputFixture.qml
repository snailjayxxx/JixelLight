import QtQuick
import Private.Input 1.0

Rectangle {
    id: root
    width:320;height:200;color:"#202830"
    property var records:[]
    property bool routeEnabled:true
    function record(kind,x,y,pressure,active) {
        let next=records.slice();next.push({kind:kind,x:x,y:y,pressure:pressure,active:active});records=next
    }
    MouseArea {
        id: mouse
        anchors.fill:parent
        onPressed:function(event){root.record("mouse-press",event.x,event.y,1,pressed)}
        onPositionChanged:function(event){if(pressed)root.record("mouse-move",event.x,event.y,1,pressed)}
        onReleased:function(event){root.record("mouse-release",event.x,event.y,1,pressed)}
    }
    Item {
        x:40;y:30;width:180;height:140;clip:true
        Item { id:canvas;width:240;height:140 }
    }
    TabletInput {
        id: tablet
        objectName:"tabletRouter"
        target:canvas;enabled:root.routeEnabled;blocked:mouse.pressed
        onBegan:function(x,y,pressure){root.record("tablet-press",x,y,pressure,pressed)}
        onMoved:function(x,y,pressure){root.record("tablet-move",x,y,pressure,pressed)}
        onEnded:function(x,y,pressure){root.record("tablet-release",x,y,pressure,pressed)}
        onCanceled:root.record("tablet-cancel",0,0,0,pressed)
    }
}
