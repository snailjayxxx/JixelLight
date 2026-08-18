import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

RowLayout {
    id: root
    property string label: "Adjustment"
    property real from: -100
    property real to: 100
    property real value: 0
    property int decimals: 0
    signal edited(real newValue)
    spacing: 8

    Label { text: root.label; Layout.preferredWidth: 74; color: "#d7dde6"; font.pixelSize: 12 }
    Slider {
        Layout.fillWidth: true; from: root.from; to: root.to; value: root.value
        onMoved: root.edited(value)
    }
    Label {
        text: Number(root.value).toFixed(root.decimals)
        Layout.preferredWidth: 42; horizontalAlignment: Text.AlignRight
        color: "#aeb9c7"; font.family: "monospace"; font.pixelSize: 11
    }
}
