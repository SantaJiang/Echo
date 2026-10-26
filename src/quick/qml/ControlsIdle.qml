import QtQuick

Item {
    id: idle
    property bool expanded: false
    property bool queueOpen: false
    property bool transitioning: false
    property bool interacting: false
    property bool dialogActive: false
    property bool menuOpened: false
    property real panelOpenness: 0
    property bool playing: false
    property bool controlsIdle: false
    readonly property bool canAutoHide: expanded && !transitioning && !queueOpen && panelOpenness === 0 && !interacting && !dialogActive && !menuOpened
    readonly property bool controlsShown: !expanded || transitioning || !controlsIdle || queueOpen || panelOpenness > 0 || interacting || dialogActive || menuOpened
    onCanAutoHideChanged: restart()
    onExpandedChanged: showControls()
    onPlayingChanged: showControls()
    onTransitioningChanged: restart()
    onQueueOpenChanged: showControls()
    function showControls() {
        controlsIdle = false;
        restart();
    }
    function restart() {
        hideTimer.stop();
        if (canAutoHide)
            hideTimer.restart();
    }
    Timer {
        id: hideTimer
        objectName: "controlsHideTimer"
        interval: 3000
        onTriggered: if (idle.canAutoHide) idle.controlsIdle = true
    }
    HoverHandler {
        enabled: idle.expanded
        property point lastPosition: Qt.point(-1, -1)
        onHoveredChanged: if (hovered) idle.showControls()
        onPointChanged: {
            const position = point.position;
            if (position.x !== lastPosition.x || position.y !== lastPosition.y) {
                lastPosition = Qt.point(position.x, position.y);
                if (hovered) idle.showControls();
            }
        }
    }
    TapHandler {
        enabled: idle.expanded
        onTapped: idle.showControls()
    }
}
