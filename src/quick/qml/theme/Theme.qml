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
    readonly property QtObject active: themeId === "nocturne" ? nocturne
        : themeId === "glacial" ? glacial
        : themeId === "verdant" ? verdant
        : dusk

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
        { "id": "nocturne", "name": "墨金", "canvas": "#16130F", "accent": "#E4C48A", "text": "#F6F0E6" },
        { "id": "glacial", "name": "霜青", "canvas": "#101418", "accent": "#9FD4E8", "text": "#E8F1F6" },
        { "id": "verdant", "name": "松烟", "canvas": "#101614", "accent": "#8ED4B8", "text": "#E8F3EE" }
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
    readonly property QtObject nocturne: QtObject {
        readonly property color canvas: "#16130F"
        readonly property color sidebar: "#1E1A15"
        readonly property color surface: "#262119"
        readonly property color surfaceRaised: "#332C22"
        readonly property color scrim: "#9914100C"
        readonly property color textPrimary: "#F6F0E6"
        readonly property color textSecondary: "#CBBFAA"
        readonly property color textMuted: "#8F8474"
        readonly property color accent: "#E4C48A"
        readonly property color accentStrong: "#F0D7A8"
        readonly property color accentText: "#241C10"
        readonly property color selection: "#4A3D2A"
        readonly property color border: "#5C5142"
        readonly property color atmosphereTop: "#100E0B"
        readonly property color atmosphereBottom: "#16130F"
        readonly property color glowA: "#120D06"
        readonly property color glowB: "#100A06"
        readonly property color glowC: "#100D09"
        readonly property color artworkBase: "#1A1612"
        readonly property color shadeTop: "#241C14"
        readonly property color shadeBottom: "#14110E"
        readonly property color avatar0: "#5C4630"
        readonly property color avatar1: "#6A5340"
        readonly property color avatar2: "#4E4638"
        readonly property color avatar3: "#5A4034"
        readonly property color avatar4: "#3E4A40"
        readonly property color avatar5: "#6A5848"
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
    readonly property QtObject verdant: QtObject {
        readonly property color canvas: "#101614"
        readonly property color sidebar: "#161E1B"
        readonly property color surface: "#1C2823"
        readonly property color surfaceRaised: "#24342E"
        readonly property color scrim: "#990C100E"
        readonly property color textPrimary: "#E8F3EE"
        readonly property color textSecondary: "#B4C9C0"
        readonly property color textMuted: "#7D948A"
        readonly property color accent: "#8ED4B8"
        readonly property color accentStrong: "#B7E8D4"
        readonly property color accentText: "#102018"
        readonly property color selection: "#2A463C"
        readonly property color border: "#3E5A4E"
        readonly property color atmosphereTop: "#0B100E"
        readonly property color atmosphereBottom: "#101614"
        readonly property color glowA: "#080F0C"
        readonly property color glowB: "#060B08"
        readonly property color glowC: "#0D0F0A"
        readonly property color artworkBase: "#121A16"
        readonly property color shadeTop: "#14241E"
        readonly property color shadeBottom: "#101412"
        readonly property color avatar0: "#3A5648"
        readonly property color avatar1: "#466058"
        readonly property color avatar2: "#3E5048"
        readonly property color avatar3: "#4A6858"
        readonly property color avatar4: "#3A5040"
        readonly property color avatar5: "#506858"
    }
}
