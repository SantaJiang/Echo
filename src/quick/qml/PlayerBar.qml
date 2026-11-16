import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "qrc:/echo/quick/theme"

Rectangle {
    id: bar
    required property var playback
    property bool reduceMotion: false
    property bool queueOpen: false
    // A parked pointer or retained keyboard focus is not ongoing activity.
    // Hold the controls only while a gesture or popup is actually in use.
    readonly property bool interacting: barSeek.pressed || volumePopup.opened || volumeSlider.pressed
        || barTransportButtons.busy || barShuffle.down || barRepeat.down
        || barVolume.down || queueToggle.down || barFavorite.down
    signal expansionRequested()
    signal queueRequested()
    function timeText(ms) {
        const total = Math.max(0, Math.floor(ms / 1000));
        return Math.floor(total / 60) + ":" + String(total % 60).padStart(2, "0");
    }
    objectName: "playerBar"
    height: 104
    color: Theme.c24202d
    Rectangle {
        width: parent.width
        height: 1
        color: Theme.c12ffffff
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
        followEnabled: true
        followedValue: playback.position
        smoothFollow: playback.playing
        reduceMotion: bar.reduceMotion
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
        objectName: "playerBarZones"
        anchors.fill: parent
        anchors.leftMargin: 24
        anchors.rightMargin: 24
        spacing: 0
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 1
            Layout.minimumWidth: 0
            RowLayout {
                objectName: "barTrackInfo"
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(340, bar.width * 0.31, parent.width)
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
                            color: Theme.cf3eef9
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }
                        FavoriteButton {
                            id: barFavorite
                            objectName: "barFavorite"
                            implicitWidth: 30
                            implicitHeight: 30
                            iconSize: 18
                            reduceMotion: bar.reduceMotion
                            favorite: playback.track.favorite
                            trackId: playback.track.id
                            onClicked: playback.toggleFavorite()
                        }
                        Item {
                            Layout.fillWidth: true
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: playback.track.artist
                        color: Theme.caaa1b7
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }
                }
            }
        }
        RowLayout {
            objectName: "barTransport"
            Layout.alignment: Qt.AlignVCenter
            Layout.preferredWidth: implicitWidth
            Layout.minimumWidth: implicitWidth
            Layout.maximumWidth: implicitWidth
            spacing: 10
            ActionButton {
                id: barShuffle
                objectName: "barShuffle"
                symbol: "shuffle"
                active: playback.shuffle
                label: "随机播放"
                onClicked: playback.toggleShuffle()
            }
            PlaybackTransport {
                id: barTransportButtons
                playback: bar.playback
                namePrefix: "bar"
                reduceMotion: bar.reduceMotion
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
                    color: Theme.ccbb5ff
                    font.pixelSize: 10
                }
            }
        }
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 1
            Layout.minimumWidth: 0
            RowLayout {
                objectName: "barActions"
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: 8
                Text {
                    text: bar.timeText(playback.position) + " / " + bar.timeText(playback.duration)
                    color: Theme.caaa1b7
                    font.pixelSize: 12
                    font.family: Qt.platform.os === "osx" ? "Menlo"
                                 : Qt.platform.os === "windows" ? "Consolas"
                                 : Qt.application.font.family
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
            color: Theme.c332b40
            radius: 12
            border.color: Theme.c20ffffff
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
