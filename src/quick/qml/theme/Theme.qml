pragma Singleton
import QtQuick

QtObject {
    id: theme

    function motion(reduced, duration) { return reduced ? 0 : duration }
    function icon(name, tone) { return "image://echo-icons/" + name + "/" + tone }

    readonly property int motionShort: 100
    readonly property int motionMedium: 180
    readonly property int motionReveal: 260
    readonly property int motionPanel: 360
    readonly property int radiusCard: 10
    readonly property int radiusControl: 8

    readonly property color canvas: "#17151D"
    readonly property color sidebar: "#201D28"
    readonly property color surface: "#24202D"
    readonly property color surfaceRaised: "#302C3D"
    readonly property color scrim: "#990B0910"
    readonly property color textPrimary: "#F3EDF9"
    readonly property color textSecondary: "#BBB4C8"
    readonly property color textMuted: "#847E90"
    readonly property color accent: "#C9AAFF"
    readonly property color accentStrong: "#CFB3F7"
    readonly property color accentText: "#21172C"
    readonly property color selection: "#42344E"
    readonly property color border: "#51465F"
    readonly property color atmosphereTop: "#0D0B12"
    readonly property color atmosphereBottom: "#111016"
    readonly property color glowA: "#0E0617"
    readonly property color glowB: "#14080B"
    readonly property color glowC: "#060503"
    readonly property color artworkBase: "#16141D"
    readonly property color shadeTop: "#121824"
    readonly property color shadeBottom: "#111016"
    readonly property var avatars: ["#55436B", "#40566B", "#61506A", "#46615B", "#685344", "#4B4C70"]

    readonly property color danger: "#C83B4B"
    readonly property color hover: "#18FFFFFF"
    readonly property color pressed: "#30FFFFFF"
    readonly property color hairline: "#20FFFFFF"
    readonly property color stripe: "#06FFFFFF"
}
