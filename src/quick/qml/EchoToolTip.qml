import QtQuick
import QtQuick.Controls

ToolTip {
    id: hint
    objectName: "echoToolTip"
    delay: 600
    timeout: 5000
    margins: 8
    leftPadding: 10
    rightPadding: 10
    topPadding: 7
    bottomPadding: 7
    font.pixelSize: 12
    font.weight: Font.Normal
    implicitWidth: Math.min(320, implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: implicitContentHeight + topPadding + bottomPadding
    x: Math.round((parent.width - width) / 2)
    y: -height - 8
    onAboutToShow: {
        const top = parent.mapToItem(null, 0, 0).y;
        y = top >= implicitHeight + 16 ? -implicitHeight - 8 : parent.height + 8;
    }
    contentItem: Text {
        text: hint.text
        font: hint.font
        color: "#eee8f5"
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
    background: Rectangle {
        objectName: "tooltipBackground"
        color: "#302938"
        border.color: "#51465f"
        border.width: 1
        radius: 7
        antialiasing: true
    }
    enter: Transition {
        NumberAnimation {
            property: "opacity"
            from: 0
            to: 1
            duration: 90
        }
    }
    exit: Transition {
        NumberAnimation {
            property: "opacity"
            from: 1
            to: 0
            duration: 70
        }
    }
}
