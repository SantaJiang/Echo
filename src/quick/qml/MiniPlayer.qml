import QtQuick
import QtQuick.Controls
import QtQuick.Window
import QtQuick.Layouts
import "qrc:/echo/quick/theme"
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
        anchors.fill: parent; radius: 16; antialiasing: true; color: Theme.c24202d; border.color: Theme.c51465f
        Accessible.role: Accessible.Pane
        Accessible.name: mini.title
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
                    Text { Layout.fillWidth: true; text: playback.track.title; color: Theme.cf3edf9; font.pixelSize: 13; elide: Text.ElideRight }
                    Text { Layout.fillWidth: true; text: playback.track.artist; color: Theme.caaa1b7; font.pixelSize: 11; elide: Text.ElideRight }
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
                    FavoriteButton { objectName: "miniFavorite"; Layout.alignment: Qt.AlignVCenter; implicitWidth: 32; iconSize: 18; favorite: playback.track.favorite; trackId: playback.track.id; onClicked: playback.toggleFavorite() }
                    PlaybackTransport { Layout.alignment: Qt.AlignVCenter; playback: mini.playback; namePrefix: "mini"; spacing: 4; buttonSize: 32; toggleSize: 38; iconSize: 18 }
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
                followEnabled: true
                followedValue: playback.position
                onPressedChanged: if (!pressed) playback.seek(Math.round(value))
                onMoved: if (!pressed) playback.seek(Math.round(value))
                Accessible.name: "播放进度"
            }
        }
        ActionButton { anchors.right: parent.right; anchors.rightMargin: 6; anchors.top: parent.top; anchors.topMargin: 3; implicitWidth: 24; iconSize: 14; symbol: "close"; label: "隐藏迷你播放器"; onClicked: mini.hide() }
    }
}
