import QtQuick
import "qrc:/echo/quick/theme"

Item {
    id: background
    objectName: "playbackBackground"
    property url artwork
    property bool dynamicBackground: false
    readonly property bool usesArtwork: dynamicBackground && artworkImage.status === Image.Ready

    // Only artwork needs a texture. The default atmosphere is evaluated directly
    // at the output resolution, without an enlarged low-resolution gradient.
    Image {
        id: artworkImage
        objectName: "playbackBackgroundImage"
        source: background.dynamicBackground ? background.artwork : ""
        sourceSize: Qt.size(256, 256)
        asynchronous: true
        visible: false
    }

    // Blur at a bounded resolution, retaining fractional color values until the
    // final composition. No timers or animated noise are needed for this effect.
    ShaderEffect {
        id: horizontalBlur
        width: 256
        height: 256
        visible: background.usesArtwork
        property var source: artworkImage
        property vector2d direction: Qt.vector2d(1 / 256, 0)
        fragmentShader: "qrc:/echo/quick/shaders/backgroundblur.frag.qsb"
    }
    ShaderEffectSource {
        id: horizontalTexture
        sourceItem: background.usesArtwork ? horizontalBlur : null
        hideSource: true
        visible: false
        textureSize: Qt.size(256, 256)
        format: ShaderEffectSource.RGBA16F
    }
    ShaderEffect {
        id: verticalBlur
        width: 256
        height: 256
        visible: background.usesArtwork
        property var source: horizontalTexture
        property vector2d direction: Qt.vector2d(0, 1 / 256)
        fragmentShader: "qrc:/echo/quick/shaders/backgroundblur.frag.qsb"
    }
    ShaderEffectSource {
        id: blurredArtwork
        sourceItem: background.usesArtwork ? verticalBlur : null
        hideSource: true
        visible: false
        textureSize: Qt.size(256, 256)
        format: ShaderEffectSource.RGBA16F
    }
    ShaderEffect {
        objectName: "playbackBackgroundOutput"
        anchors.fill: parent
        property var source: blurredArtwork
        property real useArtwork: background.usesArtwork ? 1 : 0
        property vector2d viewportSize: Qt.vector2d(width, height)
        property vector2d artworkSize: Qt.vector2d(Math.max(1, artworkImage.implicitWidth),
                                                  Math.max(1, artworkImage.implicitHeight))
        property color atmosphereTop: Theme.atmosphereTop
        property color atmosphereBottom: Theme.atmosphereBottom
        property color glowA: Theme.glowA
        property color glowB: Theme.glowB
        property color glowC: Theme.glowC
        property color artworkBase: Theme.artworkBase
        property color shadeTop: Theme.shadeTop
        property color shadeBottom: Theme.shadeBottom
        fragmentShader: "qrc:/echo/quick/shaders/background.frag.qsb"
    }
}
