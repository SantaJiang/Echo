import QtQuick
import QtQuick.Layouts

Item {
    id: actions
    objectName: "dialogActions"
    default property alias buttons: row.data
    property bool spread: false
    property alias spacing: row.spacing
    implicitHeight: row.implicitHeight + 24
    RowLayout {
        id: row
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: 24
        anchors.rightMargin: 24
        spacing: 10
        Item { Layout.fillWidth: true; visible: !actions.spread }
    }
}
