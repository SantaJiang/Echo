import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "qrc:/echo/quick/theme"
ApplicationWindow {
    visible: true; width: 500; height: 240; title: "Echo"; color: Theme.c24202d
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 28; spacing: 20
        Label { text: "无法打开资料库"; color: Theme.cf3edf9; font.pixelSize: 22 }
        Label { text: startupMessage; color: Theme.cbbb4c8; wrapMode: Text.Wrap; Layout.fillWidth: true; Layout.fillHeight: true }
        PillButton { text: "退出"; primary: true; Layout.alignment: Qt.AlignRight; onClicked: Qt.quit() }
    }
}
