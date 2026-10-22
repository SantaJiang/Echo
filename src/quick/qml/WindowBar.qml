import QtQuick
import QtQuick.Controls

Item {
    id: bar
    objectName: "windowBar"
    property string platformName: Qt.platform.os
    readonly property bool mac: platformName === "osx" || platformName === "macos"
    readonly property bool windows: platformName === "windows"
    property real sidebarWidth: 218
    height: 48
    DragSurface {
        objectName: "windowDragArea"
        x: bar.mac ? 88 : 6
        width: Math.max(0, bar.width - x - (bar.mac ? 6 : controls.width + 8))
        height: 24
    }
    DragSurface {
        x: bar.mac ? 88 : 6
        y: 24
        width: bar.sidebarWidth - x
        height: 24
    }
    DragSurface {
        x: 650
        y: 24
        width: Math.max(0, bar.width - x - (bar.mac ? 6 : controls.width + 8))
        height: 24
    }
    component DragSurface: MouseArea {
        acceptedButtons: Qt.LeftButton
        property point pressPosition
        property bool moveStarted: false
        onPressed: mouse => {
            pressPosition = Qt.point(mouse.x, mouse.y);
            moveStarted = false;
        }
        onPositionChanged: mouse => {
            if (pressed && !moveStarted && Math.abs(mouse.x - pressPosition.x) + Math.abs(mouse.y - pressPosition.y) >= Qt.styleHints.startDragDistance) {
                moveStarted = true;
                chrome.beginWindowMove();
            }
        }
        onDoubleClicked: chrome.toggleMaximized()
    }
    Row {
        id: controls
        anchors.right: parent.right
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        visible: !bar.mac
        WindowButton {
            objectName: "windowMinimize"
            windowAction: "minimize"
            Accessible.name: "最小化"
            onClicked: chrome.minimizeWindow()
        }
        WindowButton {
            objectName: "windowMaximize"
            windowAction: "maximize"
            Accessible.name: chrome.maximized ? "还原窗口" : "最大化"
            onClicked: chrome.toggleMaximized()
        }
        WindowButton {
            objectName: "windowClose"
            windowAction: "close"
            Accessible.name: "关闭窗口"
            onClicked: chrome.closeWindow()
        }
    }
    component WindowButton: Button {
        id: control
        required property string windowAction
        width: bar.windows ? 46 : 44
        height: 40
        padding: 0
        hoverEnabled: true
        focusPolicy: Qt.TabFocus
        EchoToolTip {
            parent: control
            visible: control.hovered && control.enabled && control.visible && !control.down
            text: control.Accessible.name
        }
        background: Item {
            Rectangle {
                anchors.centerIn: parent
                width: bar.windows ? parent.width : 30
                height: bar.windows ? parent.height : 30
                radius: bar.windows ? 4 : 15
                antialiasing: true
                color: control.windowAction === "close" && control.hovered ? "#c83b4b" : control.down ? "#36ffffff" : control.hovered || (control.windowAction === "maximize" && chrome.maximizeHovered) ? "#20ffffff" : "transparent"
                border.width: control.visualFocus ? 1 : 0
                border.color: "#c9aaff"
            }
        }
        contentItem: Item {
            readonly property color ink: chrome.active || control.hovered ? "#e7e2ed" : "#88818e"
            Rectangle {
                anchors.centerIn: parent
                width: 11
                height: 1
                color: parent.ink
                visible: control.windowAction === "minimize"
            }
            Item {
                anchors.centerIn: parent
                width: 12
                height: 12
                visible: control.windowAction === "close"
                Rectangle {
                    anchors.centerIn: parent
                    width: 13
                    height: 1.2
                    rotation: 45
                    antialiasing: true
                    color: control.contentItem.ink
                }
                Rectangle {
                    anchors.centerIn: parent
                    width: 13
                    height: 1.2
                    rotation: -45
                    antialiasing: true
                    color: control.contentItem.ink
                }
            }
            Item {
                anchors.centerIn: parent
                width: 12
                height: 12
                visible: control.windowAction === "maximize"
                Rectangle {
                    x: 3
                    y: 0
                    width: 9
                    height: 9
                    color: "transparent"
                    border.color: control.contentItem.ink
                    visible: chrome.maximized
                }
                Rectangle {
                    x: 0
                    y: chrome.maximized ? 3 : 1
                    width: chrome.maximized ? 9 : 10
                    height: width
                    color: chrome.maximized ? "#211c28" : "transparent"
                    border.color: control.contentItem.ink
                }
            }
        }
    }
}
