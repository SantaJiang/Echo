import QtQuick

ActionButton {
    id: control
    property bool favorite: false
    property int trackId: 0
    symbol: favorite ? "heartFilled" : "heart"
    active: favorite
    label: favorite ? "取消喜欢" : "喜欢"
    enabled: trackId > 0
}
