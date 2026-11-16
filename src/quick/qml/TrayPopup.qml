import QtQuick
import QtQuick.Controls
import QtQuick.Window
import QtQuick.Layouts
import "qrc:/echo/quick/theme"
Window {
    id: tray
    required property var controller
    width: 190; height: 180; color: "transparent"
    flags: Qt.Popup | Qt.FramelessWindowHint
    function present(point) {
        x = Math.max(Screen.virtualX, Math.min(point.x, Screen.virtualX + Screen.width-width));
        y = Math.max(Screen.virtualY, Math.min(point.y, Screen.virtualY + Screen.height-height));
        show(); requestActivate();
    }
    onActiveChanged: if (!active) hide()
    Rectangle { anchors.fill: parent; radius: 10; color: Theme.c292330; border.color: Theme.c51465f }
    ColumnLayout {
        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
        anchors.margins: 8; spacing: 4
        Repeater {
            model: [{text:"打开 Echo", action:"open"}, {text:"迷你播放器", action:"mini"}, {text:"设置", action:"settings"}, {text:"退出 Echo", action:"quit"}]
            PillButton {
                required property var modelData
                Layout.fillWidth: true; text: modelData.text; flat: true
                onClicked: { tray.hide(); if (modelData.action === "open") tray.controller.showMainWindow(); else tray.controller.action(modelData.action); }
            }
        }
    }
}
