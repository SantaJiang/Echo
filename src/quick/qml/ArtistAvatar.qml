import QtQuick
import "qrc:/echo/quick/theme"

Rectangle {
    id: avatar
    property string name: ""
    property bool unknown: name.trim().length === 0 || name === "未知艺术家"
    readonly property string initial: Array.from(name.trim())[0] || ""
    readonly property var avatarColors: [Theme.c55436b, Theme.c40566b, Theme.c61506a, Theme.c46615b, Theme.c685344, Theme.c4b4c70]
    readonly property int colorIndex: {
        let hash = 0;
        for (let i = 0; i < name.length; ++i)
            hash = (hash * 31 + name.charCodeAt(i)) >>> 0;
        return hash % avatarColors.length;
    }
    color: unknown ? Theme.c34313e : avatarColors[colorIndex]
    radius: width / 2
    antialiasing: true
    Text {
        anchors.centerIn: parent
        visible: !avatar.unknown
        text: avatar.initial.toUpperCase()
        color: Theme.ce9e1f2
        font.pixelSize: avatar.width * 0.39
        font.weight: Font.Medium
    }
    Image {
        anchors.centerIn: parent
        visible: avatar.unknown
        width: parent.width * 0.38
        height: width
        source: "image://echo-icons/artists/muted"
        sourceSize: Qt.size(width * Screen.devicePixelRatio, height * Screen.devicePixelRatio)
        smooth: true
    }
}
