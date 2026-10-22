import QtQuick
import QtQuick.Controls

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
        color: control.primary ? "#22172d" : "#d5c9e2"
        opacity: control.enabled ? 1 : 0.4
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
    background: Rectangle {
        antialiasing: true
        radius: height / 2
        color: control.primary ? (control.down ? "#ac8bce" : "#d0b1f1") : control.down ? "#44354f" : control.hovered ? "#362b42" : control.flat ? "transparent" : "#292231"
        border.color: control.visualFocus ? "#d0b1f1" : control.flat || control.primary ? "transparent" : "#20ffffff"
        opacity: control.enabled ? 1 : 0.4
        Behavior on color {
            ColorAnimation {
                duration: 100
            }
        }
    }
}
