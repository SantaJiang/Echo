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
        color: control.primary ? Theme.accentText : Theme.textSecondary
        opacity: control.enabled ? 1 : 0.4
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
    background: Rectangle {
        antialiasing: true
        radius: height / 2
        color: control.primary ? (control.down ? Theme.accent : Theme.accentStrong) : control.down ? Theme.selection : control.hovered ? Theme.selection : control.flat ? "transparent" : Theme.surface
        border.color: control.visualFocus ? Theme.accentStrong : control.flat || control.primary ? "transparent" : Theme.hairline
        opacity: control.enabled ? 1 : 0.4
        Behavior on color {
            ColorAnimation {
                duration: 100
            }
        }
    }
}
