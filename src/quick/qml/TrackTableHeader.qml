import QtQuick
import "qrc:/echo/quick/theme"

Item {
    id: header
    required property var controller
    required property real trackWidth
    width: trackWidth
    height: 32
    Row {
        Repeater {
            model: ["", "歌曲", "艺术家", "专辑", "时长"]
            delegate: Item {
                id: heading
                required property string modelData
                required property int index
                width: Number(header.controller.columns[index])
                height: 32
                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 9
                    anchors.verticalCenter: parent.verticalCenter
                    text: heading.modelData + (header.controller.page.sort === heading.index ? header.controller.page.descending ? " ↓" : " ↑" : "")
                    color: Theme.textMuted
                    font.pixelSize: 11
                }
                TapHandler {
                    onTapped: header.controller.sortBy(index)
                }
                MouseArea {
                    width: 7
                    height: parent.height
                    anchors.right: parent.right
                    cursorShape: Qt.SplitHCursor
                    property real startX
                    property real startWidth
                    onPressed: mouse => {
                        startX = mapToItem(header, mouse.x, 0).x;
                        startWidth = heading.width;
                    }
                    onPositionChanged: mouse => {
                        if (pressed)
                            header.controller.resizeColumn(heading.index, startWidth + mapToItem(header, mouse.x, 0).x - startX);
                    }
                }
            }
        }
    }
    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: Theme.hover
    }
}
