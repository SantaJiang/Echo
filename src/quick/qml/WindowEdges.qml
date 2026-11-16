import QtQuick

Item {
    id: edges
    visible: Qt.platform.os !== "osx" && Qt.platform.os !== "macos" && Qt.platform.os !== "windows" && !chrome.maximized && !chrome.fullscreen
    Repeater {
        model: [Qt.TopEdge, Qt.BottomEdge, Qt.LeftEdge, Qt.RightEdge, Qt.TopEdge | Qt.LeftEdge, Qt.TopEdge | Qt.RightEdge, Qt.BottomEdge | Qt.LeftEdge, Qt.BottomEdge | Qt.RightEdge]
        MouseArea {
            required property int modelData
            readonly property bool edgeLeft: (modelData & Qt.LeftEdge) !== 0
            readonly property bool edgeRight: (modelData & Qt.RightEdge) !== 0
            readonly property bool edgeTop: (modelData & Qt.TopEdge) !== 0
            readonly property bool edgeBottom: (modelData & Qt.BottomEdge) !== 0
            width: edgeLeft || edgeRight ? 6 : edges.width - 12
            height: edgeTop || edgeBottom ? 6 : edges.height - 12
            x: edgeLeft ? 0 : edgeRight ? edges.width - width : 6
            y: edgeTop ? 0 : edgeBottom ? edges.height - height : 6
            cursorShape: (edgeLeft || edgeRight) && (edgeTop || edgeBottom) ? ((edgeLeft && edgeTop) || (edgeRight && edgeBottom) ? Qt.SizeFDiagCursor : Qt.SizeBDiagCursor) : edgeLeft || edgeRight ? Qt.SizeHorCursor : Qt.SizeVerCursor
            onPressed: chrome.resizeWindow(modelData)
        }
    }
}
