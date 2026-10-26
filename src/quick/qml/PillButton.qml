import QtQuick
import QtQuick.Controls
import "qrc:/echo/quick/theme"

Button {
    id: control
    property bool primary: false
    implicitWidth: Math.max(64, contentItem.implicitWidth + 28)
    implicitHeight: 36
    padding: 8
    font.pixelSize: 12
    font.weight: primary ? Font.DemiBold : Font.Normal
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    contentItem: Text {
        text: control.text
        font: control.font
        color: control.primary ? Theme.c22172d : Theme.cd5c9e2
        opacity: control.enabled ? 1 : 0.4
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
    background: Rectangle {
        antialiasing: true
        radius: height / 2
        color: control.primary ? (control.down ? Theme.cac8bce : Theme.cd0b1f1) : control.down ? Theme.c44354f : control.hovered ? Theme.c362b42 : control.flat ? "transparent" : Theme.c292231
        border.color: control.visualFocus ? Theme.cd0b1f1 : control.flat || control.primary ? "transparent" : Theme.c20ffffff
        opacity: control.enabled ? 1 : 0.4
        Behavior on color {
            ColorAnimation {
                duration: 100
            }
        }
    }
}
