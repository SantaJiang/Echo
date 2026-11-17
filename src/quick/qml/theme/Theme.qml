pragma Singleton
import QtQuick

QtObject {
    id: theme

    function motion(reduced, duration) { return reduced ? 0 : duration }
    function icon(name, tone) { return "image://echo-icons/" + name + "/" + tone + "/" + themeId }

    readonly property int motionShort: 100
    readonly property int motionMedium: 180
    readonly property int motionReveal: 260
    readonly property int motionPanel: 360
    readonly property int radiusCard: 10
    readonly property int radiusControl: 8

    property string themeId: "dusk"
    readonly property QtObject active: themeId === "glacial" ? glacial : dusk

    readonly property color canvas: active.canvas
    readonly property color sidebar: active.sidebar
    readonly property color surface: active.surface
    readonly property color surfaceRaised: active.surfaceRaised
    readonly property color scrim: active.scrim
    readonly property color textPrimary: active.textPrimary
    readonly property color textSecondary: active.textSecondary
    readonly property color textMuted: active.textMuted
    readonly property color accent: active.accent
    readonly property color accentStrong: active.accentStrong
    readonly property color accentText: active.accentText
    readonly property color selection: active.selection
    readonly property color border: active.border
    readonly property color atmosphereTop: active.atmosphereTop
    readonly property color atmosphereBottom: active.atmosphereBottom
    readonly property color glowA: active.glowA
    readonly property color glowB: active.glowB
    readonly property color glowC: active.glowC
    readonly property color artworkBase: active.artworkBase
    readonly property color shadeTop: active.shadeTop
    readonly property color shadeBottom: active.shadeBottom
    readonly property var avatars: [active.avatar0, active.avatar1, active.avatar2, active.avatar3, active.avatar4, active.avatar5]

    readonly property color danger: "#C83B4B"
    readonly property color hover: "#18FFFFFF"
    readonly property color pressed: "#30FFFFFF"
    readonly property color hairline: "#20FFFFFF"
    readonly property color stripe: "#06FFFFFF"

    readonly property var choices: [
        { "id": "dusk", "name": "暮紫", "canvas": "#17151D", "accent": "#C9AAFF", "text": "#F3EDF9" },
        { "id": "glacial", "name": "霜青", "canvas": "#101418", "accent": "#9FD4E8", "text": "#E8F1F6" }
    ]

    readonly property QtObject dusk: QtObject {
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
        readonly property color avatar0: "#55436B"
        readonly property color avatar1: "#40566B"
        readonly property color avatar2: "#61506A"
        readonly property color avatar3: "#46615B"
        readonly property color avatar4: "#685344"
        readonly property color avatar5: "#4B4C70"
    }
    readonly property QtObject glacial: QtObject {
        readonly property color canvas: "#101418"
        readonly property color sidebar: "#161C22"
        readonly property color surface: "#1C242C"
        readonly property color surfaceRaised: "#24303A"
        readonly property color scrim: "#990C1014"
        readonly property color textPrimary: "#E8F1F6"
        readonly property color textSecondary: "#B4C4D0"
        readonly property color textMuted: "#7D8E9C"
        readonly property color accent: "#9FD4E8"
        readonly property color accentStrong: "#C5E6F2"
        readonly property color accentText: "#102028"
        readonly property color selection: "#2A3C4A"
        readonly property color border: "#3E5262"
        readonly property color atmosphereTop: "#0B1014"
        readonly property color atmosphereBottom: "#10161C"
        readonly property color glowA: "#0A1012"
        readonly property color glowB: "#070B0F"
        readonly property color glowC: "#0B0D0D"
        readonly property color artworkBase: "#12181E"
        readonly property color shadeTop: "#14202A"
        readonly property color shadeBottom: "#101418"
        readonly property color avatar0: "#3A4E60"
        readonly property color avatar1: "#465868"
        readonly property color avatar2: "#3E5560"
        readonly property color avatar3: "#4A6070"
        readonly property color avatar4: "#3A4A58"
        readonly property color avatar5: "#506070"
    }
}
