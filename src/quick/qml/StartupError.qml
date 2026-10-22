import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
ApplicationWindow {
    visible: true; width: 500; height: 240; title: "Echo"; color: "#24202d"
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 28; spacing: 20
        Label { text: "无法打开资料库"; color: "#f3edf9"; font.pixelSize: 22 }
        Label { text: startupMessage; color: "#bbb4c8"; wrapMode: Text.Wrap; Layout.fillWidth: true; Layout.fillHeight: true }
        PillButton { text: "退出"; primary: true; Layout.alignment: Qt.AlignRight; onClicked: Qt.quit() }
    }
}
