import QtQuick

Item {
    id: root
    property var redData: []
    property var greenData: []
    property var blueData: []
    property var lumaData: []
    property bool showLuma: false

    function requestPaint() { canvas.requestPaint() }
    onRedDataChanged: requestPaint(); onGreenDataChanged: requestPaint(); onBlueDataChanged: requestPaint(); onLumaDataChanged: requestPaint(); onShowLumaChanged: requestPaint()

    Rectangle { anchors.fill: parent; color: "#0d1015"; border.color: "#28313b"; radius: 8 }
    Canvas {
        id: canvas; anchors.fill: parent; anchors.margins: 8
        onPaint: {
            var ctx=getContext("2d"); ctx.globalAlpha=1; ctx.clearRect(0,0,width,height)
            function draw(data, color, alpha) {
                if (!data || data.length < 2) return
                var maxv=1; for(var i=0;i<data.length;i++) maxv=Math.max(maxv, Math.log(1+Number(data[i])))
                ctx.beginPath(); ctx.moveTo(0,height)
                for(var x=0;x<data.length;x++) {
                    var px=x/(data.length-1)*width; var py=height-(Math.log(1+Number(data[x]))/maxv)*height
                    ctx.lineTo(px,py)
                }
                ctx.lineTo(width,height); ctx.closePath(); ctx.globalAlpha=alpha; ctx.fillStyle=color; ctx.fill(); ctx.globalAlpha=1
            }
            if (root.showLuma) draw(root.lumaData,"#d9e0e8",0.72)
            else { draw(root.redData,"#ff5d67",0.36); draw(root.greenData,"#58d88c",0.34); draw(root.blueData,"#5c8fff",0.40) }
        }
    }
}
