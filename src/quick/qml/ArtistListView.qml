import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "qrc:/echo/quick/theme"

ListView {
    id: artistList
    required property var controller
    signal contextRequested(Item sender, point position, int row)
    objectName: "artistList"
    Layout.fillWidth: true
    Layout.fillHeight: true
    visible: artistList.controller.page.kind === "artists" && artistList.controller.cards.count > 0
    model: visible ? artistList.controller.cards : null
    clip: true
    reuseItems: true
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: ScrollBar {}
    onMovementEnded: artistList.controller.setScroll(contentY)
    delegate: ItemDelegate {
        id: artistRow
        required property var item
        required property int index
        objectName: "artistRow" + index
        width: artistList.width
        height: 96
        padding: 0
        hoverEnabled: true
        Accessible.role: Accessible.ListItem
        Accessible.name: item.title + "，" + item.subtitle
        onClicked: artistList.controller.openCard(index)
        background: Rectangle {
            radius: 8
            color: artistRow.down ? Theme.c18ffffff : artistRow.hovered || artistRow.visualFocus ? Theme.c0bffffff : "transparent"
        }
        contentItem: Item {
            ArtistAvatar {
                anchors.left: parent.left
                anchors.leftMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                width: 64
                height: 64
                name: artistRow.item.title
            }
            Column {
                anchors.left: parent.left
                anchors.leftMargin: 94
                anchors.right: chevron.left
                anchors.rightMargin: 16
                anchors.verticalCenter: parent.verticalCenter
                spacing: 7
                Text {
                    width: parent.width
                    text: artistRow.item.title
                    color: Theme.ce6dfef
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                Text {
                    width: parent.width
                    text: artistRow.item.subtitle
                    color: Theme.c8d849b
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }
            }
            Text {
                id: chevron
                anchors.right: parent.right
                anchors.rightMargin: 16
                anchors.verticalCenter: parent.verticalCenter
                text: "›"
                color: Theme.c8d849b
                font.pixelSize: 28
            }
            Rectangle {
                anchors.left: parent.left
                anchors.leftMargin: 94
                anchors.right: parent.right
                anchors.rightMargin: 16
                anchors.bottom: parent.bottom
                height: 1
                color: Theme.c12ffffff
                visible: artistRow.index < artistList.count - 1
            }
        }
        TapHandler {
            acceptedButtons: Qt.RightButton
            onTapped: eventPoint => artistList.contextRequested(artistRow, eventPoint.position, artistRow.index)
        }
    }
}
