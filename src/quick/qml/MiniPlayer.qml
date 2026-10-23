import QtQuick
import QtQuick.Controls
import QtQuick.Window
import QtQuick.Layouts
Window {
    id: mini
    objectName: "miniPlayer"
    width: 440; height: 100
    minimumWidth: 440; maximumWidth: 440
    minimumHeight: 100; maximumHeight: 100
    title: "Echo 迷你播放器"
    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    color: "transparent"
    Component.onCompleted: client.setMiniWindow(mini)
    onVisibleChanged: playback.setPresentationActive(visible || chrome.visible)
    onClosing: event => { event.accepted = false; mini.hide(); }
    Rectangle {
        anchors.fill: parent; radius: 16; antialiasing: true; color: "#24202d"; border.color: "#51465f"
        MouseArea { anchors.fill: parent; onPressed: mini.startSystemMove(); onDoubleClicked: client.showMainWindow() }
        Cover { x: 14; y: 10; width: 60; height: 60; radius: 10; artwork: playback.track.artwork }
        Column {
            x: 86; y: 20; width: 132; spacing: 7
            Text { width: parent.width; text: playback.track.title; color: "#f3edf9"; font.pixelSize: 13; elide: Text.ElideRight }
            Text { width: parent.width; text: playback.track.artist; color: "#aaa1b7"; font.pixelSize: 11; elide: Text.ElideRight }
        }
        RowLayout {
            objectName: "miniTransport"
            x: 220; y: 22; width: 182; height: 38; spacing: 4
            ActionButton { objectName: "miniFavorite"; Layout.alignment: Qt.AlignVCenter; implicitWidth: 32; iconSize: 18; symbol: playback.track.favorite ? "heartFilled" : "heart"; active: playback.track.favorite; label: playback.track.favorite ? "取消喜欢" : "喜欢"; enabled: playback.track.id > 0; onClicked: playback.toggleFavorite() }
            ActionButton { objectName: "miniPrevious"; Layout.alignment: Qt.AlignVCenter; implicitWidth: 32; iconSize: 18; symbol: "previous"; label: "上一首"; onClicked: playback.previous() }
            ActionButton { objectName: "miniToggle"; Layout.alignment: Qt.AlignVCenter; implicitWidth: 38; iconSize: 20; prominent: true; symbol: playback.playing ? "pause" : "play"; label: playback.playing ? "暂停" : "播放"; onClicked: playback.toggle() }
            ActionButton { objectName: "miniNext"; Layout.alignment: Qt.AlignVCenter; implicitWidth: 32; iconSize: 18; symbol: "next"; label: "下一首"; onClicked: playback.next() }
            ActionButton { objectName: "miniReturn"; Layout.alignment: Qt.AlignVCenter; implicitWidth: 32; iconSize: 17; symbol: "expand"; label: "返回主窗口"; onClicked: client.showMainWindow() }
        }
        ActionButton { x: 410; y: 3; implicitWidth: 24; iconSize: 14; symbol: "close"; label: "隐藏迷你播放器"; onClicked: mini.hide() }
        SeekSlider {
            id: miniSeek
            objectName: "miniSeek"
            x: 14; y: 74; width: parent.width - 28; height: 22
            from: 0; to: Math.max(1, playback.duration); enabled: playback.track.id > 0 && playback.duration > 0
            stepSize: 5000
            Binding { target: miniSeek; property: "value"; value: playback.position; when: !miniSeek.pressed }
            onPressedChanged: if (!pressed) playback.seek(Math.round(value))
            onMoved: if (!pressed) playback.seek(Math.round(value))
            Accessible.name: "播放进度"
        }
    }
}
