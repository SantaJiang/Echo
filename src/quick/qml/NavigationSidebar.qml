import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "qrc:/echo/quick/theme"

Rectangle {
    id: sidebar
    objectName: "navigationSidebar"
    required property var controller
    property bool queueOpen: false
    property real topInset: 48
    // Tints over the system backdrop instead of painting an opaque column.
    property bool glass: false
    signal contextRequested(Item sender, point position, int row)
    signal queueDismissRequested()
    color: sidebar.glass ? Theme.sidebarGlass : Theme.sidebar
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 18
        anchors.topMargin: sidebar.topInset
        spacing: 8
        Text {
            text: "资料库"
            color: Theme.textMuted
            font.pixelSize: 11
            font.weight: Font.DemiBold
            Layout.leftMargin: 12
            Layout.bottomMargin: 8
        }
        ListView {
            id: navigation
            objectName: "navigationView"
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: sidebar.controller.navigation
            clip: true
            spacing: 4
            TapHandler {
                enabled: sidebar.queueOpen
                onTapped: if (navigation.indexAt(point.position.x + navigation.contentX,
                    point.position.y + navigation.contentY) < 0) sidebar.queueDismissRequested()
            }
            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
            }
            delegate: Rectangle {
                id: navRow
                required property var item
                required property int index
                readonly property bool selected: sidebar.controller.page.kind === item.kind && (item.kind !== "playlist" || sidebar.controller.page.key === item.key)
                width: navigation.width
                height: item.kind === "playlistSection" ? 52 : 38
                radius: 9
                color: item.kind === "playlistSection" ? "transparent" : selected ? Theme.selection : navHover.hovered ? Theme.hover : "transparent"
                Accessible.role: Accessible.Button
                Accessible.name: item.title
                Accessible.onPressAction: if (item.kind !== "listFolder" && item.kind !== "playlistSection")
                    sidebar.controller.navigate(item.kind, item.key, item.title)
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12 + item.depth * 12
                    anchors.rightMargin: 8
                    spacing: 11
                    Image {
                        visible: item.kind !== "playlistSection"
                        source: Theme.icon(item.icon, navRow.selected ? "accent" : "light")
                        sourceSize: Qt.size(16, 16)
                        Layout.preferredWidth: 16
                        Layout.preferredHeight: 16
                        opacity: navRow.selected ? 1 : 0.65
                    }
                    Text {
                        Layout.fillWidth: true
                        text: item.title
                        font.pixelSize: item.kind === "playlistSection" ? 11 : 13
                        font.weight: navRow.selected ? Font.DemiBold : Font.Normal
                        color: item.kind === "playlistSection" ? Theme.textMuted : navRow.selected ? Theme.textPrimary : Theme.textSecondary
                        elide: Text.ElideRight
                    }
                }
                HoverHandler {
                    id: navHover
                }
                TapHandler {
                    acceptedButtons: Qt.LeftButton
                    onTapped: if (item.kind !== "listFolder" && item.kind !== "playlistSection")
                        sidebar.controller.navigate(item.kind, item.kind === "playlist" ? item.key : "", item.title)
                }
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: eventPoint => sidebar.contextRequested(navRow, eventPoint.position, index)
                }
                ActionButton {
                    objectName: "newPlaylistButton"
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    visible: item.kind === "playlistSection"
                    implicitWidth: 32
                    implicitHeight: 32
                    iconSize: 16
                    symbol: "plus"
                    label: "新建歌单"
                    onClicked: sidebar.controller.action("newPlaylist")
                }
            }
        }
        Text {
            Layout.fillWidth: true
            text: sidebar.controller.status
            visible: text.length > 0
            wrapMode: Text.Wrap
            color: Theme.textMuted
            font.pixelSize: 11
            maximumLineCount: 3
            elide: Text.ElideRight
        }
        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: Theme.hover
            Layout.topMargin: 8
            Layout.bottomMargin: 6
        }
        Rectangle {
            objectName: "settingsNavigation"
            Layout.fillWidth: true
            implicitHeight: 38
            radius: 9
            color: settingsHover.hovered ? Theme.hover : "transparent"
            Accessible.role: Accessible.Button
            Accessible.name: "设置"
            Accessible.onPressAction: sidebar.controller.showSettings()
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                spacing: 11
                Image {
                    source: Theme.icon("settings", "light")
                    sourceSize: Qt.size(16, 16)
                    Layout.preferredWidth: 16
                    Layout.preferredHeight: 16
                    opacity: 0.65
                }
                Text {
                    Layout.fillWidth: true
                    text: "设置"
                    color: Theme.textSecondary
                    font.pixelSize: 13
                }
            }
            HoverHandler {
                id: settingsHover
            }
            TapHandler {
                onTapped: sidebar.controller.showSettings()
            }
        }
    }
}
