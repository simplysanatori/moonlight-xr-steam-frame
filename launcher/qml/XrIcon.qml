// Moonlight XR Native launcher (lane wave4-launcher): the small line pictures of the Settings page's section
// headers and jump list. Drawn as strokes (24 by 24, like the icon sets they follow) with QtQuick.Shapes, so there
// is no icon font or image file to ship. Loaded through a Loader by the pages, so a Qt without QtQuick.Shapes only
// loses the pictures. New file, copied into app/gui by launcher/build.sh. GPLv3.
import QtQuick
import QtQuick.Shapes

Item {
    id: icon
    property string name
    property color color: "#80CBC4"
    property real size: 22

    implicitWidth: size
    implicitHeight: size
    width: size
    height: size

    readonly property var paths: ({
        // a stack of layers
        "profiles": "M12 3 L21 8 L12 13 L3 8 Z M3 12.5 L12 17.5 L21 12.5 M3 16.5 L12 21.5 L21 16.5",
        // Wi-Fi
        "network": "M2.5 9 A14 14 0 0 1 21.5 9 M5.5 12.5 A9.5 9.5 0 0 1 18.5 12.5 M8.7 16 A5 5 0 0 1 15.3 16 M12 19.5 L12.01 19.5",
        // a monitor
        "video": "M3 5 H21 V16 H3 Z M8 20 H16 M12 16 V20",
        // a speaker
        "audio": "M4 9 H8 L13 5 V19 L8 15 H4 Z M16 9 A4.5 4.5 0 0 1 16 15 M18.5 6.5 A8.5 8.5 0 0 1 18.5 17.5",
        // a gamepad
        "gamepad": "M7 8 H17 A5 5 0 0 1 21 16 A2.5 2.5 0 0 1 17.5 17.5 L15.5 15 H8.5 L6.5 17.5 A2.5 2.5 0 0 1 3 16 A5 5 0 0 1 7 8 Z M8 10.5 V13.5 M6.5 12 H9.5 M15.5 11 L15.51 11 M17.5 13 L17.51 13",
        // a mouse
        "input": "M8 3 H16 A3 3 0 0 1 19 6 V18 A3 3 0 0 1 16 21 H8 A3 3 0 0 1 5 18 V6 A3 3 0 0 1 8 3 Z M12 3 V10",
        // a server
        "host": "M4 4 H20 V10 H4 Z M4 14 H20 V20 H4 Z M7.5 7 L7.51 7 M7.5 17 L7.51 17",
        // sliders
        "general": "M4 7 H20 M4 17 H20 M9 4 V10 M15 14 V20",
        // a window
        "ui": "M3 4 H21 V20 H3 Z M3 9 H21 M6 6.5 L6.51 6.5",
        // bars
        "performance": "M5 20 V13 M12 20 V5 M19 20 V10",
        // code brackets
        "advanced": "M8 7 L3 12 L8 17 M16 7 L21 12 L16 17 M14 4 L10 20",
        // a globe
        "misc": "M12 3 A9 9 0 1 0 12 21 A9 9 0 1 0 12 3 Z M3 12 H21 M12 3 C8 8 8 16 12 21 M12 3 C16 8 16 16 12 21",
        // an arrow up
        "top": "M12 19 V5 M6 11 L12 5 L18 11",
        // a cross
        "close": "M6 6 L18 18 M18 6 L6 18",
        // a tick, for what is in use
        "check": "M5 12.5 L10 17.5 L19 7",
        // a figure with open arms (native, lane a11y-tab: Accessibility)
        "accessibility": "M10.5 4.5 A1.5 1.5 0 1 0 13.5 4.5 A1.5 1.5 0 1 0 10.5 4.5 M5 8.5 H19 M12 8.5 V14 M12 14 L8.5 21 M12 14 L15.5 21"
    })

    Shape {
        width: 24
        height: 24
        anchors.centerIn: parent
        scale: icon.size / 24
        antialiasing: true
        ShapePath {
            strokeColor: icon.color
            strokeWidth: 2
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg {
                path: icon.paths[icon.name] || ""
            }
        }
    }
}
