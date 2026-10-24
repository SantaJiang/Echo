import QtQuick
import QtQuick.Controls

Item {
    id: root
    property bool expanded: true
    clip: true
    Rectangle {
        anchors.fill: parent
        color: "#17151e"
    }
    PillButton {
        anchors.centerIn: parent
        text: "展开播放详情"
        onClicked: root.expanded = true
    }
    NowPlaying {
        width: root.width
        height: root.height
        playback: root.playbackObject
        y: root.expanded ? 0 : height
        visible: y < height
        enabled: root.expanded
        onCollapse: root.expanded = false
        Behavior on y {
            NumberAnimation {
                duration: 360
                easing.type: Easing.OutCubic
            }
        }
    }
    PlayerBar {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        playback: root.playbackObject
        onExpansionRequested: root.expanded = !root.expanded
    }
    readonly property var playbackObject: playback
}
