import QtQuick

Item {
    id: root
    property string mode: "waveform"
    property string imageUrl: ""
    property bool current: false
    Rectangle { anchors.fill: parent; color: "#080b0f"; border.color: "#28313b"; radius: 6 }
    Item {
        id: plot
        anchors.centerIn: parent
        width: root.mode==="vectorscope" ? Math.min(parent.width-24,parent.height-20) : parent.width-40
        height: root.mode==="vectorscope" ? width : parent.height-20
        Image {
            anchors.fill: parent; source: root.imageUrl
            cache: false; asynchronous: true; smooth: true
            opacity: root.current ? 1 : .3
            sourceSize: Qt.size(Math.max(1,width*Screen.devicePixelRatio),Math.max(1,height*Screen.devicePixelRatio))
        }
        Canvas {
            id: grid; anchors.fill: parent
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            Connections { target: root; function onModeChanged() { grid.requestPaint() } }
            onPaint: {
                const ctx=getContext("2d"); ctx.clearRect(0,0,width,height)
                ctx.strokeStyle="#506070"; ctx.lineWidth=1; ctx.font="9px sans-serif"
                ctx.fillStyle="#a0acb8"
                if (root.mode==="vectorscope") {
                    ctx.beginPath(); ctx.moveTo(0,height/2); ctx.lineTo(width,height/2)
                    ctx.moveTo(width/2,0); ctx.lineTo(width/2,height); ctx.stroke()
                    ctx.beginPath(); ctx.ellipse(width*.25,height*.25,width*.5,height*.5); ctx.stroke()
                    const targets=[{label:"R",r:1,g:0,b:0,color:"#ff7777"},{label:"Y",r:1,g:1,b:0,color:"#eeee77"},
                        {label:"G",r:0,g:1,b:0,color:"#77ee99"},{label:"C",r:0,g:1,b:1,color:"#77eeee"},
                        {label:"B",r:0,g:0,b:1,color:"#88aaff"},{label:"M",r:1,g:0,b:1,color:"#dd88ee"}]
                    for (const c of targets) {
                        const y=.2126*c.r+.7152*c.g+.0722*c.b
                        const x=(.5+(c.b-y)/1.8556)*(width-1), py=(.5-(c.r-y)/1.5748)*(height-1)
                        ctx.fillStyle=c.color; ctx.strokeStyle=c.color
                        ctx.strokeRect(Math.max(1,Math.min(width-5,x-2)),Math.max(1,Math.min(height-5,py-2)),4,4)
                        ctx.fillText(c.label,Math.max(2,Math.min(width-9,x+4)),Math.max(9,Math.min(height-2,py+10)))
                    }
                } else {
                    for (let i=0;i<=4;++i) {
                        const y=i*(height-1)/4
                        ctx.beginPath();ctx.moveTo(0,y);ctx.lineTo(width,y);ctx.stroke()
                    }
                    if (root.mode==="parade") {
                        for (let i=1;i<3;++i) {ctx.beginPath();ctx.moveTo(i*width/3,0);ctx.lineTo(i*width/3,height);ctx.stroke()}
                        for (let i=0;i<3;++i) {ctx.fillStyle=["#ff7777","#77ee99","#88aaff"][i];ctx.fillText(["R","G","B"][i],i*width/3+4,11)}
                    }
                }
            }
        }
    }
    Repeater {
        model: ["100","75","50","25","0"]
        Text {
            required property string modelData; required property int index
            visible: root.mode!=="vectorscope"; x: 2; y: 6+index*(root.height-20)/4
            text: modelData; color: "#8a98a5"; font.pixelSize: 8
        }
    }
}
