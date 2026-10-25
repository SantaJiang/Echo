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
    property real controlsReveal: 1
    property bool controlsShown: true
    property bool dynamicBackground: false
    property real bottomInset: 104
    property real topInset: 0
    readonly property real contentHeight: height - topInset - bottomInset
    property alias coverItem: largeCover
    // Cocoa can deliver a fullscreen resize in one step. Interpolate one
    // shared canvas so the cover, spacing and lyric viewport move together.
    property real layoutWidth: width
    property real layoutHeight: height
    readonly property bool layoutResizing: widthAnimation.running || heightAnimation.running
    readonly property real typographyProgress: Math.max(0, Math.min(1, (layoutWidth - 1180) / 220))
    onLayoutResizingChanged: if (!layoutResizing) Qt.callLater(() => lyrics.followCurrent(true))
    Behavior on layoutWidth {
        enabled: page.visible && page.expanded && !page.reduceMotion
        NumberAnimation {
            id: widthAnimation
            duration: 320
            easing.type: Easing.OutCubic
        }
    }
    Behavior on layoutHeight {
        enabled: page.visible && page.expanded && !page.reduceMotion
        NumberAnimation {
            id: heightAnimation
            duration: 320
            easing.type: Easing.OutCubic
        }
    }
    PlaybackBackground {
        anchors.fill: parent
        artwork: page.playback.track.artwork || ""
        dynamicBackground: page.dynamicBackground
    }
    ActionButton {
        objectName: "detailCollapse"
        opacity: page.controlsReveal
        visible: opacity > 0
        enabled: page.controlsShown
        x: 28
        y: 16 + page.topInset
        symbol: "collapse"
        label: "收起播放详情"
        onClicked: page.collapse()
    }
    RowLayout {
        id: content
        objectName: "detailContent"
        // Balance the visible artwork and left-aligned lyric text, rather
        // than the otherwise empty right-hand part of the lyric column.
        x: (page.layoutWidth - width) / 2 + Math.min(80, page.layoutWidth * 0.045)
        y: 28 + page.topInset
        height: Math.max(0, page.layoutHeight - y - 32 - page.bottomInset)
        width: Math.min(1440, page.layoutWidth - Math.max(104, page.layoutWidth * 0.18))
        spacing: Math.max(48, Math.min(168, width * 0.115))
        Item {
            objectName: "detailPlayerColumn"
            Layout.preferredWidth: Math.min(520, content.width * 0.43, content.height * 0.88)
            Layout.minimumWidth: 260
            Layout.maximumWidth: Layout.preferredWidth
            Layout.fillHeight: true
            Cover {
                id: largeCover
                objectName: "detailCover"
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width
                height: width
                artwork: page.playback.track.artwork
                radius: 10
                // Keep the mask's render target stable while the canvas
                // animates; reallocating it every frame stalls resizing.
                renderTextureSize: Qt.size(Math.ceil(520 * Screen.devicePixelRatio), Math.ceil(520 * Screen.devicePixelRatio))
            }
        }
        Item {
            objectName: "detailLyricsColumn"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.maximumHeight: content.height
            Layout.alignment: Qt.AlignTop
            ColumnLayout {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: Math.min(640, parent.width)
                anchors.topMargin: 32
                anchors.bottomMargin: 8
                spacing: 0
                ColumnLayout {
                    objectName: "detailSongInfo"
                    Layout.fillWidth: true
                    Layout.bottomMargin: 12
                    spacing: 8
                    Text {
                        objectName: "detailTitle"
                        Layout.fillWidth: true
                        text: page.playback.track.title
                        color: "#faf8ff"
                        font.pixelSize: 22 + 3 * page.typographyProgress
                        font.weight: Font.Medium
                        elide: Text.ElideRight
                    }
                    Text {
                        objectName: "detailMetadata"
                        Layout.fillWidth: true
                        text: page.playback.track.artist + "  ·  " + page.playback.track.album
                        color: "#a9a2b6"
                        font.pixelSize: 13
                        elide: Text.ElideRight
                    }
                }
                ListView {
                    id: lyrics
                    objectName: "lyricsView"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    readonly property real edgeFadeHeight: Math.min(200, height * 0.3)
                    readonly property real readingAnchor: 0.42
                    layer.enabled: true
                    layer.effect: MultiEffect {
                        maskEnabled: true
                        maskSource: lyricFadeMask
                        maskThresholdMin: 0.5
                        maskSpreadAtMin: 1.0
                        autoPaddingEnabled: false
                    }
                    model: page.playback.lyrics
                    property int activeLyricIndex: page.playback.lyricIndex
                    // Before the first timestamp, keep the first line as the
                    // reading focus instead of lighting up the entire lyric list.
                    readonly property int emphasisIndex: Math.max(0, activeLyricIndex)
                    // Keep ListView's selection separate from the playback row:
                    // changing currentIndex can reposition even without a highlight.
                    currentIndex: -1
                    clip: true
                    spacing: 18 + 2 * page.typographyProgress
                    topMargin: height * readingAnchor
                    bottomMargin: height * 0.5
                    property bool manualScrolling: false
                    function followCurrent(instant = false) {
                        if (manualScrolling || count === 0)
                            return;
                        followAnimation.stop();
                        cancelFlick();
                        const previous = contentY;
                        let line = itemAtIndex(emphasisIndex);
                        if (!line) {
                            positionViewAtIndex(emphasisIndex, ListView.Beginning);
                            line = itemAtIndex(emphasisIndex);
                        }
                        const targetY = line ? Math.max(originY - topMargin,
                            Math.min(line.y - height * readingAnchor, originY + contentHeight - height + bottomMargin)) : contentY;
                        contentY = previous;
                        if (page.reduceMotion || page.layoutResizing || instant)
                            contentY = targetY;
                        else {
                            followAnimation.to = targetY;
                            followAnimation.restart();
                        }
                    }
                    onActiveLyricIndexChanged: Qt.callLater(followCurrent)
                    onModelChanged: Qt.callLater(followCurrent)
                    onHeightChanged: Qt.callLater(followCurrent)
                    onWidthChanged: Qt.callLater(followCurrent)
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
                        font.family: Qt.platform.os === "osx" ? "PingFang SC" : Qt.application.font.family
                        font.pixelSize: 20 + 2 * page.typographyProgress
                        font.weight: index === lyrics.emphasisIndex ? Font.Medium : Font.Normal
                        lineHeightMode: Text.FixedHeight
                        lineHeight: Math.round(font.pixelSize * 1.45)
                        color: index === lyrics.emphasisIndex ? "#faf8ff" : "#ddd6e8"
                        opacity: index === lyrics.emphasisIndex ? 1 : Math.max(0.025, 0.52 * Math.pow(0.7, Math.abs(index - lyrics.emphasisIndex) - 1))
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
            Rectangle {
                id: lyricFadeMask
                width: lyrics.width
                height: lyrics.height
                visible: false
                layer.enabled: true
                gradient: Gradient {
                    GradientStop { position: 0; color: "transparent" }
                    GradientStop { position: lyrics.edgeFadeHeight / Math.max(1, lyrics.height); color: "white" }
                    GradientStop { position: 1 - lyrics.edgeFadeHeight / Math.max(1, lyrics.height); color: "white" }
                    GradientStop { position: 1; color: "transparent" }
                }
            }
        }
    }
}
