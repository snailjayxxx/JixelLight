import QtQuick
import QtQuick.Controls

Item {
    id: root
    property var values: [0.0, 0.25, 0.5, 0.75, 1.0]
    property int channel: 0
    signal pointEdited(int point, real value)
    implicitHeight: 150

    Rectangle {
        anchors.fill: parent
        color: "#0b0f13"
        border.color: "#2d3742"
        radius: 5
    }

    Canvas {
        id: canvas
        anchors.fill: parent
        anchors.margins: 8
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.strokeStyle = "#26313b"
            ctx.lineWidth = 1
            for (let i = 1; i < 4; ++i) {
                const p = i / 4
                ctx.beginPath(); ctx.moveTo(width * p, 0); ctx.lineTo(width * p, height); ctx.stroke()
                ctx.beginPath(); ctx.moveTo(0, height * p); ctx.lineTo(width, height * p); ctx.stroke()
            }
            ctx.strokeStyle = "#4a5968"
            ctx.beginPath(); ctx.moveTo(0, height); ctx.lineTo(width, 0); ctx.stroke()

            if (!root.values || root.values.length < 5) return
            ctx.strokeStyle = root.channel === 1 ? "#ff7777" : root.channel === 2 ? "#71d68a" : root.channel === 3 ? "#78a9ff" : "#e6edf5"
            ctx.lineWidth = 2
            ctx.beginPath()
            for (let i = 0; i < 5; ++i) {
                const x = width * i / 4
                const y = height * (1.0 - Number(root.values[i]))
                if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y)
            }
            ctx.stroke()
        }
    }

    Repeater {
        model: 5
        delegate: Rectangle {
            id: point
            width: 12; height: 12; radius: 6
            border.width: 2
            border.color: "#e8eef5"
            color: root.channel === 1 ? "#d94c4c" : root.channel === 2 ? "#47b565" : root.channel === 3 ? "#4c78d9" : "#9ba8b6"
            x: 8 + (root.width - 16) * index / 4 - width / 2
            y: 8 + (root.height - 16) * (1.0 - Number(root.values[index])) - height / 2

            MouseArea {
                anchors.fill: parent
                anchors.margins: -7
                cursorShape: Qt.SizeVerCursor
                onPressed: function(mouse) { updateValue(mouse) }
                onPositionChanged: function(mouse) { if (pressed) updateValue(mouse) }
                function updateValue(mouse) {
                    const p = mapToItem(root, mouse.x, mouse.y)
                    const usable = Math.max(1, root.height - 16)
                    const value = Math.max(0.0, Math.min(1.0, 1.0 - (p.y - 8) / usable))
                    root.pointEdited(index, value)
                }
            }
        }
    }

    onValuesChanged: canvas.requestPaint()
    onChannelChanged: canvas.requestPaint()
}
