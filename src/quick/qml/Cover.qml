import QtQuick
import QtQuick.Effects
import "qrc:/echo/quick/theme"

Rectangle {
    id: cover
    property string artwork: ""
    property real imageScale: 1
    property bool zoomable: false
    property int decodeSize: Math.min(2048, Math.pow(2, Math.ceil(Math.log2(Math.max(128, cover.width * Screen.devicePixelRatio * 2)))))
    property size renderTextureSize: Qt.size(Math.ceil(width * Screen.devicePixelRatio * 2), Math.ceil(height * Screen.devicePixelRatio * 2))
    radius: 12
    color: Theme.c302c3d
    antialiasing: true
    Item {
        id: artworkContent
        anchors.fill: parent
        visible: cover.zoomable
        Image {
            id: fallback
            anchors.fill: parent
            source: "image://echo-icons/cover/light"
            sourceSize: Qt.size(cover.decodeSize, cover.decodeSize)
            smooth: true
            fillMode: Image.PreserveAspectCrop
            scale: cover.imageScale
            visible: image.status !== Image.Ready
        }
        Image {
            id: image
            objectName: "coverImage"
            anchors.fill: parent
            source: cover.artwork.length ? cover.artwork : "image://echo-icons/cover/light"
            asynchronous: true
            sourceSize: Qt.size(cover.decodeSize, cover.decodeSize)
            smooth: true
            fillMode: Image.PreserveAspectCrop
            scale: cover.imageScale
            visible: status === Image.Ready
        }
    }
    ShaderEffectSource {
        id: zoomSource
        anchors.fill: parent
        sourceItem: cover.zoomable ? artworkContent : null
        hideSource: true
        live: true
        smooth: true
        visible: false
        textureSize: cover.renderTextureSize
    }
    Rectangle {
        id: mask
        anchors.fill: parent
        radius: cover.radius
        antialiasing: true
        layer.enabled: true
        layer.smooth: true
        layer.samples: 4
        layer.textureSize: cover.renderTextureSize
        visible: false
    }
    MultiEffect {
        objectName: "coverEffect"
        anchors.fill: parent
        source: cover.zoomable ? zoomSource : image.status === Image.Ready ? image : fallback
        maskEnabled: true
        maskThresholdMin: 0.5
        maskSpreadAtMin: 1.0
        maskSource: mask
    }
    Rectangle {
        anchors.fill: parent
        radius: cover.radius
        color: "transparent"
        border.color: Theme.c15ffffff
    }
}
