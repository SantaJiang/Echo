import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "qrc:/echo/quick/theme"

RowLayout {
    id: pageHeader
    required property var controller
    objectName: "libraryHeader"
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
        artwork: pageHeader.controller.page.artwork
        visible: ["album", "playlist"].includes(pageHeader.controller.page.kind) && pageHeader.controller.tracks.count > 0
    }
    ArtistAvatar {
        id: headerArtist
        Layout.preferredWidth: 80
        Layout.preferredHeight: 80
        name: pageHeader.controller.page.title
        visible: pageHeader.controller.page.kind === "artist" && pageHeader.controller.tracks.count > 0
    }
    ColumnLayout {
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignVCenter
        spacing: headerArtwork.visible || headerArtist.visible ? 10 : 7
        Text {
            Layout.fillWidth: true
            text: pageHeader.controller.page.title
            color: Theme.textPrimary
            font.pixelSize: headerArtwork.visible || headerArtist.visible ? 28 : 30
            font.weight: headerArtwork.visible || headerArtist.visible ? Font.DemiBold : Font.Bold
            elide: Text.ElideRight
        }
        Text {
            Layout.fillWidth: true
            text: pageHeader.controller.page.busy ? "正在读取资料库…" : pageHeader.controller.page.subtitle
            color: Theme.textMuted
            font.pixelSize: headerArtwork.visible || headerArtist.visible ? 13 : 12
            elide: Text.ElideMiddle
        }
    }
    PillButton {
        text: "···"
        visible: pageHeader.controller.page.kind === "source" || pageHeader.controller.page.kind === "playlist"
        flat: true
        Accessible.name: "当前页面菜单"
        onClicked: pageHeader.controller.action("pageMenu")
    }
    PillButton {
        primary: true
        text: "播放全部"
        enabled: !pageHeader.controller.page.busy && pageHeader.controller.tracks.count > 0
        visible: pageHeader.controller.tracks.count > 0
        onClicked: pageHeader.controller.playAll(false)
    }
    ActionButton {
        symbol: "shuffle"
        label: "随机播放全部"
        visible: pageHeader.controller.tracks.count > 0
        enabled: !pageHeader.controller.page.busy
        onClicked: pageHeader.controller.playAll(true)
    }
    ActionButton {
        symbol: "plus"
        label: "添加音乐文件夹"
        visible: pageHeader.controller.page.kind === "sources" || pageHeader.controller.page.kind === "songs"
        onClicked: pageHeader.controller.action("addSource")
    }
}
