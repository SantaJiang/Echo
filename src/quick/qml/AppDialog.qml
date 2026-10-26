import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "qrc:/echo/quick/theme"
Dialog {
    id: control
    parent: Overlay.overlay
    anchors.centerIn: parent
    default property alias bodyData: dialogBody.data
    width: Math.max(0, Math.min(540, parent ? parent.width - 40 : 540))
    height: Math.min(implicitHeight, parent ? Math.max(0, parent.height - 40) : implicitHeight)
    modal: true
    focus: true
    padding: 24
    bottomPadding: footer ? 0 : 24
    spacing: 18
    closePolicy: Popup.CloseOnEscape
    contentItem: ScrollView {
        id: bodyScroll
        objectName: "dialogBodyScroll"
        implicitHeight: dialogBody.implicitHeight
        contentWidth: availableWidth
        contentHeight: dialogBody.implicitHeight
        clip: true
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ColumnLayout {
            id: dialogBody
            width: bodyScroll.availableWidth
            spacing: 0
        }
    }
    header: Label {
        text: control.title
        color: Theme.cf3edf9
        font.pixelSize: 20
        font.weight: Font.DemiBold
        padding: 24
        bottomPadding: 0
    }
    background: Rectangle {
        color: Theme.c24202d
        radius: 16
        antialiasing: true
        border.color: Theme.c51465f
    }
    Overlay.modal: Rectangle { color: Theme.c990b0910 }
}
