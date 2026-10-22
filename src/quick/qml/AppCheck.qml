import QtQuick
import QtQuick.Controls
CheckBox {
    id: control
    property string preference: ""
    checked: Boolean(client.preferences[preference])
    onToggled: client.setPreference(preference, checked)
    spacing: 12
    padding: 4
    indicator: Rectangle {
        x: control.leftPadding
        y: (control.height-height)/2
        width: 20; height: 20; radius: 6
        antialiasing: true
        color: control.checked ? "#c9aaff" : "#17151d"
        border.color: control.visualFocus ? "#eee3ff" : "#71607e"
        Text { anchors.centerIn: parent; text: "✓"; visible: control.checked; color: "#21172c"; font.pixelSize: 15 }
    }
    contentItem: Text {
        text: control.text
        font.pixelSize: 13
        color: "#eee3ff"
        leftPadding: control.indicator.width + control.spacing
        verticalAlignment: Text.AlignVCenter
        wrapMode: Text.Wrap
    }
}
