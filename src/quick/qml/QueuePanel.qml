import QtQuick
import QtQuick.Effects
import QtQuick.Controls
import QtQuick.Layouts
import "qrc:/echo/quick/theme"

Rectangle {
    id: panel
    required property var controller
    property bool opened: false
    property bool reduceMotion: false
    property bool dismissable: false
    property string listObjectName: "queueList"
    property real openness: opened ? 1 : 0
    signal closeRequested()
    objectName: "queuePanel"
    color: "transparent"
    radius: 14
    Rectangle {
        id: surface
        anchors.fill: parent
        color: Theme.sidebar
        radius: panel.radius
        border.color: Theme.hairline
        layer.enabled: true
        visible: false
    }
    MultiEffect {
        objectName: "queueShadow"
        anchors.fill: surface
        source: surface
        z: -1
        shadowEnabled: true
        blurMax: 24
        shadowBlur: 1
        shadowOpacity: 0.30
        shadowColor: "black"
        shadowVerticalOffset: 6
    }
    visible: openness > 0
    enabled: opened
    Behavior on openness {
        NumberAnimation {
            duration: Theme.motion(panel.reduceMotion, 240)
            easing.type: Easing.OutCubic
        }
    }
    // Empty panel space must consume input too, so the outside dismiss area
    // cannot accidentally close the queue or seek a lyric behind it.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onWheel: wheel => wheel.accepted = true
    }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 18
        RowLayout {
            Layout.fillWidth: true
            Text {
                text: "播放队列"
                color: Theme.textPrimary
                font.pixelSize: 18
                font.weight: Font.DemiBold
            }
            Item {
                Layout.fillWidth: true
            }
            PillButton {
                text: "清空后续"
                flat: true
                onClicked: panel.controller.queueAction("clear")
            }
            ActionButton {
                objectName: "queueCloseButton"
                visible: panel.dismissable
                implicitWidth: 30
                implicitHeight: 30
                iconSize: 16
                symbol: "close"
                label: "收起播放队列"
                onClicked: panel.closeRequested()
            }
        }
        ListView {
            id: queueList
            objectName: panel.listObjectName
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: panel.controller.queue
            boundsBehavior: Flickable.StopAtBounds
            clip: true
            reuseItems: true
            spacing: 5
            ScrollBar.vertical: ScrollBar {}
            Text {
                anchors.centerIn: parent
                width: Math.max(0, parent.width - 24)
                visible: queueList.count === 0
                text: "播放队列为空"
                horizontalAlignment: Text.AlignHCenter
                color: Theme.textSecondary
                font.pixelSize: 13
            }
            delegate: Rectangle {
                id: queueRow
                required property var item
                required property int index
                width: queueList.width
                height: 58
                radius: 8
                color: item.current ? Theme.selection : qHover.hovered ? Theme.hairline : "transparent"
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 7
                    spacing: 10
                    Cover {
                        Layout.preferredWidth: 38
                        Layout.preferredHeight: 38
                        artwork: queueRow.item.artwork
                        radius: 6
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Text {
                            Layout.fillWidth: true
                            text: queueRow.item.title
                            color: Theme.textPrimary
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            text: queueRow.item.artist
                            color: Theme.textMuted
                            font.pixelSize: 11
                            elide: Text.ElideRight
                        }
                    }
                }
                HoverHandler {
                    id: qHover
                }
                z: queueMouse.pressed ? 2 : 0
                transform: Translate {
                    y: queueMouse.dragOffset
                }
                MouseArea {
                    id: queueMouse
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    preventStealing: true
                    property real pressY: 0
                    property real dragOffset: 0
                    property int sourceRow: -1
                    onPressed: mouse => {
                        pressY = mapToItem(panel, mouse.x, mouse.y).y;
                        sourceRow = queueRow.index;
                        dragOffset = 0;
                    }
                    onPositionChanged: mouse => {
                        if (pressed && (pressedButtons & Qt.LeftButton))
                            dragOffset = mapToItem(panel, mouse.x, mouse.y).y - pressY;
                    }
                    onReleased: mouse => {
                        const offset = dragOffset;
                        dragOffset = 0;
                        if (mouse.button === Qt.LeftButton && Math.abs(offset) > 20)
                            panel.controller.queueAction("move", sourceRow, Math.max(0, Math.min(queueList.count - 1, sourceRow + Math.round(offset / 63))));
                    }
                    onCanceled: dragOffset = 0
                    onDoubleClicked: mouse => {
                        if (mouse.button === Qt.LeftButton)
                            panel.controller.queueAction("play", queueRow.index);
                    }
                    onClicked: mouse => {
                        if (mouse.button === Qt.RightButton)
                            queueMenu.popup();
                    }
                }
                Menu {
                    id: queueMenu
                    MenuItem {
                        text: "播放"
                        onTriggered: panel.controller.queueAction("play", queueRow.index)
                    }
                    MenuItem {
                        text: "从队列移除"
                        onTriggered: panel.controller.queueAction("remove", queueRow.index)
                    }
                    MenuItem {
                        text: "上移"
                        enabled: queueRow.index > 0
                        onTriggered: panel.controller.queueAction("move", queueRow.index, queueRow.index - 1)
                    }
                    MenuItem {
                        text: "下移"
                        enabled: queueRow.index + 1 < queueList.count
                        onTriggered: panel.controller.queueAction("move", queueRow.index, queueRow.index + 1)
                    }
                }
            }
        }
    }
}
