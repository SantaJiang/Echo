import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "qrc:/echo/quick/theme"

FocusScope {
    id: root
    objectName: "quickShell"
    property bool expanded: false
    property bool queueOpen: false
    property var contextMenuPoint: null
    onQueueOpenChanged: showControls()
    function contextAt(item, position, action) {
        contextMenuPoint = item.mapToItem(root, position.x, position.y);
        action();
        contextMenuPoint = null;
    }
    property real reveal: expanded ? 1 : 0
    readonly property bool transitioning: reveal > 0 && reveal < 1
    ControlsIdle {
        id: controlsIdle
        anchors.fill: parent
        z: -1
        expanded: root.expanded
        queueOpen: root.queueOpen
        transitioning: root.transitioning
        interacting: playerBar.interacting
        dialogActive: appDialogs.active
        menuOpened: appMenu.opened
        panelOpenness: queuePanel.openness
        playing: playback.playing
    }
    readonly property bool controlsShown: controlsIdle.controlsShown
    readonly property bool canAutoHideControls: controlsIdle.canAutoHide
    property real controlsReveal: controlsShown ? 1 : 0
    function closeQueue() {
        queueOpen = false;
        showControls();
        root.forceActiveFocus();
    }
    function showControls() {
        controlsIdle.showControls();
    }
    Behavior on controlsReveal {
        NumberAnimation {
            duration: Theme.motion(client.reduceMotion, Theme.motionReveal)
            easing.type: Easing.OutCubic
        }
    }
    readonly property real sideWidth: 218
    clip: true
    focus: true
    Behavior on reveal {
        NumberAnimation {
            duration: Theme.motion(client.reduceMotion, Theme.motionPanel)
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
        enabled: !appDialogs.active && !libraryView.searchActive
        onActivated: client.action("undo")
    }
    Shortcut {
        sequences: [StandardKey.Redo]
        enabled: !appDialogs.active && !libraryView.searchActive
        onActivated: client.action("redo")
    }
    Shortcut {
        sequences: [StandardKey.SelectAll]
        enabled: !appDialogs.active && !libraryView.searchActive && !root.expanded
        onActivated: client.selectAll()
    }
    Rectangle {
        anchors.fill: parent
        color: Theme.c17151d
    }
    Item {
        id: libraryShell
        anchors.fill: parent
        anchors.bottomMargin: playerBar.height
        visible: root.reveal < 1
        enabled: !root.expanded
        RowLayout {
            anchors.fill: parent
            spacing: 0
            NavigationSidebar {
                id: sidebar
                controller: client
                topInset: windowChrome.height
                Layout.preferredWidth: root.sideWidth
                Layout.minimumWidth: root.sideWidth
                Layout.maximumWidth: root.sideWidth
                Layout.fillHeight: true
                onContextRequested: (sender, position, row) => root.contextAt(sender, position, () => client.navigationMenu(row))
                onQueueDismissRequested: root.queueOpen = false
            }
            LibraryView {
                id: libraryView
                controller: client
                windowWidth: root.width
                fullContentWidth: libraryShell.width - sidebar.width - 2 * horizontalMargin
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumWidth: 0
                Layout.leftMargin: horizontalMargin
                Layout.rightMargin: horizontalMargin
                Layout.topMargin: 28
                onCardContextRequested: (sender, position, row) => root.contextAt(sender, position, () => client.cardMenu(row))
                onTrackContextRequested: (sender, position, row) => root.contextAt(sender, position, () => client.trackMenu(row))
                onQueueDismissRequested: root.queueOpen = false
            }
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
        onInteractingChanged: root.showControls()
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
        id: queueOverlay
        objectName: "queueOverlay"
        anchors.fill: parent
        anchors.bottomMargin: playerBar.height
        z: 4.5
        visible: queuePanel.openness > 0
        MouseArea {
            objectName: "queueDismissBackground"
            anchors.fill: parent
            anchors.topMargin: windowChrome.height
            acceptedButtons: Qt.AllButtons
            onClicked: root.closeQueue()
            onWheel: wheel => wheel.accepted = true
        }
        QueuePanel {
            id: queuePanel
            objectName: "queuePanel"
            listObjectName: "queueList"
            controller: client
            opened: root.queueOpen
            reduceMotion: client.reduceMotion
            dismissable: true
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
            libraryView.focusSearch();
        }
    }
    AppDialogs { id: appDialogs; controller: client }
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
    MiniPlayer { id: miniPlayer; controller: client; playback: root.playbackObject; mainWindow: chrome }
    Loader {
        id: trayPopup
        active: Qt.platform.os !== "osx" && Qt.platform.os !== "macos"
        sourceComponent: TrayPopup { controller: client }
    }
    Connections {
        target: client
        function onMenuRequested(entries) {
            appMenu.entries = entries;
            const point = root.contextMenuPoint || client.cursorPosition();
            appMenu.popup(root, point.x, point.y);
        }
        function onTrayMenuRequested(position) { if (trayPopup.item) trayPopup.item.present(position); }
    }
    readonly property var playbackObject: playback
}
