import QtQuick
import QtQuick.Controls
import QtQuick.Window
Window {
    id: tray
    width: 190; height: 180; color: "transparent"
    flags: Qt.Popup | Qt.FramelessWindowHint
    function present(point) {
        x = Math.max(Screen.virtualX, Math.min(point.x, Screen.virtualX + Screen.width-width));
        y = Math.max(Screen.virtualY, Math.min(point.y, Screen.virtualY + Screen.height-height));
        show(); requestActivate();
    }
    onActiveChanged: if (!active) hide()
    Rectangle { anchors.fill: parent; radius: 10; color: "#292330"; border.color: "#51465f" }
    Column {
        anchors.fill: parent; anchors.margins: 8; spacing: 4
        Repeater {
            model: [{text:"打开 Echo", action:"open"}, {text:"迷你播放器", action:"mini"}, {text:"设置", action:"settings"}, {text:"退出 Echo", action:"quit"}]
            PillButton {
                required property var modelData
                width: 174; text: modelData.text; flat: true
                onClicked: { tray.hide(); if (modelData.action === "open") client.showMainWindow(); else client.action(modelData.action); }
            }
        }
    }
}
