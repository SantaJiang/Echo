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
    // Follow the native window geometry directly. A second layout animation
    // would run after Cocoa's zoom animation and leave the content behind.
    readonly property real typographyProgress: Math.max(0, Math.min(1, (width - 1180) / 220))
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
    Item {
        id: content
        objectName: "detailContent"
        // Center the reading block itself; unused lyric-column space must
        // not pull the visible composition toward the left.
        readonly property real availableWidth: Math.min(1440, page.width - Math.max(104, page.width * 0.18))
        readonly property real coverWidth: Math.min(520, availableWidth * 0.43, height * 0.88)
        readonly property real lyricWidth: Math.min(640, availableWidth - coverWidth - spacing,
                                                   Math.max(280, naturalLyricWidth))
        readonly property real naturalLyricWidth: {
            // Re-measure when responsive typography changes.
            const measuringFont = lyricMetrics.font;
            let widest = 0;
            const lines = lyrics.model || [];
            for (let i = 0; i < lines.length; ++i)
                widest = Math.max(widest, lyricMetrics.advanceWidth(lines[i].text || ""));
            return Math.ceil(widest);
        }
        x: (page.width - width) / 2
        y: 28 + page.topInset
        height: Math.max(0, page.height - y - 32 - page.bottomInset)
        width: coverWidth + spacing + lyricWidth
        readonly property real spacing: Math.max(48, Math.min(168, availableWidth * 0.115))
        FontMetrics {
            id: lyricMetrics
            font.family: Qt.platform.os === "osx" ? "PingFang SC" : Qt.application.font.family
            font.pixelSize: 20 + 2 * page.typographyProgress
            font.weight: Font.Medium
        }
        Item {
            objectName: "detailPlayerColumn"
            width: content.coverWidth
            height: content.height
            Cover {
                id: largeCover
                objectName: "detailCover"
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width
                height: width
                artwork: page.playback.track.artwork
                radius: 10
                // Keep the mask's render target stable throughout resizing.
                renderTextureSize: Qt.size(Math.ceil(520 * Screen.devicePixelRatio), Math.ceil(520 * Screen.devicePixelRatio))
            }
        }
        Item {
            objectName: "detailLyricsColumn"
            x: content.coverWidth + content.spacing
            width: content.lyricWidth
            height: content.height
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
                        if (page.reduceMotion || instant)
                            contentY = targetY;
                        else {
                            followAnimation.to = targetY;
                            followAnimation.restart();
                        }
                    }
                    onActiveLyricIndexChanged: Qt.callLater(followCurrent)
                    onModelChanged: Qt.callLater(followCurrent)
                    function followAfterLayout() {
                        // Commit wrapping and row positions before reading line.y.
                        forceLayout();
                        followCurrent(true);
                    }
                    onHeightChanged: Qt.callLater(followAfterLayout)
                    onWidthChanged: Qt.callLater(followAfterLayout)
                    onSpacingChanged: Qt.callLater(followAfterLayout)
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
