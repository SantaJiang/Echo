import QtQuick
import QtQuick.Layouts

RowLayout {
    id: transport
    required property var playback
    property string namePrefix: "bar"
    property bool reduceMotion: false
    property int buttonSize: 44
    property int iconSize: 24
    property int toggleSize: 56
    spacing: 10
    readonly property bool busy: previousButton.down || toggleButton.down || nextButton.down
    ActionButton {
        id: previousButton
        objectName: transport.namePrefix + "Previous"
        Layout.alignment: Qt.AlignVCenter
        implicitWidth: transport.buttonSize
        implicitHeight: transport.buttonSize
        iconSize: transport.iconSize
        reduceMotion: transport.reduceMotion
        symbol: "previous"
        label: "上一首"
        onClicked: transport.playback.previous()
    }
    ActionButton {
        id: toggleButton
        objectName: transport.namePrefix + "Toggle"
        Layout.alignment: Qt.AlignVCenter
        implicitWidth: transport.toggleSize
        implicitHeight: transport.toggleSize
        iconSize: transport.buttonSize === 32 ? 20 : 26
        prominent: true
        reduceMotion: transport.reduceMotion
        symbol: transport.playback.playing ? "pause" : "play"
        label: transport.playback.playing ? "暂停" : "播放"
        onClicked: transport.playback.toggle()
    }
    ActionButton {
        id: nextButton
        objectName: transport.namePrefix + "Next"
        Layout.alignment: Qt.AlignVCenter
        implicitWidth: transport.buttonSize
        implicitHeight: transport.buttonSize
        iconSize: transport.iconSize
        reduceMotion: transport.reduceMotion
        symbol: "next"
        label: "下一首"
        onClicked: transport.playback.next()
    }
}
