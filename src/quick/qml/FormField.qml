import QtQuick
import QtQuick.Controls
import "qrc:/echo/quick/theme"
TextField {
    id: field
    color: Theme.cf3edf9
    placeholderTextColor: Theme.c8e849c
    font.pixelSize: 13
    implicitHeight: 36
    leftPadding: 12
    rightPadding: 12
    selectByMouse: true
    background: Rectangle {
        color: Theme.c17151d
        radius: 8
        antialiasing: true
        border.color: field.activeFocus ? Theme.cc9aaff : Theme.c51465f
    }
}
