import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: bar
    required property var playback
    property bool reduceMotion: false
    property bool queueOpen: false
    // A parked pointer or retained keyboard focus is not ongoing activity.
    // Hold the controls only while a gesture or popup is actually in use.
    readonly property bool interacting: barSeek.pressed || volumePopup.opened || volumeSlider.pressed
        || barToggle.down || barShuffle.down || barPrevious.down || barNext.down || barRepeat.down
        || barVolume.down || queueToggle.down || barFavorite.down
    signal expansionRequested()
    signal queueRequested()
    function timeText(ms) {
        const total = Math.max(0, Math.floor(ms / 1000));
        return Math.floor(total / 60) + ":" + String(total % 60).padStart(2, "0");
    }
    objectName: "playerBar"
    height: 104
    color: "#24202d"
    Rectangle {
        width: parent.width
        height: 1
        color: "#12ffffff"
    }
    SeekSlider {
        id: barSeek
        objectName: "barSeek"
        z: 2
        anchors.top: parent.top
        anchors.topMargin: -12
        anchors.left: parent.left
        anchors.right: parent.right
        from: 0
        to: Math.max(1, playback.duration)
        enabled: playback.track.id > 0
        property real interpolatedPosition: playback.position
        Behavior on interpolatedPosition {
            NumberAnimation {
                duration: playback.playing && !bar.reduceMotion ? 100 : 0
            }
        }
        Binding {
            target: barSeek
            property: "value"
            value: barSeek.interpolatedPosition
            when: !barSeek.pressed
        }
        onPressedChanged: if (!pressed)
            playback.seek(Math.round(value))
        Accessible.name: "播放进度"
    }
    // Only unhandled presses reach this background; transport/seek controls keep their own input.
    MouseArea {
        anchors.fill: parent
        onClicked: {
            bar.expansionRequested();
        }
        cursorShape: Qt.PointingHandCursor
        Accessible.name: "展开或收起播放详情"
    }
    RowLayout {
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: 24
        width: Math.min(340, parent.width * 0.31)
        spacing: 14
        Cover {
            id: smallCover
            objectName: "playerCover"
            Layout.preferredWidth: 60
            Layout.preferredHeight: 60
            artwork: playback.track.artwork
            radius: 10
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6
            RowLayout {
                Layout.fillWidth: true
                spacing: 5
                Text {
                    Layout.maximumWidth: Math.max(0, smallCover.parent.width - 114)
                    Layout.preferredWidth: Math.min(implicitWidth, Math.max(0, smallCover.parent.width - 114))
                    text: playback.track.title
                    color: "#f3eef9"
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                ActionButton {
                    id: barFavorite
                    objectName: "barFavorite"
                    implicitWidth: 30
                    implicitHeight: 30
                    iconSize: 18
                    symbol: playback.track.favorite ? "heartFilled" : "heart"
                    active: playback.track.favorite
                    label: playback.track.favorite ? "取消喜欢" : "喜欢"
                    enabled: playback.track.id > 0
                    onClicked: playback.toggleFavorite()
                }
                Item {
                    Layout.fillWidth: true
                }
            }
            Text {
                Layout.fillWidth: true
                text: playback.track.artist
                color: "#aaa1b7"
                font.pixelSize: 12
                elide: Text.ElideRight
            }
        }
    }
    RowLayout {
        objectName: "barTransport"
        anchors.centerIn: parent
        spacing: 10
        ActionButton {
            id: barShuffle
            objectName: "barShuffle"
            symbol: "shuffle"
            active: playback.shuffle
            label: "随机播放"
            onClicked: playback.toggleShuffle()
        }
        ActionButton {
            id: barPrevious
            objectName: "barPrevious"
            symbol: "previous"
            label: "上一首"
            onClicked: playback.previous()
        }
        ActionButton {
            id: barToggle
            objectName: "barToggle"
            symbol: playback.playing ? "pause" : "play"
            prominent: true
            label: playback.playing ? "暂停" : "播放"
            onClicked: playback.toggle()
        }
        ActionButton {
            id: barNext
            objectName: "barNext"
            symbol: "next"
            label: "下一首"
            onClicked: playback.next()
        }
        ActionButton {
            id: barRepeat
            objectName: "barRepeat"
            symbol: "repeat"
            active: playback.repeat > 0
            label: playback.repeat === 2 ? "单曲循环" : playback.repeat === 1 ? "列表循环" : "循环播放"
            onClicked: playback.cycleRepeat()
            Text {
                anchors.right: parent.right
                anchors.top: parent.top
                text: "1"
                visible: playback.repeat === 2
                color: "#cbb5ff"
                font.pixelSize: 10
            }
        }
    }
    RowLayout {
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.rightMargin: 24
        spacing: 8
        Text {
            text: bar.timeText(playback.position) + " / " + bar.timeText(playback.duration)
            color: "#aaa1b7"
            font.pixelSize: 12
            font.family: "Menlo"
            Layout.rightMargin: 12
        }
        ActionButton {
            id: queueToggle
            objectName: "queueToggle"
            symbol: "queue"
            active: bar.queueOpen
            label: "播放队列"
            onClicked: {
                bar.queueRequested();
            }
        }
        ActionButton {
            id: barVolume
            objectName: "barVolume"
            symbol: "volume"
            label: "音量"
            onClicked: volumePopup.open()
        }
    }
    Popup {
        id: volumePopup
        objectName: "barVolumePopup"
        x: parent.width - width - 22
        y: -height - 12
        width: 190
        height: 54
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle {
            color: "#332b40"
            radius: 12
            border.color: "#20ffffff"
        }
        SeekSlider {
            id: volumeSlider
            anchors.fill: parent
            anchors.margins: 6
            from: 0
            to: 1
            value: playback.volume
            onMoved: playback.volume = value
            Accessible.name: "音量"
        }
    }
}
