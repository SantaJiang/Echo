import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "qrc:/echo/quick/theme"

Item {
    id: page
    required property var playback
    signal collapse
    property bool reduceMotion: false
    property real controlsReveal: 1
    property bool controlsShown: true
    property bool dynamicBackground: false
    property real bottomInset: 104
    property real topInset: 0
    readonly property real contentHeight: height - topInset - bottomInset
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
        // Equal columns keep both the center gap and outer margins symmetric,
        // independent of lyric length and responsive line wrapping.
        readonly property real availableWidth: Math.max(0, Math.min(1440, page.width - Math.max(104, page.width * 0.18)))
        readonly property real spacing: Math.min(availableWidth, Math.max(48, Math.min(168, availableWidth * 0.115)))
        readonly property real columnWidth: Math.max(0, Math.min(520, (availableWidth - spacing) / 2, height * 0.88))
        readonly property real coverWidth: columnWidth
        readonly property real lyricWidth: columnWidth
        x: (page.width - width) / 2
        y: 28 + page.topInset
        height: Math.max(0, page.height - y - 32 - page.bottomInset)
        width: 2 * columnWidth + spacing
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
                // Bound decoding by the largest cover, not each resize frame.
                // Changing sourceSize reloads the asynchronous Image and briefly
                // selects the fallback while Cocoa is busy resizing the window.
                decodeSize: 2048
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
                    Item {
                        Layout.fillWidth: true
                        // Reserve a stable title slot so typography scaling
                        // does not independently move the entire lyric viewport.
                        implicitHeight: titleText.implicitHeight
                        Text {
                            id: titleText
                            objectName: "detailTitle"
                            width: parent.width / scale
                            scale: (22 + 3 * page.typographyProgress) / 25
                            transformOrigin: Item.TopLeft
                            text: page.playback.track.title
                            color: Theme.cfaf8ff
                            font.pixelSize: 25
                            renderType: Text.QtRendering
                            font.weight: Font.Medium
                            elide: Text.ElideRight
                        }
                    }
                    Text {
                        objectName: "detailMetadata"
                        Layout.fillWidth: true
                        text: page.playback.track.artist + "  ·  " + page.playback.track.album
                        color: Theme.ca9a2b6
                        font.pixelSize: 13
                        elide: Text.ElideRight
                    }
                }
                LyricsView {
                    id: lyrics
                    objectName: "lyricsView"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: page.playback.lyrics
                    activeLyricIndex: page.playback.lyricIndex
                    reduceMotion: page.reduceMotion
                    typographyProgress: page.typographyProgress
                    onSeekRequested: time => page.playback.seek(time)
                }
            }
        }
    }
}
