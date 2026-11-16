import QtQuick
import QtQuick.Controls
import "qrc:/echo/quick/theme"

Item {
    id: bar
    objectName: "windowBar"
    property string platformName: Qt.platform.os
    readonly property bool mac: platformName === "osx" || platformName === "macos"
    readonly property bool windows: platformName === "windows"
    property real sidebarWidth: 218
    // 导航内容的起点。拖动区停在这里，避免盖住已经上移的侧栏。
    property real sidebarContentTop: 48
    height: 48
    // macOS 的红绿灯占住左上角，拖动区从 88 起并避开搜索栏所在的一行。
    // Windows 和 Linux 的左侧导航上移后，只有导航上方的窄条和右侧标题栏还能拖动。
    DragSurface {
        objectName: "windowDragArea"
        x: bar.mac ? 88 : bar.sidebarWidth
        width: Math.max(0, bar.width - x - (bar.mac ? 6 : controls.width + 8))
        height: bar.mac ? 24 : bar.height
    }
    DragSurface {
        visible: !bar.mac
        x: 6
        width: Math.max(0, bar.sidebarWidth - x)
        height: Math.min(bar.height, bar.sidebarContentTop)
    }
    DragSurface {
        visible: bar.mac
        x: 88
        y: 24
        width: bar.sidebarWidth - x
        height: 24
    }
    DragSurface {
        visible: bar.mac
        x: bar.sidebarWidth + 432
        y: 24
        width: Math.max(0, bar.width - x - (controls.width + 8))
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
                color: control.windowAction === "close" && control.hovered ? Theme.cc83b4b : control.down ? Theme.c36ffffff : control.hovered || (control.windowAction === "maximize" && chrome.maximizeHovered) ? Theme.c20ffffff : "transparent"
                border.width: control.visualFocus ? 1 : 0
                border.color: Theme.cc9aaff
            }
        }
        contentItem: Item {
            readonly property color ink: chrome.active || control.hovered ? Theme.ce7e2ed : Theme.c88818e
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
                    color: chrome.maximized ? Theme.c211c28 : "transparent"
                    border.color: control.contentItem.ink
                }
            }
        }
    }
}
