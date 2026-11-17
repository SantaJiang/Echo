import QtQuick
import QtQuick.Controls
import "qrc:/echo/quick/theme"

Slider {
    id: control
    implicitHeight: 24
    padding: 4
    property color accent: Theme.accent
    property bool followEnabled: false
    property real followedValue: 0
    property bool smoothFollow: false
    property bool reduceMotion: false
    property real shownValue: followedValue
    Behavior on shownValue {
        enabled: smoothFollow
        NumberAnimation { duration: Theme.motion(control.reduceMotion, Theme.motionShort) }
    }
    Binding {
        target: control
        property: "value"
        value: control.smoothFollow ? control.shownValue : control.followedValue
        when: control.followEnabled && !control.pressed
    }
    background: Rectangle {
        x: control.leftPadding
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: control.availableWidth
        height: control.hovered || control.pressed ? 5 : 3
        radius: 3
        color: Theme.pressed
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
