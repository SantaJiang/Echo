import QtQuick
import QtQuick.Controls
TextField {
    id: field
    color: "#f3edf9"
    placeholderTextColor: "#8e849c"
    font.pixelSize: 13
    implicitHeight: 36
    leftPadding: 12
    rightPadding: 12
    selectByMouse: true
    background: Rectangle {
        color: "#17151d"
        radius: 8
        antialiasing: true
        border.color: field.activeFocus ? "#c9aaff" : "#51465f"
    }
}
