import QtQuick
import QtQuick.Controls
import "qrc:/echo/quick/theme"

Button {
    id: control
    property string symbol: "play"
    property bool active: false
    property bool prominent: false
    property bool filled: false
    property string label: ""
    property bool reduceMotion: false
    property int iconSize: prominent ? 26 : 24
    implicitWidth: prominent ? 56 : 44
    implicitHeight: implicitWidth
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    Accessible.name: label
    EchoToolTip {
        parent: control
        visible: control.hovered && control.enabled && control.visible && !control.down && control.label.length > 0
        text: control.label
    }
    background: Rectangle {
        antialiasing: true
        radius: width / 2
        border.width: control.visualFocus ? 2 : 0
        border.color: Theme.accent
        color: control.prominent ? Theme.textPrimary : control.down ? Theme.pressed : control.hovered ? Theme.hover : control.filled ? Theme.hover : "transparent"
        scale: control.down ? 0.94 : 1
        Behavior on scale {
            NumberAnimation {
                duration: Theme.motion(control.reduceMotion, Theme.motionShort)
            }
        }
        Behavior on color {
            ColorAnimation {
                duration: Theme.motion(control.reduceMotion, Theme.motionShort)
            }
        }
    }
    contentItem: Item {
        Image {
            anchors.centerIn: parent
            width: control.iconSize
            height: width
            source: Theme.icon(control.symbol, control.prominent ? "dark" : control.active ? "accent" : "light")
            sourceSize: Qt.size(width * Screen.devicePixelRatio, height * Screen.devicePixelRatio)
            fillMode: Image.PreserveAspectFit
            opacity: control.enabled ? 1 : 0.35
        }
    }
}
