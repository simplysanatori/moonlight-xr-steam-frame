// Moonlight XR Native launcher (lane wave2-bind-editor): the Controller bindings page, reached from the
// Settings page. One row per control of the Frame's two controllers for pointer mode and for gamepad mode,
// for every app or for one app of a PC's app list ([app "Name"] sections). A row opens an editor under it
// (what the control does: a key, a mouse button, a wheel notch, a pad button, a stick use, one of our own
// commands, or nothing). The file is ~/.config/moonlight-xr/bindings.ini; reading it, checking an edit
// and finding the lines to change are all done by the VR client's own binding code (xr/core/xr_bind.c),
// compiled into the launcher (launcher/src/mlxrbindings.h). New file, copied into app/gui by
// launcher/build.sh. GPLv3.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import QtQuick.Controls.Material

import ComputerManager 1.0
import ComputerModel 1.0
import SdlGamepadKeyNavigation 1.0
import MlxrBindings 1.0

Flickable {
    id: bindPage
    objectName: qsTr("Controller bindings")

    readonly property color accentColor: "#80CBC4"
    readonly property color summaryColor: "#B3FFFFFF"
    readonly property color reasonColor: "#FFB74D"
    readonly property color errorColor: "#EF9A9A"
    readonly property color panelColor: "#303030"

    // ---- what the page shows ----
    // 0 pointer mode, 1 gamepad mode (the core's XR_BIND_MODE_)
    property int mode: 0
    // false: the [pointer] and [gamepad] sections, every app. true: one app's lines over them
    property bool perApp: false
    property string chosenApp: ""
    readonly property string scopeApp: perApp ? chosenApp : ""
    // the control or chord whose editor is open
    property string openSource: ""
    // a chord typed in that has no line in the file yet
    property string pendingChord: ""
    property string chordError: ""
    property string pageError: ""
    // app names picked by hand that the file does not have yet
    property var extraApps: []
    // counts up whenever the file is read or written, so the rows read it again
    property int rev: MlxrBindings.revision
    property int pcTick: 0
    property int appTick: 0
    property int pcIndex: 0
    property var appModelObj: null

    readonly property var allSources: MlxrBindings.sourceNames()
    readonly property var pcNames: { pcTick; return MlxrBindings.modelNames(pcModel) }
    readonly property var pcApps: { appTick; return appModelObj ? MlxrBindings.modelNames(appModelObj) : [] }
    // the PC's apps, then the file's other [app] sections, then names typed in
    readonly property var appChoices: {
        var out = [], seen = {}
        var lists = [pcApps, MlxrBindings.fileApps, extraApps]
        for (var l = 0; l < lists.length; l++) {
            for (var i = 0; i < lists[l].length; i++) {
                var k = String(lists[l][i]).toLowerCase()
                if (!seen[k]) {
                    seen[k] = true
                    out.push(lists[l][i])
                }
            }
        }
        return out
    }
    readonly property var chordList: {
        rev
        var l = MlxrBindings.chords(scopeApp, mode)
        if (pendingChord !== "") {
            var have = false
            for (var i = 0; i < l.length; i++) {
                if (l[i].expr === pendingChord) have = true
            }
            if (!have) l.push({ expr: pendingChord })
        }
        return l
    }

    boundsBehavior: Flickable.OvershootBounds
    contentWidth: width
    contentHeight: bindColumn.height + 40

    ScrollBar.vertical: ScrollBar {
        anchors {
            left: parent.right
            leftMargin: -10
        }
    }

    // ---- keep the focused item on screen (as the Settings page does) ----
    function isChildOfFlickable(item) {
        while (item) {
            if (item.parent === contentItem) {
                return true
            }
            item = item.parent
        }
        return false
    }

    NumberAnimation on contentY {
        id: autoScrollAnimation
        duration: 100
    }

    Window.onActiveFocusItemChanged: {
        var item = Window.activeFocusItem
        if (item && isChildOfFlickable(item)) {
            var pos = item.mapToItem(contentItem, 0, 0)
            var scrollMargin = height > 100 ? 50 : 0
            if (pos.y - scrollMargin < contentY) {
                autoScrollAnimation.from = contentY
                autoScrollAnimation.to = Math.max(pos.y - scrollMargin, 0)
                autoScrollAnimation.start()
            }
            else if (pos.y + item.height + scrollMargin > contentY + height) {
                autoScrollAnimation.from = contentY
                autoScrollAnimation.to = Math.min(pos.y + item.height + scrollMargin - height, contentHeight - height)
                autoScrollAnimation.start()
            }
        }
    }

    StackView.onActivated: {
        SdlGamepadKeyNavigation.setUiNavMode(true)
        MlxrBindings.reload()
    }

    StackView.onDeactivating: SdlGamepadKeyNavigation.setUiNavMode(false)

    // ---- the PC list and the chosen PC's app list ----
    ComputerModel {
        id: pcModel
        Component.onCompleted: {
            initialize(ComputerManager)
            bindPage.pcTick++
        }
    }

    Connections {
        target: pcModel
        ignoreUnknownSignals: true
        function onRowsInserted() { bindPage.pcTick++ }
        function onRowsRemoved() { bindPage.pcTick++ }
        function onModelReset() { bindPage.pcTick++ }
        function onDataChanged() { bindPage.pcTick++ }
    }

    Connections {
        target: bindPage.appModelObj
        ignoreUnknownSignals: true
        function onRowsInserted() { bindPage.appTick++ }
        function onRowsRemoved() { bindPage.appTick++ }
        function onModelReset() { bindPage.appTick++ }
        function onLayoutChanged() { bindPage.appTick++ }
        function onDataChanged() { bindPage.appTick++ }
    }

    function rebuildApps() {
        if (appModelObj) {
            appModelObj.destroy()
            appModelObj = null
        }
        if (pcIndex >= 0 && pcIndex < pcNames.length) {
            var m = Qt.createQmlObject('import AppModel 1.0; AppModel {}', bindPage, '')
            m.initialize(ComputerManager, pcIndex, true)
            appModelObj = m
        }
        appTick++
    }

    onPcIndexChanged: rebuildApps()
    // the PC list is read when its model has started; the first PC's apps follow
    onPcNamesChanged: {
        if (!appModelObj && pcNames.length > 0) rebuildApps()
    }

    Component.onCompleted: {
        MlxrBindings.reload()
        rebuildApps()
    }

    Timer {
        interval: 400
        running: true
        onTriggered: console.info("MLXR_BINDINGS_UI: errors=" + MlxrBindings.errorCount + " strip=" + errorStrip.shown +
                                  " pcs=" + bindPage.pcNames.join(","))
    }

    // (test log) the apps of the PC the page is on, after the test hook has picked one
    Timer {
        interval: 2000
        running: true
        onTriggered: console.info("MLXR_BINDINGS_UI: apps pc=" + (bindPage.pcIndex < bindPage.pcNames.length ? bindPage.pcNames[bindPage.pcIndex] : "") +
                                  " list=" + bindPage.pcApps.join(","))
    }

    // ---- words ----
    function controlLabel(name) {
        var n = name.substring(name.indexOf(".") + 1)
        var map = {
            "trigger": qsTr("Trigger"), "grip": qsTr("Grip"), "bumper": qsTr("Bumper"),
            "stick": qsTr("Stick (as a whole)"), "stick_up": qsTr("Stick up"), "stick_down": qsTr("Stick down"),
            "stick_left": qsTr("Stick left"), "stick_right": qsTr("Stick right"), "stick_click": qsTr("Stick click"),
            "view": qsTr("View button"), "menu": qsTr("Menu button"),
            "dpad_up": qsTr("D-pad up"), "dpad_down": qsTr("D-pad down"),
            "dpad_left": qsTr("D-pad left"), "dpad_right": qsTr("D-pad right"),
            "a": qsTr("A button"), "b": qsTr("B button"), "x": qsTr("X button"), "y": qsTr("Y button"),
            "trigger_touch": qsTr("Trigger touch"), "stick_touch": qsTr("Stick touch"),
            "a_touch": qsTr("A touch"), "b_touch": qsTr("B touch"), "x_touch": qsTr("X touch"), "y_touch": qsTr("Y touch")
        }
        return map[n] !== undefined ? map[n] : n
    }

    function chordLabel(expr) {
        var parts = expr.split("+"), out = []
        for (var i = 0; i < parts.length; i++) {
            var hand = parts[i].substring(0, parts[i].indexOf("."))
            out.push((hand === "left" ? qsTr("Left") : qsTr("Right")) + " " + controlLabel(parts[i]).toLowerCase())
        }
        return out.join(" + ")
    }

    function modsText(mods) {
        return ((mods & 2) ? "Ctrl+" : "") + ((mods & 1) ? "Shift+" : "") + ((mods & 4) ? "Alt+" : "") + ((mods & 8) ? "Win+" : "")
    }

    function kindLabel(kind) {
        switch (kind) {
        case "none": return qsTr("Nothing")
        case "key": return qsTr("Key")
        case "mouse": return qsTr("Mouse button")
        case "wheel": return qsTr("Wheel")
        case "pad": return qsTr("Pad button")
        case "stick": return qsTr("Stick use")
        case "app": return qsTr("App command")
        }
        return kind
    }

    function valueLabel(kind, value) {
        var maps = {
            "mouse": { "left": qsTr("Left click"), "right": qsTr("Right click"), "middle": qsTr("Middle click"),
                       "x1": qsTr("Back (X1)"), "x2": qsTr("Forward (X2)") },
            "wheel": { "up": qsTr("Up"), "down": qsTr("Down"), "left": qsTr("Left"), "right": qsTr("Right") },
            "pad": { "lt": "LT", "rt": "RT", "lb": "LB", "rb": "RB", "ls": qsTr("LS click"), "rs": qsTr("RS click"),
                     "start": qsTr("Start"), "back": qsTr("Back"), "guide": qsTr("Guide"),
                     "dpad_up": qsTr("D-pad up"), "dpad_down": qsTr("D-pad down"),
                     "dpad_left": qsTr("D-pad left"), "dpad_right": qsTr("D-pad right"),
                     "a": "A", "b": "B", "x": "X", "y": "Y" },
            "stick": { "scroll": qsTr("Scroll"), "mouse": qsTr("Moves the mouse"), "wasd": qsTr("W A S D keys"),
                       "arrows": qsTr("Arrow keys"), "dpad": qsTr("D-pad"),
                       "pad_left": qsTr("Left stick of the pad"), "pad_right": qsTr("Right stick of the pad") },
            "app": { "keyboard": qsTr("Show or hide the keyboard"), "ray": qsTr("Step the ray (off, dot, ray)"),
                     "recentre": qsTr("Recentre the screen"), "cog": qsTr("Open or close the cog panel"),
                     "3d": qsTr("3D on or off"), "gamepad": qsTr("Switch pointer and gamepad mode"),
                     "bringhere": qsTr("Bring the screen here"),
                     "host": qsTr("Open or close the Host sheet"),
                     "zoomin": qsTr("Zoom in on the pointer"), "zoomout": qsTr("Zoom out"),
                     "zoomreset": qsTr("Zoom back to the whole picture") }
        }
        var m = maps[kind]
        return m && m[value] !== undefined ? m[value] : value
    }

    function actionLabel(text) {
        if (!text) return ""
        var d = MlxrBindings.describe(text)
        if (!d.ok) return text
        switch (d.kind) {
        case "none": return qsTr("Nothing")
        case "key": return qsTr("Key: %1").arg(modsText(d.mods) + d.value)
        case "mouse": return qsTr("Mouse: %1").arg(valueLabel("mouse", d.value))
        case "wheel": return qsTr("Wheel: %1").arg(valueLabel("wheel", d.value))
        case "pad": return qsTr("Pad: %1").arg(valueLabel("pad", d.value))
        case "stick": return qsTr("Stick: %1").arg(valueLabel("stick", d.value))
        case "app": return valueLabel("app", d.value)
        }
        return text
    }

    function fromLabel(info) {
        if (!info || !info.ok) return ""
        if (info.from === 2) return qsTr("set for this app")
        if (info.from === 1) return perApp ? qsTr("from Every app") : qsTr("set in your file")
        return qsTr("default")
    }

    function handSources(hand, touch) {
        var out = []
        for (var i = 0; i < allSources.length; i++) {
            var n = allSources[i]
            if (n.indexOf(hand + ".") === 0 && (n.indexOf("_touch") > 0) === touch) out.push(n)
        }
        return out
    }

    function isFileApp(name) {
        var l = MlxrBindings.fileApps, k = String(name).toLowerCase()
        for (var i = 0; i < l.length; i++) {
            if (String(l[i]).toLowerCase() === k) return true
        }
        return false
    }

    function chooseApp(name) {
        chosenApp = name
        openSource = ""
        pendingChord = ""
    }

    // ---- row types ----
    component Cat: Label {
        Layout.fillWidth: true
        Layout.topMargin: 22
        Layout.leftMargin: 16
        Layout.bottomMargin: 2
        font.pointSize: 11
        font.bold: true
        color: bindPage.accentColor
    }

    component Note: Label {
        Layout.fillWidth: true
        Layout.leftMargin: 16
        Layout.rightMargin: 16
        font.pointSize: 10
        color: bindPage.summaryColor
        wrapMode: Text.Wrap
    }

    // A chooser button: a kind, a key, a value
    component Chip: Button {
        property bool on: false
        font.pointSize: 10
        implicitHeight: 38
        implicitWidth: Math.max(48, implicitContentWidth + 24)
        highlighted: on
    }

    // What one control does: the kind of action, then its value (the key and its modifiers for a key)
    component BindEditor: Rectangle {
        id: ed
        property string expr
        property bool whole: false
        property string kind: "none"
        property int mods: 0
        property string keyName: ""
        property string value: ""
        property string errorText: ""
        readonly property var info: { bindPage.rev; return MlxrBindings.binding(bindPage.scopeApp, bindPage.mode, ed.expr) }
        readonly property var kinds: whole ? ["none", "stick"]
                                           : (bindPage.mode === 1 ? ["none", "key", "mouse", "wheel", "pad", "app"]
                                                                  : ["none", "key", "mouse", "wheel", "app"])
        readonly property var options: {
            var all = MlxrBindings.choices(kind), out = []
            for (var i = 0; i < all.length; i++) {
                // the pad's own sticks are for gamepad mode only
                if (kind === "stick" && bindPage.mode === 0 && all[i].indexOf("pad_") === 0) continue
                out.push(all[i])
            }
            return out
        }

        color: bindPage.panelColor
        radius: 4
        implicitHeight: edColumn.height + 24

        function load() {
            var d = MlxrBindings.describe(info.action)
            kind = d.ok ? d.kind : "none"
            mods = d.ok ? d.mods : 0
            keyName = d.ok && d.kind === "key" ? d.key : ""
            value = d.ok ? d.value : ""
            actionField.text = info.action
        }

        function composeKey() {
            var s = ""
            if (mods & 2) s += "ctrl+"
            if (mods & 1) s += "shift+"
            if (mods & 4) s += "alt+"
            if (mods & 8) s += "win+"
            return "key:" + s + keyName
        }

        function apply(text) {
            errorText = MlxrBindings.setBinding(bindPage.scopeApp, bindPage.mode, expr, text)
            if (errorText === "") load()
        }

        Component.onCompleted: load()

        Column {
            id: edColumn
            x: 12
            y: 12
            width: parent.width - 24
            spacing: 10

            Label {
                width: parent.width
                text: qsTr("What it does. The default is: %1").arg(bindPage.actionLabel(ed.info.defaultAction))
                font.pointSize: 10
                color: bindPage.summaryColor
                wrapMode: Text.Wrap
            }

            Flow {
                width: parent.width
                spacing: 6
                Repeater {
                    model: ed.kinds
                    delegate: Chip {
                        objectName: "kind_" + modelData
                        text: bindPage.kindLabel(modelData)
                        on: ed.kind === modelData
                        onClicked: {
                            ed.errorText = ""
                            if (modelData === "none") ed.apply("none")
                            else ed.kind = modelData
                        }
                    }
                }
            }

            // the values of a kind that is not a key
            Flow {
                width: parent.width
                spacing: 6
                visible: ed.kind !== "key" && ed.kind !== "none"
                Repeater {
                    model: ed.kind === "key" || ed.kind === "none" ? [] : ed.options
                    delegate: Chip {
                        objectName: "opt_" + ed.kind + "_" + modelData
                        text: bindPage.valueLabel(ed.kind, modelData)
                        on: ed.value === modelData && MlxrBindings.describe(ed.info.action).kind === ed.kind
                        onClicked: ed.apply(ed.kind + ":" + modelData)
                    }
                }
            }

            // a key: modifiers, then the key (picking the key writes it)
            Loader {
                width: parent.width
                active: ed.kind === "key"
                visible: active
                sourceComponent: Component {
                    Column {
                        width: parent ? parent.width : 400
                        spacing: 8
                        Flow {
                            width: parent.width
                            spacing: 6
                            Repeater {
                                model: [ { n: "ctrl", b: 2, t: "Ctrl" }, { n: "shift", b: 1, t: "Shift" },
                                         { n: "alt", b: 4, t: "Alt" }, { n: "win", b: 8, t: "Win" } ]
                                delegate: Chip {
                                    objectName: "mod_" + modelData.n
                                    text: modelData.t
                                    on: (ed.mods & modelData.b) !== 0
                                    onClicked: {
                                        ed.mods = ed.mods ^ modelData.b
                                        if (ed.keyName !== "") ed.apply(ed.composeKey())
                                    }
                                }
                            }
                        }
                        Repeater {
                            model: MlxrBindings.keyGroups()
                            delegate: Column {
                                width: parent.width
                                spacing: 4
                                Label {
                                    text: modelData.title
                                    font.pointSize: 9
                                    color: bindPage.summaryColor
                                }
                                Flow {
                                    width: parent.width
                                    spacing: 4
                                    Repeater {
                                        model: modelData.keys
                                        delegate: Chip {
                                            objectName: "key_" + modelData
                                            text: modelData
                                            on: ed.keyName === modelData
                                            onClicked: {
                                                ed.keyName = modelData
                                                ed.apply(ed.composeKey())
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            RowLayout {
                width: parent.width
                spacing: 8
                TextField {
                    id: actionField
                    objectName: "actionField"
                    Layout.fillWidth: true
                    placeholderText: qsTr("or type it as the file does: key:ctrl+shift+esc")
                    font.pointSize: 10
                    onAccepted: ed.apply(text)
                }
                Chip {
                    objectName: "resetRow"
                    text: qsTr("Reset row")
                    enabled: ed.info.explicit === true
                    onClicked: {
                        ed.errorText = MlxrBindings.resetBinding(bindPage.scopeApp, bindPage.mode, ed.expr)
                        if (ed.errorText === "") ed.load()
                    }
                }
                Chip {
                    objectName: "doneRow"
                    text: qsTr("Done")
                    onClicked: bindPage.openSource = ""
                }
            }

            Label {
                width: parent.width
                visible: text !== ""
                text: ed.errorText
                font.pointSize: 10
                color: bindPage.errorColor
                wrapMode: Text.Wrap
            }
        }
    }

    // One control (or chord): its name, what it does now and where that comes from; a click opens the editor
    component BindRow: ColumnLayout {
        id: bRow
        property string expr
        property string title
        property bool whole: false
        property string reason: ""
        readonly property var info: { bindPage.rev; return MlxrBindings.binding(bindPage.scopeApp, bindPage.mode, bRow.expr) }
        readonly property bool expanded: bindPage.openSource === bRow.expr && bRow.reason === ""
        Layout.fillWidth: true
        spacing: 0

        ItemDelegate {
            id: head
            objectName: "row_" + bRow.expr
            Layout.fillWidth: true
            enabled: bRow.reason === ""
            highlighted: visualFocus
            focusPolicy: Qt.StrongFocus
            activeFocusOnTab: true
            topPadding: 8
            bottomPadding: 8
            leftPadding: 16
            rightPadding: 16
            Keys.onReturnPressed: clicked()
            Keys.onEnterPressed: clicked()
            onClicked: bindPage.openSource = bRow.expanded ? "" : bRow.expr

            contentItem: RowLayout {
                spacing: 12
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Label {
                        Layout.fillWidth: true
                        text: bRow.title
                        font.pointSize: 12
                        wrapMode: Text.Wrap
                        opacity: head.enabled ? 1.0 : 0.45
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: bRow.reason !== ""
                        text: qsTr("Not available: %1").arg(bRow.reason)
                        font.pointSize: 9
                        color: bindPage.reasonColor
                        wrapMode: Text.Wrap
                    }
                }
                ColumnLayout {
                    Layout.alignment: Qt.AlignVCenter | Qt.AlignRight
                    visible: bRow.reason === ""
                    spacing: 0
                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: bindPage.actionLabel(bRow.info.action)
                        font.pointSize: 11
                        font.bold: true
                        color: bindPage.accentColor
                    }
                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: bindPage.fromLabel(bRow.info)
                        font.pointSize: 9
                        color: bRow.info.explicit ? bindPage.reasonColor : bindPage.summaryColor
                    }
                }
            }
        }

        Loader {
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            Layout.bottomMargin: 6
            active: bRow.expanded
            visible: active
            sourceComponent: Component {
                BindEditor {
                    expr: bRow.expr
                    whole: bRow.whole
                }
            }
        }
    }

    // One controller's rows, then its touch sensors
    component HandColumn: ColumnLayout {
        id: hc
        property string hand
        property string heading
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignTop
        spacing: 0

        Cat { text: hc.heading }
        Repeater {
            model: bindPage.handSources(hc.hand, false)
            delegate: BindRow {
                expr: modelData
                title: bindPage.controlLabel(modelData)
                whole: modelData === "left.stick" || modelData === "right.stick"
            }
        }
        Cat { text: qsTr("%1 touch sensors").arg(hc.hand === "left" ? qsTr("Left") : qsTr("Right")) }
        Repeater {
            model: bindPage.handSources(hc.hand, true)
            delegate: BindRow {
                expr: modelData
                title: bindPage.controlLabel(modelData)
                reason: qsTr("the headset does not read the touch sensors yet")
            }
        }
    }

    // ---- the page ----
    ColumnLayout {
        id: bindColumn
        width: Math.min(bindPage.width - 20, 1100)
        x: Math.round((bindPage.width - width) / 2)
        spacing: 0

        Cat { text: qsTr("Controller bindings") }
        Note {
            text: qsTr("What each button on the Frame's two controllers does, in pointer mode and in gamepad mode, for every app or for one app. " +
                       "Changes are saved to %1 as you make them and take effect when the next stream starts. " +
                       "Your comments and the rest of the file are kept; only the lines you change are rewritten.").arg(MlxrBindings.path)
        }

        // the core's complaints about the file
        Rectangle {
            id: errorStrip
            objectName: "errorStrip"
            readonly property bool shown: MlxrBindings.errorCount > 0 || MlxrBindings.fileError !== ""
            Layout.fillWidth: true
            Layout.margins: 16
            visible: shown
            color: "#4A2323"
            radius: 4
            border.color: bindPage.errorColor
            implicitHeight: errColumn.height + 20

            Column {
                id: errColumn
                x: 12
                y: 10
                width: parent.width - 24
                spacing: 4
                Label {
                    width: parent.width
                    text: MlxrBindings.fileError !== ""
                          ? qsTr("bindings.ini cannot be used: %1").arg(MlxrBindings.fileError)
                          : qsTr("%n problem(s) in bindings.ini. These lines are left out until you fix them in the file; the rest of the file works.",
                                 "", MlxrBindings.errorCount)
                    font.pointSize: 11
                    font.bold: true
                    color: bindPage.errorColor
                    wrapMode: Text.Wrap
                }
                Repeater {
                    model: MlxrBindings.fileError !== "" ? [] : MlxrBindings.errors
                    delegate: Label {
                        width: errColumn.width
                        text: modelData
                        font.pointSize: 10
                        font.family: "monospace"
                        wrapMode: Text.Wrap
                    }
                }
                Label {
                    width: parent.width
                    visible: MlxrBindings.fileError === "" && MlxrBindings.errorCount > MlxrBindings.errors.length
                    text: qsTr("... and %1 more.").arg(MlxrBindings.errorCount - MlxrBindings.errors.length)
                    font.pointSize: 10
                }
            }
        }

        Label {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            visible: text !== ""
            text: bindPage.pageError
            color: bindPage.errorColor
            font.pointSize: 10
            wrapMode: Text.Wrap
        }

        // ---- which bindings: every app, or one app of a PC ----
        RowLayout {
            Layout.leftMargin: 16
            Layout.topMargin: 12
            spacing: 8
            Chip {
                objectName: "tabGeneral"
                text: qsTr("Every app")
                on: !bindPage.perApp
                onClicked: {
                    bindPage.perApp = false
                    bindPage.openSource = ""
                    bindPage.pendingChord = ""
                }
            }
            Chip {
                objectName: "tabApp"
                text: qsTr("One app")
                on: bindPage.perApp
                onClicked: {
                    bindPage.perApp = true
                    bindPage.openSource = ""
                    bindPage.pendingChord = ""
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            visible: bindPage.perApp
            spacing: 6

            Note {
                Layout.topMargin: 8
                text: qsTr("An app's lines go over the Every app lines, for that app only. Pick the PC to list its apps. " +
                           "Apps already in your file are listed too; ● marks the ones that have lines of their own.")
            }
            Item {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                implicitHeight: pcFlow.height
                Flow {
                    id: pcFlow
                    width: parent.width
                    spacing: 6
                    Repeater {
                        model: bindPage.pcNames
                        delegate: Chip {
                            objectName: "pc_" + modelData
                            text: modelData
                            on: bindPage.pcIndex === index
                            onClicked: bindPage.pcIndex = index
                        }
                    }
                }
            }
            Item {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.preferredHeight: Math.min(appFlow.height, 190)
                Flickable {
                    anchors.fill: parent
                    clip: true
                    contentWidth: width
                    contentHeight: appFlow.height
                    boundsBehavior: Flickable.StopAtBounds
                    Flow {
                        id: appFlow
                        width: parent.width
                        spacing: 6
                        Repeater {
                            model: bindPage.appChoices
                            delegate: Chip {
                                objectName: "app_" + modelData
                                text: modelData + (bindPage.isFileApp(modelData) ? "  ●" : "")
                                on: bindPage.chosenApp === modelData
                                onClicked: bindPage.chooseApp(modelData)
                            }
                        }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                spacing: 8
                TextField {
                    id: appField
                    objectName: "appField"
                    Layout.fillWidth: true
                    placeholderText: qsTr("Another app, by the name the app list shows")
                    font.pointSize: 10
                    onAccepted: addAppBtn.clicked()
                }
                Chip {
                    id: addAppBtn
                    objectName: "addAppBtn"
                    text: qsTr("Use this name")
                    enabled: appField.text.trim() !== ""
                    onClicked: {
                        var n = appField.text.trim()
                        if (n === "") return
                        if (bindPage.appChoices.indexOf(n) < 0) bindPage.extraApps = bindPage.extraApps.concat([n])
                        bindPage.chooseApp(n)
                        appField.text = ""
                    }
                }
            }
            Label {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                visible: bindPage.chosenApp === ""
                text: qsTr("Pick an app to see and change its bindings.")
                font.pointSize: 11
                color: bindPage.reasonColor
            }
            Label {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                visible: bindPage.chosenApp !== ""
                text: qsTr("Bindings for %1").arg(bindPage.chosenApp)
                font.pointSize: 13
                font.bold: true
                color: bindPage.accentColor
                wrapMode: Text.Wrap
            }
        }

        RowLayout {
            Layout.leftMargin: 16
            Layout.topMargin: 14
            spacing: 8
            visible: !bindPage.perApp || bindPage.chosenApp !== ""
            Chip {
                objectName: "modePointer"
                text: qsTr("Pointer mode")
                on: bindPage.mode === 0
                onClicked: {
                    bindPage.mode = 0
                    bindPage.openSource = ""
                    bindPage.pendingChord = ""
                }
            }
            Chip {
                objectName: "modeGamepad"
                text: qsTr("Gamepad mode")
                on: bindPage.mode === 1
                onClicked: {
                    bindPage.mode = 1
                    bindPage.openSource = ""
                    bindPage.pendingChord = ""
                }
            }
        }
        Note {
            Layout.topMargin: 6
            visible: !bindPage.perApp || bindPage.chosenApp !== ""
            text: bindPage.mode === 0
                  ? qsTr("Pointer mode: the ray points at the PC's screen and the buttons click, scroll and type.")
                  : qsTr("Gamepad mode: the two controllers act as one Xbox pad on the PC.")
        }

        GridLayout {
            Layout.fillWidth: true
            visible: !bindPage.perApp || bindPage.chosenApp !== ""
            columns: bindColumn.width >= 900 ? 2 : 1
            columnSpacing: 24
            rowSpacing: 0
            HandColumn { hand: "left"; heading: qsTr("Left controller") }
            HandColumn { hand: "right"; heading: qsTr("Right controller") }
        }

        // ---- chords ----
        ColumnLayout {
            Layout.fillWidth: true
            visible: !bindPage.perApp || bindPage.chosenApp !== ""
            spacing: 0

            Cat { text: qsTr("Chords") }
            Note {
                text: qsTr("Two to four controls held together. A chord replaces what its controls do on their own while all of them are down.")
            }
            Repeater {
                model: bindPage.chordList
                delegate: BindRow {
                    expr: modelData.expr
                    title: bindPage.chordLabel(modelData.expr)
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.topMargin: 8
                spacing: 8
                TextField {
                    id: chordField
                    objectName: "chordField"
                    Layout.fillWidth: true
                    placeholderText: qsTr("A new chord, like left.view+right.menu")
                    font.pointSize: 10
                    onAccepted: addChordBtn.clicked()
                }
                Chip {
                    id: addChordBtn
                    objectName: "addChordBtn"
                    text: qsTr("Add chord")
                    enabled: chordField.text.trim() !== ""
                    onClicked: {
                        var r = MlxrBindings.normalizeSources(chordField.text)
                        if (!r.ok) {
                            bindPage.chordError = r.error
                        }
                        else if (r.expr.indexOf("+") < 0) {
                            bindPage.chordError = qsTr("A chord is two to four controls joined with +.")
                        }
                        else {
                            bindPage.chordError = ""
                            bindPage.pendingChord = r.expr
                            bindPage.openSource = r.expr
                            chordField.text = ""
                        }
                    }
                }
            }
            Note {
                visible: bindPage.chordError !== ""
                text: bindPage.chordError
                color: bindPage.errorColor
            }
        }

        // ---- reset ----
        RowLayout {
            Layout.leftMargin: 16
            Layout.topMargin: 22
            visible: !bindPage.perApp || bindPage.chosenApp !== ""
            Chip {
                id: resetAllBtn
                objectName: "resetAllBtn"
                property bool armed: false
                text: armed ? qsTr("Click again to take every line of this list out of the file")
                            : (bindPage.perApp ? qsTr("Reset all for this app") : qsTr("Reset all (every app)"))
                on: armed
                onClicked: {
                    if (!armed) {
                        armed = true
                        armTimer.restart()
                        return
                    }
                    armed = false
                    armTimer.stop()
                    bindPage.pageError = MlxrBindings.resetAll(bindPage.scopeApp)
                    bindPage.openSource = ""
                    bindPage.pendingChord = ""
                }
                Timer {
                    id: armTimer
                    interval: 5000
                    onTriggered: resetAllBtn.armed = false
                }
            }
        }

        // ---- what cannot be bound ----
        Cat { text: qsTr("Reserved chords (cannot be bound)") }
        Repeater {
            model: MlxrBindings.reservedChords()
            delegate: Note {
                objectName: "reserved_" + index
                text: bindPage.chordLabel(modelData.chord) + " — " +
                      (modelData.kind === "switch" ? qsTr("switches between pointer mode and gamepad mode")
                                                   : qsTr("disconnects gamepad mode (Start, Back, LB and RB held together)"))
            }
        }
        Note {
            Layout.topMargin: 6
            text: qsTr("In pointer mode a chord with a grip in it is refused too, because the grips grab the screen, the keyboard's bar and the stats window there; " +
                       "a grip can still be bound on its own. The system button belongs to SteamVR.")
        }

        Note {
            Layout.topMargin: 24
            Layout.bottomMargin: 20
            font.pointSize: 9
            text: qsTr("The file is read by the same code the headset uses, so a line it refuses here is refused there. Edit it by hand any time; this page reads it again each time it opens.")
        }
    }
}
