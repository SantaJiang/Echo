import QtQuick
import QtQuick.Controls
Dialog {
    id: control
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(540, parent ? parent.width - 40 : 540)
    modal: true
    focus: true
    padding: 24
    spacing: 18
    closePolicy: Popup.CloseOnEscape
    header: Label {
        text: control.title
        color: "#f3edf9"
        font.pixelSize: 20
        font.weight: Font.DemiBold
        padding: 24
        bottomPadding: 0
    }
    background: Rectangle {
        color: "#24202d"
        radius: 16
        antialiasing: true
        border.color: "#51465f"
    }
    Overlay.modal: Rectangle { color: "#990b0910" }
}
