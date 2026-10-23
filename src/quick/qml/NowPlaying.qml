import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects

Item {
    id: page
    required property var playback
    property bool expanded: true
    signal collapse
    property bool reduceMotion: false
    property bool transitioning: false
    property bool persistentBar: false
    property bool dynamicBackground: false
    property real topInset: 0
    readonly property real contentHeight: height - topInset
    property alias coverItem: largeCover
    Rectangle {
        anchors.fill: parent
        color: "#16141d"
    }
    Image {
        id: backgroundImage
        objectName: "playbackBackgroundImage"
        anchors.fill: parent
        source: page.dynamicBackground && page.playback.track.artwork
            ? page.playback.track.artwork : "image://echo-icons/cover/background"
        sourceSize: Qt.size(96, 96)
        asynchronous: true
        fillMode: Image.PreserveAspectCrop
        visible: false
    }
    MultiEffect {
        source: backgroundImage
        anchors.fill: parent
        blurEnabled: true
        blurMax: 64
        blur: 1
        saturation: -0.3
        brightness: -0.35
        opacity: 0.65
        visible: backgroundImage.status === Image.Ready
    }
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop {
                position: 0
                color: "#15121824"
            }
            GradientStop {
                position: 1
                color: "#ed111016"
            }
        }
    }
    ActionButton {
        x: 28
        y: 16 + page.topInset
        symbol: "collapse"
        label: "收起播放详情"
        onClicked: page.collapse()
    }
    RowLayout {
        anchors.fill: parent
        anchors.margins: 52
        anchors.topMargin: 64 + page.topInset
        anchors.bottomMargin: 32
        spacing: Math.max(48, page.width * 0.07)
        ColumnLayout {
            Layout.preferredWidth: Math.min(440, page.width * 0.43, page.contentHeight - (page.persistentBar ? 260 : 380))
            Layout.minimumWidth: 280
            Layout.maximumWidth: Math.min(440, page.width * 0.43, page.contentHeight - (page.persistentBar ? 260 : 380))
            Layout.fillHeight: true
            spacing: 16
            Item {
                Layout.fillHeight: true
                Layout.maximumHeight: 56
            }
            Cover {
                id: largeCover
                objectName: "detailCover"
                Layout.fillWidth: true
                Layout.preferredHeight: width
                artwork: page.playback.track.artwork
                radius: 16
            }
            RowLayout {
                Layout.fillWidth: true
                ColumnLayout {
                    Layout.fillWidth: true
                    Text {
                        Layout.fillWidth: true
                        text: page.playback.track.title
                        color: "#faf8ff"
                        font.pixelSize: 23
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        text: page.playback.track.artist
                        color: "#aaa5b8"
                        font.pixelSize: 15
                        elide: Text.ElideRight
                    }
                }
                ActionButton {
                    symbol: page.playback.track.favorite ? "heartFilled" : "heart"
                    active: page.playback.track.favorite
                    label: page.playback.track.favorite ? "取消喜欢" : "喜欢"
                    enabled: page.playback.track.id > 0
                    onClicked: page.playback.toggleFavorite()
                }
            }
            SeekSlider {
                id: seek
                visible: !page.persistentBar
                objectName: "detailSeek"
                Layout.fillWidth: true
                from: 0
                to: Math.max(1, page.playback.duration)
                property real interpolatedPosition: page.playback.position
                Behavior on interpolatedPosition {
                    NumberAnimation {
                        duration: page.playback.playing && !page.reduceMotion ? 100 : 0
                    }
                }
                Binding {
                    target: seek
                    property: "value"
                    value: seek.interpolatedPosition
                    when: !seek.pressed
                }
                enabled: page.playback.track.id > 0
                Accessible.name: "播放进度"
                onPressedChanged: if (!pressed)
                    page.playback.seek(Math.round(value))
            }
            RowLayout {
                visible: !page.persistentBar
                Layout.fillWidth: true
                Text {
                    text: timeText(page.playback.position)
                    color: "#aaa5b8"
                    font.pixelSize: 12
                }
                Item {
                    Layout.fillWidth: true
                }
                Text {
                    text: "−" + timeText(Math.max(0, page.playback.duration - page.playback.position))
                    color: "#aaa5b8"
                    font.pixelSize: 12
                }
            }
            RowLayout {
                visible: !page.persistentBar
                Layout.alignment: Qt.AlignHCenter
                spacing: 12
                ActionButton {
                    symbol: "shuffle"
                    active: page.playback.shuffle
                    label: "随机播放"
                    onClicked: page.playback.toggleShuffle()
                }
                ActionButton {
                    symbol: "previous"
                    label: "上一首"
                    onClicked: page.playback.previous()
                }
                ActionButton {
                    objectName: "detailToggle"
                    symbol: page.playback.playing ? "pause" : "play"
                    prominent: true
                    label: page.playback.playing ? "暂停" : "播放"
                    onClicked: page.playback.toggle()
                }
                ActionButton {
                    symbol: "next"
                    label: "下一首"
                    onClicked: page.playback.next()
                }
                ActionButton {
                    symbol: "repeat"
                    active: page.playback.repeat > 0
                    label: page.playback.repeat === 2 ? "单曲循环" : page.playback.repeat === 1 ? "列表循环" : "循环播放"
                    onClicked: page.playback.cycleRepeat()
                    Text {
                        anchors.right: parent.right
                        anchors.top: parent.top
                        text: "1"
                        visible: page.playback.repeat === 2
                        color: "#cbb5ff"
                        font.pixelSize: 10
                    }
                }
            }
            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: page.playback.format
                color: "#918a9e"
                font.pixelSize: 11
                elide: Text.ElideRight
            }
            Item {
                Layout.fillHeight: true
                Layout.maximumHeight: 56
            }
        }
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            ColumnLayout {
                anchors.fill: parent
                spacing: 20
                Text {
                    Layout.fillWidth: true
                    text: page.playback.track.album
                    color: "#aaa5b8"
                    font.pixelSize: 13
                    elide: Text.ElideRight
                }
                ListView {
                    id: lyrics
                    objectName: "lyricsView"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: page.playback.lyrics
                    property int activeLyricIndex: page.playback.lyricIndex
                    // Keep ListView's selection separate from the playback row:
                    // changing currentIndex can reposition even without a highlight.
                    currentIndex: -1
                    clip: true
                    spacing: 20
                    topMargin: height * 0.3
                    bottomMargin: height * 0.5
                    property bool manualScrolling: false
                    function followCurrent() {
                        if (manualScrolling || activeLyricIndex < 0 || count === 0)
                            return;
                        followAnimation.stop();
                        cancelFlick();
                        const previous = contentY;
                        positionViewAtIndex(activeLyricIndex, ListView.Beginning);
                        const line = itemAtIndex(activeLyricIndex);
                        const targetY = line ? Math.max(originY - topMargin,
                            Math.min(line.y - height * 0.3, originY + contentHeight - height + bottomMargin)) : contentY;
                        contentY = previous;
                        if (page.reduceMotion)
                            contentY = targetY;
                        else {
                            followAnimation.to = targetY;
                            followAnimation.restart();
                        }
                    }
                    onActiveLyricIndexChanged: Qt.callLater(followCurrent)
                    onModelChanged: Qt.callLater(followCurrent)
                    onHeightChanged: Qt.callLater(followCurrent)
                    onManualScrollingChanged: if (!manualScrolling) Qt.callLater(followCurrent)
                    NumberAnimation {
                        id: followAnimation
                        target: lyrics
                        property: "contentY"
                        duration: 420
                        easing.type: Easing.OutCubic
                    }
                    function pauseFollowing() {
                        followAnimation.stop();
                        manualScrolling = true;
                        manualResume.restart();
                    }
                    onMovementStarted: pauseFollowing()
                    onMovementEnded: if (manualScrolling) manualResume.restart()
                    WheelHandler {
                        target: null
                        blocking: false
                        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                        onWheel: event => {
                            // Suspend following before ListView handles the first
                            // trackpad delta, including updates during momentum.
                            lyrics.pauseFollowing();
                            event.accepted = false;
                        }
                    }
                    Timer {
                        id: manualResume
                        interval: 5000
                        onTriggered: {
                            if (lyrics.moving || lyrics.dragging || lyrics.flicking)
                                restart();
                            else
                                lyrics.manualScrolling = false;
                        }
                    }
                    delegate: Text {
                        required property var modelData
                        required property int index
                        width: lyrics.width
                        text: modelData.text
                        wrapMode: Text.Wrap
                        font.pixelSize: 25
                        font.weight: Font.DemiBold
                        color: index === lyrics.activeLyricIndex || lyrics.activeLyricIndex < 0 ? "#f6f2ff" : "#8b829b"
                        opacity: index === lyrics.activeLyricIndex || lyrics.activeLyricIndex < 0 ? 1 : Math.max(0.22, 0.6 - Math.abs(index - lyrics.activeLyricIndex) * 0.08)
                        Behavior on opacity {
                            NumberAnimation {
                                duration: page.reduceMotion ? 0 : 260
                            }
                        }
                        TapHandler {
                            enabled: modelData.time >= 0
                            onTapped: page.playback.seek(modelData.time)
                        }
                    }
                    Text {
                        anchors.centerIn: parent
                        visible: lyrics.count === 0
                        text: "让音乐自己诉说"
                        color: "#777081"
                        font.pixelSize: 23
                    }
                }
            }
        }
    }
    function timeText(ms) {
        let total = Math.max(0, Math.floor(ms / 1000));
        return Math.floor(total / 60) + ":" + String(total % 60).padStart(2, "0");
    }
}
