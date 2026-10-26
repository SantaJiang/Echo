import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: emptyState
    required property var controller
    objectName: "libraryEmptyState"
    Layout.fillWidth: true
    Layout.fillHeight: true
    visible: emptyState.controller.cards.count === 0 && emptyState.controller.tracks.count === 0 && !emptyState.controller.page.busy
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
            text: emptyState.controller.page.kind === "search" ? "没有找到匹配的音乐" : "把喜欢的音乐，留在这里"
            color: "#e8e0f1"
            font.pixelSize: 22
            horizontalAlignment: Text.AlignHCenter
        }
        Text {
            width: parent.width
            text: emptyState.controller.page.kind === "search" ? "试试其他歌名、专辑或艺术家" : "添加音乐文件夹，或把音乐文件拖到窗口。"
            color: "#8e829e"
            font.pixelSize: 12
            horizontalAlignment: Text.AlignHCenter
        }
        PillButton {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: emptyState.controller.page.kind !== "search"
            primary: true
            text: "添加音乐文件夹"
            onClicked: emptyState.controller.action("addSource")
        }
    }
}
