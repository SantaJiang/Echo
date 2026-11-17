import QtQuick
import QtQuick.Controls
import "qrc:/echo/quick/theme"
TextField {
    id: field
    color: Theme.textPrimary
    placeholderTextColor: Theme.textMuted
    font.pixelSize: 13
    implicitHeight: 36
    leftPadding: 12
    rightPadding: 12
    selectByMouse: true
    background: Rectangle {
        color: Theme.canvas
        radius: 8
        antialiasing: true
        border.color: field.activeFocus ? Theme.accent : Theme.border
    }
}
