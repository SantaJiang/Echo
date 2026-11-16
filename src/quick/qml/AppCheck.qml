import QtQuick
import QtQuick.Controls
import "qrc:/echo/quick/theme"
CheckBox {
    id: control
    required property var controller
    property string preference: ""
    checked: Boolean(control.controller.preferences[preference])
    onToggled: control.controller.setPreference(preference, checked)
    spacing: 12
    padding: 4
    indicator: Rectangle {
        x: control.leftPadding
        y: (control.height-height)/2
        width: 20; height: 20; radius: 6
        antialiasing: true
        color: control.checked ? Theme.cc9aaff : Theme.c17151d
        border.color: control.visualFocus ? Theme.ceee3ff : Theme.c71607e
        Text { anchors.centerIn: parent; text: "✓"; visible: control.checked; color: Theme.c21172c; font.pixelSize: 15 }
    }
    contentItem: Text {
        text: control.text
        font.pixelSize: 13
        color: Theme.ceee3ff
        leftPadding: control.indicator.width + control.spacing
        verticalAlignment: Text.AlignVCenter
        wrapMode: Text.Wrap
    }
}
