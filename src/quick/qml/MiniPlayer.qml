import QtQuick
import QtQuick.Controls
import QtQuick.Window
import QtQuick.Layouts
Window {
    id: mini
    required property var controller
    required property var playback
    required property var mainWindow
    objectName: "miniPlayer"
    width: 440; height: 100
    minimumWidth: 440; maximumWidth: 440
    minimumHeight: 100; maximumHeight: 100
    title: "Echo 迷你播放器"
    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    color: "transparent"
    Component.onCompleted: mini.controller.setMiniWindow(mini)
    onVisibleChanged: playback.setPresentationActive(visible || mini.mainWindow.visible)
    onClosing: event => { event.accepted = false; mini.hide(); }
    Rectangle {
        anchors.fill: parent; radius: 16; antialiasing: true; color: "#24202d"; border.color: "#51465f"
        MouseArea { anchors.fill: parent; onPressed: mini.startSystemMove(); onDoubleClicked: mini.controller.showMainWindow() }
        ColumnLayout {
            anchors.fill: parent
            anchors.leftMargin: 14
            anchors.rightMargin: 14
            anchors.topMargin: 10
            anchors.bottomMargin: 4
            spacing: 4
            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 60
                Layout.minimumHeight: 60
                Layout.maximumHeight: 60
                spacing: 0
                Cover {
                    objectName: "miniCover"
                    Layout.preferredWidth: 60
                    Layout.preferredHeight: 60
                    Layout.alignment: Qt.AlignTop
                    radius: 10
                    artwork: playback.track.artwork
                }
                ColumnLayout {
                    objectName: "miniMetadata"
                    Layout.leftMargin: 12
                    Layout.topMargin: 10
                    Layout.alignment: Qt.AlignTop
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.preferredWidth: 132
                    Layout.maximumWidth: 132
                    spacing: 7
                    Text { Layout.fillWidth: true; text: playback.track.title; color: "#f3edf9"; font.pixelSize: 13; elide: Text.ElideRight }
                    Text { Layout.fillWidth: true; text: playback.track.artist; color: "#aaa1b7"; font.pixelSize: 11; elide: Text.ElideRight }
                }
                RowLayout {
                    objectName: "miniTransport"
                    Layout.leftMargin: 2
                    Layout.topMargin: 12
                    Layout.alignment: Qt.AlignTop
                    Layout.preferredWidth: 182
                    Layout.minimumWidth: 182
                    Layout.maximumWidth: 182
                    Layout.preferredHeight: 38
                    spacing: 4
                    ActionButton { objectName: "miniFavorite"; Layout.alignment: Qt.AlignVCenter; implicitWidth: 32; iconSize: 18; symbol: playback.track.favorite ? "heartFilled" : "heart"; active: playback.track.favorite; label: playback.track.favorite ? "取消喜欢" : "喜欢"; enabled: playback.track.id > 0; onClicked: playback.toggleFavorite() }
                    ActionButton { objectName: "miniPrevious"; Layout.alignment: Qt.AlignVCenter; implicitWidth: 32; iconSize: 18; symbol: "previous"; label: "上一首"; onClicked: playback.previous() }
                    ActionButton { objectName: "miniToggle"; Layout.alignment: Qt.AlignVCenter; implicitWidth: 38; iconSize: 20; prominent: true; symbol: playback.playing ? "pause" : "play"; label: playback.playing ? "暂停" : "播放"; onClicked: playback.toggle() }
                    ActionButton { objectName: "miniNext"; Layout.alignment: Qt.AlignVCenter; implicitWidth: 32; iconSize: 18; symbol: "next"; label: "下一首"; onClicked: playback.next() }
                    ActionButton { objectName: "miniReturn"; Layout.alignment: Qt.AlignVCenter; implicitWidth: 32; iconSize: 17; symbol: "expand"; label: "返回主窗口"; onClicked: mini.controller.showMainWindow() }
                }
                Item { Layout.fillWidth: true; Layout.minimumWidth: 0 }
            }
            SeekSlider {
                id: miniSeek
                objectName: "miniSeek"
                Layout.fillWidth: true
                Layout.preferredHeight: 22
                Layout.minimumHeight: 22
                Layout.maximumHeight: 22
                from: 0; to: Math.max(1, playback.duration); enabled: playback.track.id > 0 && playback.duration > 0
                stepSize: 5000
                Binding { target: miniSeek; property: "value"; value: playback.position; when: !miniSeek.pressed }
                onPressedChanged: if (!pressed) playback.seek(Math.round(value))
                onMoved: if (!pressed) playback.seek(Math.round(value))
                Accessible.name: "播放进度"
            }
        }
        ActionButton { anchors.right: parent.right; anchors.rightMargin: 6; anchors.top: parent.top; anchors.topMargin: 3; implicitWidth: 24; iconSize: 14; symbol: "close"; label: "隐藏迷你播放器"; onClicked: mini.hide() }
    }
}
