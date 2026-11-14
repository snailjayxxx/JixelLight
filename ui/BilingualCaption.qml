import QtQuick
import QtQuick.Controls

// Keep the photo viewport stable when fallback fonts or wrapping give the
// translated captions different heights. Both measures follow the same font
// and available width as the visible text, including narrow split panes.
Item {
    id: root
    required property string chineseText
    required property string englishText
    property bool chinese: true
    readonly property string text: chinese ? chineseText : englishText
    property alias font: display.font
    property alias color: display.color
    readonly property real contentHeight: display.contentHeight
    readonly property real topPadding: display.topPadding
    readonly property real bottomPadding: display.bottomPadding
    implicitWidth: display.implicitWidth
    implicitHeight: Math.max(chineseMeasure.implicitHeight, englishMeasure.implicitHeight)
                    + display.topPadding + display.bottomPadding

    // The Label retains its normal style and ancestor/window font resolution.
    // Only the plain layout Item owns the writable implicit dimensions.
    Label {
        id: display
        anchors.fill: parent
        text: root.text
        wrapMode: Text.Wrap
    }

    Text {
        id: chineseMeasure
        visible: false
        text: root.chineseText
        font: display.font
        width: Math.max(0, root.width - display.leftPadding - display.rightPadding)
        wrapMode: display.wrapMode
        textFormat: display.textFormat
        lineHeight: display.lineHeight
        lineHeightMode: display.lineHeightMode
    }
    Text {
        id: englishMeasure
        visible: false
        text: root.englishText
        font: display.font
        width: Math.max(0, root.width - display.leftPadding - display.rightPadding)
        wrapMode: display.wrapMode
        textFormat: display.textFormat
        lineHeight: display.lineHeight
        lineHeightMode: display.lineHeightMode
    }
}
