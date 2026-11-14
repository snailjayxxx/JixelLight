import QtQuick
import QtQuick.Controls

// Keep the photo viewport stable when fallback fonts or wrapping give the
// translated captions different heights. Both measures follow the same font
// and available width as the visible text, including narrow split panes.
Label {
    id: root
    required property string chineseText
    required property string englishText
    property bool chinese: true
    text: chinese ? chineseText : englishText
    wrapMode: Text.Wrap
    implicitHeight: Math.max(chineseMeasure.implicitHeight, englishMeasure.implicitHeight)
                    + topPadding + bottomPadding

    Text {
        id: chineseMeasure
        visible: false
        text: root.chineseText
        font: root.font
        width: Math.max(0, root.width - root.leftPadding - root.rightPadding)
        wrapMode: root.wrapMode
        textFormat: root.textFormat
        lineHeight: root.lineHeight
        lineHeightMode: root.lineHeightMode
    }
    Text {
        id: englishMeasure
        visible: false
        text: root.englishText
        font: root.font
        width: Math.max(0, root.width - root.leftPadding - root.rightPadding)
        wrapMode: root.wrapMode
        textFormat: root.textFormat
        lineHeight: root.lineHeight
        lineHeightMode: root.lineHeightMode
    }
}
