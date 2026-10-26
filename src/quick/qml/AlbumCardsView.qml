import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: cardViewport
    objectName: "cardViewport"
    required property var controller
    required property real fullContentWidth
    property real queueWidth: 300
    property bool queueOpen: false
    property bool initialized: false
    property alias view: cardGrid
    signal contextRequested(Item sender, point position, int row)
    signal queueDismissRequested()
    function updateQueueLayout() {
        if (!initialized) return;
        cardGrid.queueAnimationWidth = fullContentWidth;
        if (!controller.reduceMotion) cardLayoutTimer.restart();
        cardGrid.queueLayoutOpen = queueOpen;
    }
    onQueueOpenChanged: updateQueueLayout()
    Component.onCompleted: {
        cardGrid.queueAnimationWidth = fullContentWidth;
        cardGrid.queueLayoutOpen = queueOpen;
        initialized = true;
    }
    Timer { id: cardLayoutTimer; interval: 280 }
    Layout.fillWidth: true
    Layout.fillHeight: cardViewport.controller.tracks.count === 0
    Layout.preferredHeight: cardViewport.controller.tracks.count > 0 ? Math.min(280, cardGrid.cellHeight + 8) : 0
    visible: cardViewport.controller.cards.count > 0 && cardViewport.controller.page.kind !== "artists"
    clip: true
    GridView {
        id: cardGrid
        objectName: "cardGrid"
        property bool queueLayoutOpen: false
        property real queueAnimationWidth: -1
        readonly property bool animateQueueLayout: cardLayoutTimer.running && queueAnimationWidth === cardViewport.fullContentWidth && !cardViewport.controller.reduceMotion
        readonly property real fullWidth: Math.max(1, cardViewport.fullContentWidth)
        readonly property real queuedWidth: Math.max(1, fullWidth - cardViewport.queueWidth + 30)
        readonly property int minimumColumns: 2
        readonly property real targetCellWidth: 226
        // Keep tiles at a readable, stable size. A wider window adds columns,
        // rather than repeatedly growing and shrinking every cover at a breakpoint.
        function cellFor(available) { return Math.min(targetCellWidth, available / minimumColumns); }
        // Bound resources by the grid's largest possible cell. Window resizing
        // and column changes must not reload artwork or reallocate its mask.
        readonly property real largestCover: targetCellWidth - 18
        readonly property int artworkResolution: Math.min(2048, Math.pow(2, Math.ceil(Math.log2(Math.max(512, largestCover * Screen.devicePixelRatio * 2.04)))))
        readonly property int artworkTextureSide: Math.ceil(largestCover * Screen.devicePixelRatio * 2)
        // Resolve the destination once per toggle. The viewport still follows the panel live.
        width: queueLayoutOpen ? queuedWidth : fullWidth
        height: cardViewport.height
        enabled: !animateQueueLayout
        model: cardViewport.controller.cards
        cellWidth: cellFor(width)
        cellHeight: cellWidth - 18 + 58
        clip: false
        reuseItems: true
        cacheBuffer: 300
        TapHandler {
            enabled: cardViewport.queueOpen
            onTapped: if (cardGrid.indexAt(point.position.x + cardGrid.contentX,
                point.position.y + cardGrid.contentY) < 0) cardViewport.queueDismissRequested()
        }
        ScrollBar.vertical: ScrollBar {
            parent: cardViewport
            enabled: cardGrid.enabled
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.right: parent.right
        }
        onMovementEnded: if (cardViewport.controller.tracks.count === 0)
            cardViewport.controller.setScroll(contentY)
        delegate: Item {
            id: card
            required property var item
            required property int index
            width: cardGrid.cellWidth
            height: cardGrid.cellHeight
            property bool positioned: false
            Component.onCompleted: Qt.callLater(function() { card.positioned = true; })
            Column {
                id: cardVisual
                objectName: "cardVisual" + card.index
                // Keep viewport coordinates stable even if GridView adjusts its scroll origin.
                property real visualX: card.x - cardGrid.contentX
                property real visualY: card.y - cardGrid.contentY
                property real visualWidth: card.width
                x: visualX - card.x + cardGrid.contentX
                y: visualY - card.y + cardGrid.contentY
                width: visualWidth - 18
                spacing: 8
                Behavior on visualX {
                    enabled: card.positioned && cardGrid.animateQueueLayout
                    NumberAnimation { duration: 240; easing.type: Easing.OutCubic }
                }
                Behavior on visualY {
                    enabled: card.positioned && cardGrid.animateQueueLayout
                    NumberAnimation { duration: 240; easing.type: Easing.OutCubic }
                }
                Behavior on visualWidth {
                    enabled: card.positioned && cardGrid.animateQueueLayout
                    NumberAnimation { duration: 240; easing.type: Easing.OutCubic }
                }
                Item {
                    width: parent.width
                    height: width
                    Cover {
                        id: cardArt
                        objectName: "cardArtwork" + card.index
                        anchors.fill: parent
                        visible: card.item.kind !== "artist"
                        artwork: card.item.artwork || ""
                        decodeSize: cardGrid.artworkResolution
                        renderTextureSize: Qt.size(cardGrid.artworkTextureSide, cardGrid.artworkTextureSide)
                        radius: 10
                        zoomable: true
                        imageScale: cardHover.hovered && !cardViewport.controller.reduceMotion ? 1.02 : 1
                        Behavior on imageScale {
                            NumberAnimation {
                                duration: 180
                                easing.type: Easing.OutCubic
                            }
                        }
                    }
                    ArtistAvatar {
                        anchors.centerIn: parent
                        width: parent.width * 0.78
                        height: width
                        name: card.item.title
                        visible: card.item.kind === "artist"
                    }
                    ActionButton {
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.margins: 10
                        symbol: "play"
                        prominent: true
                        implicitWidth: 38
                        implicitHeight: 38
                        label: "播放"
                        opacity: cardHover.hovered ? 1 : 0
                        enabled: opacity > 0
                        visible: card.item.kind !== "source"
                        onClicked: cardViewport.controller.playCard(card.index)
                        Behavior on opacity {
                            NumberAnimation {
                                duration: 140
                            }
                        }
                    }
                }
                Text {
                    width: parent.width
                    text: card.item.title
                    color: "#e6dfef"
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                Text {
                    width: parent.width
                    text: card.item.subtitle || ""
                    color: "#82788f"
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }
            }
            HoverHandler {
                id: cardHover
            }
            TapHandler {
                acceptedButtons: Qt.LeftButton
                onTapped: cardViewport.controller.openCard(index)
            }
            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: eventPoint => cardViewport.contextRequested(card, eventPoint.position, index)
            }
            Accessible.role: Accessible.Button
            Accessible.name: item.title
            Accessible.onPressAction: cardViewport.controller.openCard(index)
        }
    }
}
