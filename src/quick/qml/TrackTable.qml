import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "qrc:/echo/quick/theme"

Item {
    id: tableArea
    required property var controller
    property bool queueOpen: false
    property alias view: trackList
    readonly property real trackWidth: Math.max(trackList.width, controller.columns.reduce((a, b) => a + Number(b), 0))
    signal contextRequested(Item sender, point position, int row)
    signal queueDismissRequested()
    Layout.fillWidth: true
    Layout.fillHeight: true
    visible: tableArea.controller.tracks.count > 0
    clip: true
    TrackTableHeader {
        id: header
        controller: tableArea.controller
        trackWidth: tableArea.trackWidth
        x: -trackList.contentX
    }
    ListView {
        id: trackList
        objectName: "trackList"
        Accessible.role: Accessible.List
        Accessible.name: "曲目"
        anchors.fill: parent
        anchors.topMargin: 34
        anchors.bottomMargin: 8
        model: tableArea.controller.tracks
        contentWidth: tableArea.trackWidth
        flickableDirection: Flickable.AutoFlickIfNeeded
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        reuseItems: true
        cacheBuffer: 160
        keyNavigationEnabled: false
        WheelHandler {
            // Physical wheel notches should move a predictable
            // three rows. Touchpad pixel scrolling stays native.
            acceptedDevices: PointerDevice.Mouse
            target: null
            onWheel: event => {
                const distance = event.pixelDelta.y || event.angleDelta.y / 120 * 48 * 3;
                if (!distance)
                    return;
                trackList.cancelFlick();
                if (event.modifiers & Qt.ShiftModifier)
                    trackList.contentX = Math.max(0, Math.min(trackList.contentWidth - trackList.width,
                        trackList.contentX - distance));
                else {
                    trackList.contentY = Math.max(0, Math.min(trackList.contentHeight - trackList.height,
                        trackList.contentY - distance));
                    tableArea.controller.setScroll(trackList.contentY);
                }
            }
        }
        TapHandler {
            enabled: tableArea.queueOpen
            onTapped: if (trackList.indexAt(point.position.x + trackList.contentX,
                point.position.y + trackList.contentY) < 0) tableArea.queueDismissRequested()
        }
        ScrollBar.vertical: ScrollBar {}
        ScrollBar.horizontal: ScrollBar {}
        onMovementEnded: tableArea.controller.setScroll(contentY)
        Keys.onPressed: event => {
            if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                tableArea.controller.playRow(currentIndex);
                event.accepted = true;
            } else if (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace) {
                tableArea.controller.removeSelected();
                event.accepted = true;
            } else if (event.key === Qt.Key_Up || event.key === Qt.Key_Down) {
                let next = Math.max(0, Math.min(count - 1, currentIndex + (event.key === Qt.Key_Up ? -1 : 1)));
                if (event.modifiers & Qt.AltModifier)
                    tableArea.controller.moveTrack(currentIndex, next);
                else {
                    currentIndex = next;
                    tableArea.controller.selectRow(next, event.modifiers);
                    positionViewAtIndex(next, ListView.Contain);
                }
                event.accepted = true;
            }
        }
        delegate: Rectangle {
            id: trackRow
            required property var item
            required property int index
            readonly property bool selected: {
                let revision = tableArea.controller.selectionRevision;
                return tableArea.controller.isSelected(index);
            }
            width: tableArea.trackWidth
            height: 48
            radius: 7
            color: selected ? Theme.selection : rowHover.hovered ? Theme.hover : index % 2 ? Theme.stripe : "transparent"
            opacity: item.missing ? 0.45 : 1
            Accessible.role: Accessible.ListItem
            Accessible.name: item.title + "，" + item.artist
            Accessible.onPressAction: tableArea.controller.playRow(index)
            Row {
                anchors.verticalCenter: parent.verticalCenter
                Item {
                    width: Number(tableArea.controller.columns[0])
                    height: 48
                    ActionButton {
                        anchors.centerIn: parent
                        symbol: trackRow.item.favorite ? "heartFilled" : "heart"
                        active: trackRow.item.favorite
                        opacity: trackRow.item.favorite || rowHover.hovered ? 1 : 0
                        label: trackRow.item.favorite ? "取消喜欢" : "喜欢"
                        onClicked: tableArea.controller.favoriteRow(trackRow.index)
                    }
                }
                RowLayout {
                    width: Number(tableArea.controller.columns[1])
                    height: 48
                    spacing: 10
                    Cover {
                        Layout.preferredWidth: 32
                        Layout.preferredHeight: 32
                        radius: 5
                        artwork: trackRow.item.artwork
                    }
                    Text {
                        Layout.fillWidth: true
                        text: trackRow.item.title
                        color: trackRow.item.playing ? Theme.accentStrong : Theme.textPrimary
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }
                    Item {
                        Layout.preferredWidth: 10
                    }
                }
                Text {
                    width: Number(tableArea.controller.columns[2])
                    height: 48
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: 9
                    rightPadding: 12
                    text: trackRow.item.artist
                    color: Theme.textSecondary
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }
                Text {
                    width: Number(tableArea.controller.columns[3])
                    height: 48
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: 9
                    rightPadding: 12
                    text: trackRow.item.album
                    color: Theme.textSecondary
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }
                Text {
                    width: Number(tableArea.controller.columns[4])
                    height: 48
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: 9
                    text: trackRow.item.duration
                    color: Theme.textMuted
                    font.pixelSize: 11
                }
            }
            HoverHandler {
                id: rowHover
            }
            TapHandler {
                acceptedButtons: Qt.LeftButton
                onTapped: eventPoint => {
                    if (point.position.x < Number(tableArea.controller.columns[0]))
                        return;
                    trackList.currentIndex = index;
                    trackList.forceActiveFocus();
                    tableArea.controller.selectRow(index, point.modifiers);
                }
                onDoubleTapped: if (point.position.x >= Number(tableArea.controller.columns[0]))
                    tableArea.controller.playRow(index)
            }
            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: eventPoint => {
                    trackList.currentIndex = index;
                    tableArea.contextRequested(trackRow, eventPoint.position, index);
                }
            }
        }
    }
}
