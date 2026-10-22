import QtQuick
import QtQuick.Controls

Slider {
    id: control
    implicitHeight: 24
    padding: 4
    property color accent: "#eee9f5"
    background: Rectangle {
        x: control.leftPadding
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: control.availableWidth
        height: control.hovered || control.pressed ? 5 : 3
        radius: 3
        color: "#30ffffff"
        Rectangle {
            width: control.visualPosition * parent.width
            height: parent.height
            radius: 3
            color: control.accent
        }
    }
    handle: Rectangle {
        x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: control.pressed ? 12 : 9
        height: width
        radius: width / 2
        color: control.accent
        opacity: control.hovered || control.pressed ? 1 : 0
        Behavior on opacity {
            NumberAnimation {
                duration: 120
            }
        }
    }
}
