import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

FocusScope {
    id: root
    objectName: "quickShell"
    property bool expanded: false
    property bool queueOpen: false
    property var contextMenuPoint: null
    readonly property bool libraryQueueOpen: queueOpen && !expanded
    onQueueOpenChanged: showControls()
    onLibraryQueueOpenChanged: {
        cardGrid.queueAnimationWidth = root.width;
        if (!client.reduceMotion) cardLayoutTimer.restart();
        // Prepare animation before changing any grid geometry, regardless of binding evaluation order.
        cardGrid.queueLayoutOpen = root.libraryQueueOpen;
    }
    Timer {
        id: cardLayoutTimer
        interval: 280
    }
    function contextAt(item, position, action) {
        contextMenuPoint = item.mapToItem(root, position.x, position.y);
        action();
        contextMenuPoint = null;
    }
    property real reveal: expanded ? 1 : 0
    readonly property bool transitioning: reveal > 0 && reveal < 1
    property bool controlsIdle: false
    readonly property bool isPlaying: playback.playing
    readonly property bool controlsShown: !expanded || transitioning || !controlsIdle || queueOpen || playbackQueuePanel.openness > 0 || playerBar.interacting || appDialogs.active || appMenu.opened
    readonly property bool canAutoHideControls: expanded && !transitioning && !queueOpen && playbackQueuePanel.openness === 0 && !playerBar.interacting && !appDialogs.active && !appMenu.opened
    property real controlsReveal: controlsShown ? 1 : 0
    onCanAutoHideControlsChanged: restartControlsTimer()
    function closeQueue() {
        queueOpen = false;
        showControls();
        root.forceActiveFocus();
    }
    function showControls() {
        controlsIdle = false;
        restartControlsTimer();
    }
    function restartControlsTimer() {
        controlsHideTimer.stop();
        if (canAutoHideControls)
            controlsHideTimer.restart();
    }
    onExpandedChanged: showControls()
    onIsPlayingChanged: showControls()
    onTransitioningChanged: restartControlsTimer()
    Timer {
        id: controlsHideTimer
        objectName: "controlsHideTimer"
        interval: 3000
        onTriggered: if (root.canAutoHideControls) root.controlsIdle = true
    }
    HoverHandler {
        enabled: root.expanded
        property point lastPosition: Qt.point(-1, -1)
        onHoveredChanged: if (hovered) root.showControls()
        onPointChanged: {
            // Qt can replay hover events while the scene animates. Only real
            // pointer travel should reset the listening timeout.
            const position = point.position;
            if (position.x !== lastPosition.x || position.y !== lastPosition.y) {
                lastPosition = Qt.point(position.x, position.y);
                if (hovered) root.showControls();
            }
        }
    }
    TapHandler {
        enabled: root.expanded
        onTapped: root.showControls()
    }
    Behavior on controlsReveal {
        NumberAnimation {
            duration: client.reduceMotion ? 0 : 260
            easing.type: Easing.OutCubic
        }
    }
    readonly property real sideWidth: 218
    readonly property color accent: "#c9aaff"
    readonly property real trackWidth: Math.max(trackList.width, client.columns.reduce((a, b) => a + Number(b), 0))
    clip: true
    focus: true
    Behavior on reveal {
        NumberAnimation {
            duration: client.reduceMotion ? 0 : 360
            easing.type: Easing.OutCubic
        }
    }
    Keys.onSpacePressed: event => {
        playback.toggle();
        root.showControls();
        event.accepted = true;
    }
    Keys.onEscapePressed: event => {
        if (root.queueOpen)
            root.closeQueue();
        else if (root.expanded)
            root.expanded = false;
        else
            client.clearSelection();
        event.accepted = true;
    }
    Shortcut {
        sequences: [StandardKey.Undo]
        enabled: !appDialogs.active && !searchBox.activeFocus
        onActivated: client.action("undo")
    }
    Shortcut {
        sequences: [StandardKey.Redo]
        enabled: !appDialogs.active && !searchBox.activeFocus
        onActivated: client.action("redo")
    }
    Shortcut {
        sequences: [StandardKey.SelectAll]
        enabled: !appDialogs.active && !searchBox.activeFocus && !root.expanded
        onActivated: client.selectAll()
    }
    Rectangle {
        anchors.fill: parent
        color: "#17151d"
    }
    Item {
        id: libraryShell
        anchors.fill: parent
        visible: root.reveal < 1
        enabled: !root.expanded
        MouseArea {
            // Behind the library controls: only unhandled background clicks
            // dismiss the queue, without intercepting normal button actions.
            objectName: "queueDismissBackground"
            y: windowChrome.height
            width: queuePanel.x
            height: Math.max(0, playerBar.y - y)
            enabled: root.queueOpen
            onClicked: root.queueOpen = false
        }
        Rectangle {
            id: sidebar
            width: root.sideWidth
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.bottomMargin: playerBar.height
            color: "#201d28"
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 18
                anchors.topMargin: 50
                spacing: 8
                Text {
                    text: "资料库"
                    color: "#847e90"
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                    Layout.leftMargin: 12
                    Layout.bottomMargin: 8
                }
                ListView {
                    id: navigation
                    objectName: "navigationView"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: client.navigation
                    clip: true
                    spacing: 4
                    TapHandler {
                        enabled: root.queueOpen
                        onTapped: if (navigation.indexAt(point.position.x + navigation.contentX,
                            point.position.y + navigation.contentY) < 0) root.queueOpen = false
                    }
                    ScrollBar.vertical: ScrollBar {
                        policy: ScrollBar.AsNeeded
                    }
                    delegate: Rectangle {
                        id: navRow
                        required property var item
                        required property int index
                        readonly property bool selected: client.page.kind === item.kind && (item.kind !== "playlist" || client.page.key === item.key)
                        width: navigation.width
                        height: item.kind === "playlistSection" ? 52 : 38
                        radius: 9
                        color: item.kind === "playlistSection" ? "transparent" : selected ? "#42354f" : navHover.hovered ? "#12ffffff" : "transparent"
                        Accessible.role: Accessible.Button
                        Accessible.name: item.title
                        Accessible.onPressAction: if (item.kind !== "listFolder" && item.kind !== "playlistSection")
                            client.navigate(item.kind, item.key, item.title)
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12 + item.depth * 12
                            anchors.rightMargin: 8
                            spacing: 11
                            Image {
                                visible: item.kind !== "playlistSection"
                                source: "image://echo-icons/" + item.icon + "/" + (navRow.selected ? "accent" : "light")
                                sourceSize: Qt.size(16, 16)
                                Layout.preferredWidth: 16
                                Layout.preferredHeight: 16
                                opacity: navRow.selected ? 1 : 0.65
                            }
                            Text {
                                Layout.fillWidth: true
                                text: item.title
                                font.pixelSize: item.kind === "playlistSection" ? 11 : 13
                                font.weight: navRow.selected ? Font.DemiBold : Font.Normal
                                color: item.kind === "playlistSection" ? "#847e90" : navRow.selected ? "#eee3ff" : "#bbb4c8"
                                elide: Text.ElideRight
                            }
                        }
                        HoverHandler {
                            id: navHover
                        }
                        TapHandler {
                            acceptedButtons: Qt.LeftButton
                            onTapped: if (item.kind !== "listFolder" && item.kind !== "playlistSection")
                                client.navigate(item.kind, item.kind === "playlist" ? item.key : "", item.title)
                        }
                        TapHandler {
                            acceptedButtons: Qt.RightButton
                            onTapped: eventPoint => root.contextAt(navRow, eventPoint.position, function() { client.navigationMenu(index); })
                        }
                        ActionButton {
                            objectName: "newPlaylistButton"
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            visible: item.kind === "playlistSection"
                            implicitWidth: 32
                            implicitHeight: 32
                            iconSize: 16
                            symbol: "plus"
                            label: "新建歌单"
                            onClicked: client.action("newPlaylist")
                        }
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: client.status
                    visible: text.length > 0
                    wrapMode: Text.Wrap
                    color: "#a49bad"
                    font.pixelSize: 11
                    maximumLineCount: 3
                    elide: Text.ElideRight
                }
                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: "#12ffffff"
                    Layout.topMargin: 8
                    Layout.bottomMargin: 6
                }
                Rectangle {
                    objectName: "settingsNavigation"
                    Layout.fillWidth: true
                    implicitHeight: 38
                    radius: 9
                    color: settingsHover.hovered ? "#12ffffff" : "transparent"
                    Accessible.role: Accessible.Button
                    Accessible.name: "设置"
                    Accessible.onPressAction: client.showSettings()
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        spacing: 11
                        Image {
                            source: "image://echo-icons/settings/light"
                            sourceSize: Qt.size(16, 16)
                            Layout.preferredWidth: 16
                            Layout.preferredHeight: 16
                            opacity: 0.65
                        }
                        Text {
                            Layout.fillWidth: true
                            text: "设置"
                            color: "#bbb4c8"
                            font.pixelSize: 13
                        }
                    }
                    HoverHandler {
                        id: settingsHover
                    }
                    TapHandler {
                        onTapped: client.showSettings()
                    }
                }
            }
        }
        ColumnLayout {
            objectName: "libraryContent"
            anchors.left: sidebar.right
            anchors.right: queuePanel.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.bottomMargin: playerBar.height
            anchors.leftMargin: 30
            anchors.rightMargin: 30 * (1 - queuePanel.openness)
            anchors.topMargin: 28
            spacing: 16
            RowLayout {
                Layout.fillWidth: true
                ActionButton {
                    objectName: "backButton"
                    symbol: "back"
                    filled: true
                    implicitWidth: 32
                    implicitHeight: 32
                    iconSize: 18
                    Layout.preferredWidth: 32
                    Layout.preferredHeight: 32
                    Layout.alignment: Qt.AlignVCenter
                    enabled: client.page.canBack
                    label: "返回"
                    onClicked: client.back()
                }
                TextField {
                    id: searchBox
                    objectName: "searchField"
                    Layout.preferredWidth: Math.min(330, root.width * 0.3)
                    Layout.preferredHeight: 32
                    Layout.alignment: Qt.AlignVCenter
                    placeholderText: "搜索歌曲、专辑、艺术家"
                    color: "#f3edf9"
                    placeholderTextColor: "#81798e"
                    font.pixelSize: 12
                    leftPadding: 14
                    rightPadding: 32
                    background: Rectangle {
                        color: "#26222f"
                        radius: 9
                        border.color: searchBox.activeFocus ? "#826499" : "#10ffffff"
                    }
                    onTextEdited: client.search(text)
                    Keys.onEscapePressed: {
                        text = "";
                        client.search("");
                        root.forceActiveFocus();
                    }
                    ActionButton {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        implicitWidth: 30
                        implicitHeight: 30
                        symbol: "collapse"
                        label: "清除搜索"
                        visible: searchBox.text.length > 0
                        rotation: -90
                        onClicked: {
                            searchBox.text = "";
                            client.search("");
                        }
                    }
                }
                Item {
                    Layout.fillWidth: true
                }
            }
            ColumnLayout {
                id: pageContent
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 16
                objectName: "pageContent"
                property bool switching: false
                enabled: !switching
                opacity: 1
                Behavior on opacity {
                    NumberAnimation {
                        duration: client.reduceMotion ? 0 : (pageContent.switching ? 100 : 170)
                        easing.type: Easing.OutCubic
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 16
                    Layout.bottomMargin: 4
                    spacing: headerArtwork.visible || headerArtist.visible ? 18 : 5
                    Cover {
                        id: headerArtwork
                        Layout.preferredWidth: 96
                        Layout.preferredHeight: 96
                        Layout.alignment: Qt.AlignVCenter
                        radius: 12
                        artwork: client.page.artwork
                        visible: ["album", "playlist"].includes(client.page.kind) && client.tracks.count > 0
                    }
                    ArtistAvatar {
                        id: headerArtist
                        Layout.preferredWidth: 80
                        Layout.preferredHeight: 80
                        name: client.page.title
                        visible: client.page.kind === "artist" && client.tracks.count > 0
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                        spacing: headerArtwork.visible || headerArtist.visible ? 10 : 7
                        Text {
                            Layout.fillWidth: true
                            text: client.page.title
                            color: "#f4f0f8"
                            font.pixelSize: headerArtwork.visible || headerArtist.visible ? 28 : 30
                            font.weight: headerArtwork.visible || headerArtist.visible ? Font.DemiBold : Font.Bold
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            text: client.page.busy ? "正在读取资料库…" : client.page.subtitle
                            color: "#8d849b"
                            font.pixelSize: headerArtwork.visible || headerArtist.visible ? 13 : 12
                            elide: Text.ElideMiddle
                        }
                    }
                    PillButton {
                        text: "···"
                        visible: client.page.kind === "source" || client.page.kind === "playlist"
                        flat: true
                        Accessible.name: "当前页面菜单"
                        onClicked: client.action("pageMenu")
                    }
                    PillButton {
                        primary: true
                        text: "播放全部"
                        enabled: !client.page.busy && client.tracks.count > 0
                        visible: client.tracks.count > 0
                        onClicked: client.playAll(false)
                    }
                    ActionButton {
                        symbol: "shuffle"
                        label: "随机播放全部"
                        visible: client.tracks.count > 0
                        enabled: !client.page.busy
                        onClicked: client.playAll(true)
                    }
                    ActionButton {
                        symbol: "plus"
                        label: "添加音乐文件夹"
                        visible: client.page.kind === "sources" || client.page.kind === "songs"
                        onClicked: client.action("addSource")
                    }
                }
                ListView {
                    id: artistList
                    objectName: "artistList"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: client.page.kind === "artists" && client.cards.count > 0
                    model: visible ? client.cards : null
                    clip: true
                    reuseItems: true
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {}
                    onMovementEnded: client.setScroll(contentY)
                    delegate: ItemDelegate {
                        id: artistRow
                        required property var item
                        required property int index
                        objectName: "artistRow" + index
                        width: artistList.width
                        height: 96
                        padding: 0
                        hoverEnabled: true
                        Accessible.name: item.title + "，" + item.subtitle
                        onClicked: client.openCard(index)
                        background: Rectangle {
                            radius: 8
                            color: artistRow.down ? "#18ffffff" : artistRow.hovered || artistRow.visualFocus ? "#0bffffff" : "transparent"
                        }
                        contentItem: Item {
                            ArtistAvatar {
                                anchors.left: parent.left
                                anchors.leftMargin: 8
                                anchors.verticalCenter: parent.verticalCenter
                                width: 64
                                height: 64
                                name: artistRow.item.title
                            }
                            Column {
                                anchors.left: parent.left
                                anchors.leftMargin: 94
                                anchors.right: chevron.left
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 7
                                Text {
                                    width: parent.width
                                    text: artistRow.item.title
                                    color: "#e6dfef"
                                    font.pixelSize: 16
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }
                                Text {
                                    width: parent.width
                                    text: artistRow.item.subtitle
                                    color: "#8d849b"
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                }
                            }
                            Text {
                                id: chevron
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                text: "›"
                                color: "#8d849b"
                                font.pixelSize: 28
                            }
                            Rectangle {
                                anchors.left: parent.left
                                anchors.leftMargin: 94
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.bottom: parent.bottom
                                height: 1
                                color: "#12ffffff"
                                visible: artistRow.index < artistList.count - 1
                            }
                        }
                        TapHandler {
                            acceptedButtons: Qt.RightButton
                            onTapped: eventPoint => root.contextAt(artistRow, eventPoint.position, function() { client.cardMenu(artistRow.index); })
                        }
                    }
                }
                Item {
                    id: cardViewport
                    Layout.fillWidth: true
                    Layout.fillHeight: client.tracks.count === 0
                    Layout.preferredHeight: client.tracks.count > 0 ? Math.min(280, cardGrid.cellHeight + 8) : 0
                    visible: client.cards.count > 0 && client.page.kind !== "artists"
                    clip: true
                    GridView {
                        id: cardGrid
                        objectName: "cardGrid"
                        property bool queueLayoutOpen: false
                        property real queueAnimationWidth: -1
                        readonly property bool animateQueueLayout: cardLayoutTimer.running && queueAnimationWidth === root.width && !client.reduceMotion
                        readonly property real fullWidth: Math.max(1, root.width - root.sideWidth - 60)
                        readonly property real queuedWidth: Math.max(1, fullWidth - queuePanel.width + 30)
                        function cellFor(available) { return available / Math.max(2, Math.floor(available / 188)); }
                        // Share decode and render sizes across both endpoints to avoid texture churn.
                        readonly property real largestCover: Math.max(cellFor(fullWidth), cellFor(queuedWidth)) - 18
                        readonly property int artworkResolution: Math.min(2048, Math.pow(2, Math.ceil(Math.log2(Math.max(128, largestCover * Screen.devicePixelRatio * 2)))))
                        readonly property int artworkTextureSide: Math.ceil(largestCover * Screen.devicePixelRatio * 2)
                        // Resolve the destination once per toggle. The viewport still follows the panel live.
                        width: queueLayoutOpen ? queuedWidth : fullWidth
                        height: cardViewport.height
                        enabled: !animateQueueLayout
                        model: client.cards
                        cellWidth: cellFor(width)
                        cellHeight: cellWidth - 18 + 58
                        clip: false
                        reuseItems: true
                        cacheBuffer: 300
                        TapHandler {
                            enabled: root.queueOpen
                            onTapped: if (cardGrid.indexAt(point.position.x + cardGrid.contentX,
                                point.position.y + cardGrid.contentY) < 0) root.queueOpen = false
                        }
                        ScrollBar.vertical: ScrollBar {
                            parent: cardViewport
                            enabled: cardGrid.enabled
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            anchors.right: parent.right
                        }
                        onMovementEnded: if (client.tracks.count === 0)
                            client.setScroll(contentY)
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
                                        imageScale: cardHover.hovered && !client.reduceMotion ? 1.02 : 1
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
                                        onClicked: client.playCard(card.index)
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
                                onTapped: client.openCard(index)
                            }
                            TapHandler {
                                acceptedButtons: Qt.RightButton
                                onTapped: eventPoint => root.contextAt(card, eventPoint.position, function() { client.cardMenu(index); })
                            }
                            Accessible.role: Accessible.Button
                            Accessible.name: item.title
                            Accessible.onPressAction: client.openCard(index)
                        }
                    }
                }
                Item {
                    id: tableArea
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: client.tracks.count > 0
                    clip: true
                    Item {
                        id: header
                        width: root.trackWidth
                        height: 32
                        x: -trackList.contentX
                        Row {
                            Repeater {
                                model: ["", "歌曲", "艺术家", "专辑", "时长"]
                                delegate: Item {
                                    id: heading
                                    required property string modelData
                                    required property int index
                                    width: Number(client.columns[index])
                                    height: 32
                                    Text {
                                        anchors.left: parent.left
                                        anchors.leftMargin: 9
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: heading.modelData + (client.page.sort === heading.index ? client.page.descending ? " ↓" : " ↑" : "")
                                        color: "#7f748e"
                                        font.pixelSize: 11
                                    }
                                    TapHandler {
                                        onTapped: client.sortBy(index)
                                    }
                                    MouseArea {
                                        width: 7
                                        height: parent.height
                                        anchors.right: parent.right
                                        cursorShape: Qt.SplitHCursor
                                        property real startX
                                        property real startWidth
                                        onPressed: mouse => {
                                            startX = mapToItem(header, mouse.x, 0).x;
                                            startWidth = heading.width;
                                        }
                                        onPositionChanged: mouse => {
                                            if (pressed)
                                                client.resizeColumn(heading.index, startWidth + mapToItem(header, mouse.x, 0).x - startX);
                                        }
                                    }
                                }
                            }
                        }
                        Rectangle {
                            anchors.bottom: parent.bottom
                            width: parent.width
                            height: 1
                            color: "#17ffffff"
                        }
                    }
                    ListView {
                        id: trackList
                        objectName: "trackList"
                        anchors.fill: parent
                        anchors.topMargin: 34
                        anchors.bottomMargin: 8
                        model: client.tracks
                        contentWidth: root.trackWidth
                        flickableDirection: Flickable.AutoFlickIfNeeded
                        boundsBehavior: Flickable.StopAtBounds
                        clip: true
                        reuseItems: true
                        cacheBuffer: 160
                        keyNavigationEnabled: false
                        WheelHandler {
                            // Physical wheel notches should move a predictable
                            // three rows. Touchpad pixel scrolling stays native.
                            acceptedDevices: PointerDevice.Mouse
                            target: null
                            onWheel: event => {
                                const distance = event.pixelDelta.y || event.angleDelta.y / 120 * 48 * 3;
                                if (!distance)
                                    return;
                                trackList.cancelFlick();
                                if (event.modifiers & Qt.ShiftModifier)
                                    trackList.contentX = Math.max(0, Math.min(trackList.contentWidth - trackList.width,
                                        trackList.contentX - distance));
                                else {
                                    trackList.contentY = Math.max(0, Math.min(trackList.contentHeight - trackList.height,
                                        trackList.contentY - distance));
                                    client.setScroll(trackList.contentY);
                                }
                            }
                        }
                        TapHandler {
                            enabled: root.queueOpen
                            onTapped: if (trackList.indexAt(point.position.x + trackList.contentX,
                                point.position.y + trackList.contentY) < 0) root.queueOpen = false
                        }
                        ScrollBar.vertical: ScrollBar {}
                        ScrollBar.horizontal: ScrollBar {}
                        onMovementEnded: client.setScroll(contentY)
                        Keys.onPressed: event => {
                            if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                                client.playRow(currentIndex);
                                event.accepted = true;
                            } else if (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace) {
                                client.removeSelected();
                                event.accepted = true;
                            } else if (event.key === Qt.Key_Up || event.key === Qt.Key_Down) {
                                let next = Math.max(0, Math.min(count - 1, currentIndex + (event.key === Qt.Key_Up ? -1 : 1)));
                                if (event.modifiers & Qt.AltModifier)
                                    client.moveTrack(currentIndex, next);
                                else {
                                    currentIndex = next;
                                    client.selectRow(next, event.modifiers);
                                    positionViewAtIndex(next, ListView.Contain);
                                }
                                event.accepted = true;
                            }
                        }
                        delegate: Rectangle {
                            id: trackRow
                            required property var item
                            required property int index
                            readonly property bool selected: {
                                let revision = client.selectionRevision;
                                return client.isSelected(index);
                            }
                            width: root.trackWidth
                            height: 48
                            radius: 7
                            color: selected ? "#42344e" : rowHover.hovered ? "#16ffffff" : index % 2 ? "#06ffffff" : "transparent"
                            opacity: item.missing ? 0.45 : 1
                            Accessible.role: Accessible.ListItem
                            Accessible.name: item.title + "，" + item.artist
                            Accessible.onPressAction: client.playRow(index)
                            Row {
                                anchors.verticalCenter: parent.verticalCenter
                                Item {
                                    width: Number(client.columns[0])
                                    height: 48
                                    ActionButton {
                                        anchors.centerIn: parent
                                        symbol: trackRow.item.favorite ? "heartFilled" : "heart"
                                        active: trackRow.item.favorite
                                        opacity: trackRow.item.favorite || rowHover.hovered ? 1 : 0
                                        label: trackRow.item.favorite ? "取消喜欢" : "喜欢"
                                        onClicked: client.favoriteRow(trackRow.index)
                                    }
                                }
                                RowLayout {
                                    width: Number(client.columns[1])
                                    height: 48
                                    spacing: 10
                                    Cover {
                                        Layout.preferredWidth: 32
                                        Layout.preferredHeight: 32
                                        radius: 5
                                        artwork: trackRow.item.artwork
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: trackRow.item.title
                                        color: trackRow.item.playing ? "#cfb3f7" : "#e4ddeb"
                                        font.pixelSize: 12
                                        elide: Text.ElideRight
                                    }
                                    Item {
                                        Layout.preferredWidth: 10
                                    }
                                }
                                Text {
                                    width: Number(client.columns[2])
                                    height: 48
                                    verticalAlignment: Text.AlignVCenter
                                    leftPadding: 9
                                    rightPadding: 12
                                    text: trackRow.item.artist
                                    color: "#9c92aa"
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                }
                                Text {
                                    width: Number(client.columns[3])
                                    height: 48
                                    verticalAlignment: Text.AlignVCenter
                                    leftPadding: 9
                                    rightPadding: 12
                                    text: trackRow.item.album
                                    color: "#9c92aa"
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                }
                                Text {
                                    width: Number(client.columns[4])
                                    height: 48
                                    verticalAlignment: Text.AlignVCenter
                                    leftPadding: 9
                                    text: trackRow.item.duration
                                    color: "#80758f"
                                    font.pixelSize: 11
                                }
                            }
                            HoverHandler {
                                id: rowHover
                            }
                            TapHandler {
                                acceptedButtons: Qt.LeftButton
                                onTapped: eventPoint => {
                                    if (point.position.x < Number(client.columns[0]))
                                        return;
                                    trackList.currentIndex = index;
                                    trackList.forceActiveFocus();
                                    client.selectRow(index, point.modifiers);
                                }
                                onDoubleTapped: if (point.position.x >= Number(client.columns[0]))
                                    client.playRow(index)
                            }
                            TapHandler {
                                acceptedButtons: Qt.RightButton
                                onTapped: eventPoint => {
                                    trackList.currentIndex = index;
                                    root.contextAt(trackRow, eventPoint.position, function() { client.trackMenu(index); });
                                }
                            }
                        }
                    }
                }
                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: client.cards.count === 0 && client.tracks.count === 0 && !client.page.busy
                    Column {
                        anchors.centerIn: parent
                        spacing: 16
                        width: Math.min(parent.width, 420)
                        Image {
                            anchors.horizontalCenter: parent.horizontalCenter
                            source: "image://echo-icons/songs/accent"
                            sourceSize: Qt.size(48, 48)
                            opacity: 0.6
                        }
                        Text {
                            width: parent.width
                            text: client.page.kind === "search" ? "没有找到匹配的音乐" : "把喜欢的音乐，留在这里"
                            color: "#e8e0f1"
                            font.pixelSize: 22
                            horizontalAlignment: Text.AlignHCenter
                        }
                        Text {
                            width: parent.width
                            text: client.page.kind === "search" ? "试试其他歌名、专辑或艺术家" : "添加音乐文件夹，或把音乐文件拖到窗口。"
                            color: "#8e829e"
                            font.pixelSize: 12
                            horizontalAlignment: Text.AlignHCenter
                        }
                        PillButton {
                            anchors.horizontalCenter: parent.horizontalCenter
                            visible: client.page.kind !== "search"
                            primary: true
                            text: "添加音乐文件夹"
                            onClicked: client.action("addSource")
                        }
                    }
                }
                BusyIndicator {
                    Layout.alignment: Qt.AlignHCenter
                    running: client.page.busy
                    visible: running
                    implicitWidth: 30
                    implicitHeight: 30
                }
            }
        }
        QueuePanel {
            id: queuePanel
            controller: client
            opened: root.libraryQueueOpen
            reduceMotion: client.reduceMotion
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.bottomMargin: playerBar.height
            // Keep the library sidebar's existing reflow and resize behavior.
            x: root.width - width * openness
            width: 300
        }
    }
    PlayerBar {
        id: playerBar
        anchors.left: parent.left
        anchors.right: parent.right
        y: root.height - height + (1 - root.controlsReveal) * (height + 12)
        z: 5
        opacity: root.controlsReveal
        visible: opacity > 0
        enabled: root.controlsShown
        playback: root.playbackObject
        reduceMotion: client.reduceMotion
        queueOpen: root.queueOpen
        onExpansionRequested: {
            root.expanded = !root.expanded;
            root.forceActiveFocus();
        }
        onQueueRequested: {
            root.queueOpen = !root.queueOpen;
            root.forceActiveFocus();
        }
        onInteractingChanged: root.restartControlsTimer()
    }
    // Clip the sliding page above the shared bar during navigation. Once open,
    // let its background extend behind the bar as the controls slide away.
    Item {
        anchors.fill: parent
        anchors.bottomMargin: root.reveal < 1 ? playerBar.height : 0
        clip: true
        z: 4
        NowPlaying {
            id: nowPlaying
            z: 4
            objectName: "nowPlaying"
            width: root.width
            height: root.height
            y: (1 - root.reveal) * (root.height - playerBar.height)
            bottomInset: playerBar.height
            controlsReveal: root.controlsReveal
            controlsShown: root.controlsShown
            topInset: windowChrome.height
            dynamicBackground: Boolean(client.preferences.dynamicPlaybackBackground)
            playback: root.playbackObject
            expanded: root.expanded
            reduceMotion: client.reduceMotion
            visible: root.reveal > 0
            enabled: root.expanded
            onCollapse: {
                root.expanded = false;
                root.forceActiveFocus();
            }
        }
    }
    Item {
        id: playbackQueueOverlay
        objectName: "playbackQueueOverlay"
        anchors.fill: parent
        anchors.bottomMargin: playerBar.height
        z: 4.5
        visible: root.reveal > 0 && playbackQueuePanel.openness > 0
        enabled: root.expanded
        Rectangle {
            anchors.fill: parent
            color: "#28000000"
            opacity: playbackQueuePanel.openness
            MouseArea {
                objectName: "playbackQueueDismiss"
                anchors.fill: parent
                acceptedButtons: Qt.AllButtons
                onClicked: root.closeQueue()
                onWheel: wheel => wheel.accepted = true
            }
        }
        QueuePanel {
            id: playbackQueuePanel
            objectName: "playbackQueuePanel"
            listObjectName: "playbackQueueList"
            controller: client
            opened: root.expanded && root.queueOpen
            reduceMotion: client.reduceMotion
            dismissable: true
            radius: 14
            border.color: "#25ffffff"
            width: 320
            height: Math.max(0, parent.height - windowChrome.height - 24)
            x: parent.width - (width + 12) * openness
            y: windowChrome.height + 12
            onCloseRequested: root.closeQueue()
        }
    }
    WindowBar {
        id: windowChrome
        sidebarWidth: root.sideWidth
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        z: 10
    }
    WindowEdges {
        anchors.fill: parent
        z: 20
    }
    DropArea {
        anchors.fill: parent
        onEntered: drag => drag.accepted = drag.hasUrls
        onDropped: drop => {
            if (drop.hasUrls) {
                client.openUrls(drop.urls);
                drop.acceptProposedAction();
            }
        }
    }
    Connections {
        target: client
        function onFocusSearch() {
            root.expanded = false;
            searchBox.forceActiveFocus();
            searchBox.selectAll();
        }
        function onNavigationStarted() {
            pageContent.switching = true;
            pageContent.opacity = 0;
        }
        function onPageChanged() {
            if (client.page.kind !== "search" && searchBox.text.length > 0)
                searchBox.text = "";
            else if (client.page.kind === "search" && searchBox.text.trim() !== client.page.query)
                searchBox.text = client.page.query;
            if (!client.page.busy)
                Qt.callLater(function () {
                    if (client.tracks.count > 0)
                        trackList.contentY = Math.max(0, Math.min(client.scroll(), trackList.contentHeight - trackList.height));
                    else if (client.page.kind === "artists")
                        artistList.contentY = Math.max(0, Math.min(client.scroll(), artistList.contentHeight - artistList.height));
                    else
                        cardGrid.contentY = Math.max(0, Math.min(client.scroll(), cardGrid.contentHeight - cardGrid.height));
                    pageContent.switching = false;
                    pageContent.opacity = 1;
                });
        }
    }
    AppDialogs { id: appDialogs }
    AppMenu {
        id: appMenu
        objectName: "appMenu"
        onChosen: token => {
            root.contextMenuPoint = Qt.point(appMenu.x, appMenu.y);
            appMenu.close();
            client.triggerMenu(token);
            root.contextMenuPoint = null;
        }
    }
    MiniPlayer { id: miniPlayer }
    TrayPopup { id: trayPopup }
    Connections {
        target: client
        function onMenuRequested(entries) {
            appMenu.entries = entries;
            const point = root.contextMenuPoint || client.cursorPosition();
            appMenu.popup(root, point.x, point.y);
        }
        function onTrayMenuRequested(position) { trayPopup.present(position); }
    }
    readonly property var playbackObject: playback
}
