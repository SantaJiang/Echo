import QtQuick
import QtQuick.Effects
import "qrc:/echo/quick/theme"

Flickable {
    id: lyrics
    property var model: []
    property int activeLyricIndex: -1
    property bool reduceMotion: false
    property real typographyProgress: 0
    property bool manualScrolling: false
    readonly property int emphasisIndex: Math.max(0, activeLyricIndex)
    readonly property int count: rows.count
    readonly property real readingAnchor: 0.42
    readonly property real edgeFadeHeight: Math.min(200, height * 0.3)
    // Shape glyphs at a stable size, then scale continuously. pixelSize is
    // integral; binding it to the window makes every row jump at 20/21/22 px.
    readonly property real textScale: (20 + 2 * typographyProgress) / 22
    property real spacing: 18 + 2 * typographyProgress
    property Item currentLine: null
    signal seekRequested(real time)

    // Animate a position in the song, not a cached pixel offset. The same
    // position maps through the current row geometry on every resize frame.
    property real followedIndex: emphasisIndex
    Behavior on followedIndex {
        enabled: !lyrics.reduceMotion && !lyrics.manualScrolling
        NumberAnimation { duration: 420; easing.type: Easing.OutCubic }
    }
    property int rowRevision: 0
    property real resumeStartIndex: 0
    property real resumeProgress: 1
    readonly property real visibleIndex: resumeStartIndex + (followedIndex - resumeStartIndex) * resumeProgress
    // Automatic following owns its visual translation. Flickable owns manual
    // scrolling; its resize/bounds fixup rounds contentY even with pixelAligned
    // disabled, so it cannot be the rendering authority for the reading anchor.
    readonly property real displayOffset: manualScrolling ? contentY
        : positionForIndex(visibleIndex) - height * readingAnchor
    function positionForIndex(index) {
        // Track replacement of a model even when its row count stays the same.
        const revision = rowRevision;
        if (count === 0)
            return 0;
        const bounded = Math.max(0, Math.min(count - 1, index));
        const lower = Math.floor(bounded);
        const from = rows.itemAt(lower);
        const to = rows.itemAt(Math.min(count - 1, lower + 1));
        return from && to ? from.y + (to.y - from.y) * (bounded - lower) : 0;
    }
    function indexForPosition(position) {
        for (let i = 0; i < count - 1; ++i) {
            const from = rows.itemAt(i);
            const to = rows.itemAt(i + 1);
            if (from && to && position < to.y)
                return Math.max(0, i + (position - from.y) / Math.max(1, to.y - from.y));
        }
        return Math.max(0, count - 1);
    }
    function itemAtIndex(index: int): Item { return rows.itemAt(index); }
    function updateCurrentLine() { currentLine = rows.itemAt(emphasisIndex); }
    onEmphasisIndexChanged: updateCurrentLine()
    onRowRevisionChanged: updateCurrentLine()

    contentWidth: width
    contentHeight: lineColumn.height
    topMargin: height * readingAnchor
    bottomMargin: height * (1 - readingAnchor)
    flickableDirection: Flickable.VerticalFlick
    clip: true
    Binding {
        target: lyrics
        property: "contentY"
        when: !lyrics.manualScrolling && lyrics.count > 0
        value: lyrics.positionForIndex(lyrics.visibleIndex) - lyrics.height * lyrics.readingAnchor
        restoreMode: Binding.RestoreNone
    }
    onManualScrollingChanged: {
        resumeAnimation.stop();
        if (manualScrolling || reduceMotion || count === 0)
            resumeProgress = 1;
        else {
            resumeStartIndex = indexForPosition(contentY + height * readingAnchor);
            resumeProgress = 0;
            resumeAnimation.restart();
        }
    }
    NumberAnimation {
        id: resumeAnimation
        target: lyrics
        property: "resumeProgress"
        to: 1
        duration: 420
        easing.type: Easing.OutCubic
    }
    function pauseFollowing() {
        if (!manualScrolling) {
            const offset = displayOffset;
            manualScrolling = true;
            contentY = offset;
        }
        manualResume.restart();
    }
    onMovementStarted: pauseFollowing()
    onMovementEnded: if (manualScrolling) manualResume.restart()
    WheelHandler {
        target: null
        blocking: false
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        onWheel: event => {
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
    Column {
        id: lineColumn
        parent: lyrics
        y: -lyrics.displayOffset
        width: lyrics.width
        spacing: lyrics.spacing
        Repeater {
            id: rows
            model: lyrics.model
            onItemAdded: lyrics.rowRevision++
            onItemRemoved: lyrics.rowRevision++
            delegate: Item {
                id: row
                required property var modelData
                required property int index
                property alias text: line.text
                width: lineColumn.width
                height: line.height * lyrics.textScale
                opacity: index === lyrics.emphasisIndex ? 1 : Math.max(0.025, 0.52 * Math.pow(0.7, Math.abs(index - lyrics.emphasisIndex) - 1))
                Behavior on opacity { NumberAnimation { duration: Theme.motion(lyrics.reduceMotion, Theme.motionReveal) } }
                Text {
                    id: line
                    width: row.width / lyrics.textScale
                    scale: lyrics.textScale
                    transformOrigin: Item.TopLeft
                    text: row.modelData.text
                    // Keep exact row metrics for anchoring, but create glyph
                    // geometry only near the viewport, including a scroll buffer.
                    visible: row.y + row.height >= lyrics.displayOffset - lyrics.height
                        && row.y <= lyrics.displayOffset + 2 * lyrics.height
                    wrapMode: Text.Wrap
                    font.family: Qt.platform.os === "osx" ? "PingFang SC" : Qt.application.font.family
                    font.pixelSize: 22
                    renderType: Text.QtRendering
                    font.weight: row.index === lyrics.emphasisIndex ? Font.Medium : Font.Normal
                    lineHeightMode: Text.FixedHeight
                    lineHeight: 32
                    color: row.index === lyrics.emphasisIndex ? Theme.textPrimary : Theme.textSecondary
                }
                TapHandler {
                    enabled: modelData.time >= 0
                    onTapped: lyrics.seekRequested(modelData.time)
                }
            }
        }
    }
    Text {
        parent: lyrics
        anchors.centerIn: parent
        visible: lyrics.count === 0
        text: "让音乐自己诉说"
        color: Theme.textMuted
        font.pixelSize: 23
    }
    layer.enabled: true
    layer.effect: MultiEffect {
        maskEnabled: true
        maskSource: lyricFadeMask
        maskThresholdMin: 0.5
        maskSpreadAtMin: 1.0
        autoPaddingEnabled: false
    }
    Rectangle {
        id: lyricFadeMask
        parent: lyrics
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
