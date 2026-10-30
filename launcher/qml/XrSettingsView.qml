// Moonlight XR Native launcher (lane artemis-settings): the Settings page in hand-off mode, laid out
// and worded after Artemis (ClassicOldSong/moonlight-android, res/xml/preferences.xml): its
// categories in its order, a title and a summary per row, lists in a dialog. Only Artemis settings
// the Frame and the VR client honour are shown; a row that cannot work here is greyed with its
// reason. Where each one goes: docs/dev/reports/launcher.md ("Artemis settings"). New file, copied into
// app/gui by launcher/build.sh. GPLv3.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import QtQuick.Controls.Material

import StreamingPreferences 1.0
import SdlGamepadKeyNavigation 1.0
import SystemProperties 1.0
import MlxrPrefs 1.0

Flickable {
    id: settingsPage
    objectName: qsTr("Settings")

    signal languageChanged()

    readonly property color accentColor: "#80CBC4"
    readonly property color summaryColor: "#B3FFFFFF"
    readonly property color reasonColor: "#FFB74D"
    readonly property string apolloNote: qsTr("Requires Apollo as host streaming software.")

    boundsBehavior: Flickable.OvershootBounds
    contentWidth: width
    contentHeight: prefColumn.height + 40

    ScrollBar.vertical: ScrollBar {
        anchors {
            left: parent.right
            leftMargin: -10
        }
    }

    // ---- keep the focused row on screen (as the stock page does) ----
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
        if (SdlGamepadKeyNavigation.getConnectedGamepads() > 0) {
            networkRowItem.forceActiveFocus(Qt.TabFocus)
        }
    }

    function saveAll() {
        StreamingPreferences.save()
        MlxrPrefs.save()
    }

    StackView.onDeactivating: {
        SdlGamepadKeyNavigation.setUiNavMode(false)
        saveAll()
    }

    Component.onDestruction: saveAll()

    // ---- the stream's basics (stock keys) ----
    function resetBitrate() {
        StreamingPreferences.bitrateKbps = StreamingPreferences.getDefaultBitrate(StreamingPreferences.width,
                                                                                  StreamingPreferences.height,
                                                                                  StreamingPreferences.fps, false)
    }

    readonly property var resolutions: [
        { text: qsTr("360p"), w: 640, h: 360 },
        { text: qsTr("480p"), w: 854, h: 480 },
        { text: qsTr("720p"), w: 1280, h: 720 },
        { text: qsTr("1080p"), w: 1920, h: 1080 },
        { text: qsTr("1440p"), w: 2560, h: 1440 },
        { text: qsTr("4K"), w: 3840, h: 2160 }
    ]
    function resolutionIndex() {
        for (var i = 0; i < resolutions.length; i++) {
            if (resolutions[i].w === StreamingPreferences.width && resolutions[i].h === StreamingPreferences.height) {
                return i
            }
        }
        return -1
    }
    function resolutionText() {
        var i = resolutionIndex()
        return i >= 0 ? resolutions[i].text + " (" + resolutions[i].w + "x" + resolutions[i].h + ")"
                      : qsTr("Custom") + " (" + StreamingPreferences.width + "x" + StreamingPreferences.height + ")"
    }

    // One rate setting: Match headset (default; the VR client asks the host for the display's rate,
    // the one SteamVR runs this app at), a fixed rate (stock "fps"), or Artemis's custom rate
    // (Apollo). Native (lane refresh): 120 and 144 added, the Frame's faster display rates; a fixed
    // 72, 90, 120 or 144 is also set as SteamVR's per-app rate when the stream starts
    // (MlxrHandoff::setSteamVrRefreshRate). Never above 144.
    readonly property var fixedRates: [144, 120, 90, 72, 60, 30]
    readonly property var rateNames: [qsTr("Match headset"), qsTr("144 Hz"), qsTr("120 Hz"), qsTr("90 Hz"), qsTr("72 Hz"),
                                      qsTr("60 Hz"), qsTr("30 Hz"), qsTr("Custom...")]
    // Match headset: the bitrate default is the 90 Hz one, the rate install.sh gives the app in SteamVR
    readonly property int matchBitrateFps: 90
    function rateIndex() {
        if (MlxrPrefs.customRefreshRate !== "") return rateNames.length - 1
        if (MlxrPrefs.matchHeadsetFps) return 0
        var i = fixedRates.indexOf(StreamingPreferences.fps)
        return i >= 0 ? i + 1 : -1
    }
    function rateText() {
        var i = rateIndex()
        if (i === rateNames.length - 1) return qsTr("Custom (%1 Hz)").arg(MlxrPrefs.customRefreshRate)
        if (i === 0) return qsTr("Match headset")
        if (i > 0) return rateNames[i]
        return qsTr("%1 Hz (the headset caps it at %2)").arg(StreamingPreferences.fps).arg(MlxrPrefs.headsetMaxFps)
    }

    readonly property var pacingValues: ["latency", "balanced", "smoothness"]
    readonly property var pacingNames: [qsTr("Prefer lowest latency"), qsTr("Balanced"),
                                         qsTr("Prefer smoothest video (may significantly increase latency)")]

    // The PC link (the setting's key networkPath; the hand-off passes it to the VR client). The Steam
    // client's own terms: "Wi-Fi", "Steam Frame Wireless Adapter"; the Wi-Fi as the headset sees it now
    readonly property string wifiLabel: MlxrPrefs.wifiLabel()
    // The USB cable (a data cable from the headset's USB-C port to the PC) is rescanned every 10 s, so
    // the row says whether it is there now
    readonly property bool usbCable: MlxrPrefs.usbCable
    readonly property var networkValues: ["auto", "home", "adapter", "usb"]
    readonly property var networkNames: [qsTr("Automatic"), wifiLabel, qsTr("Steam Frame Wireless Adapter") + " (6 GHz)",
                                         qsTr("USB cable")]
    readonly property var networkSummaries: [qsTr("Uses whichever connection to your PC is faster when a stream starts."),
                                             qsTr("Streams over your Wi-Fi network."),
                                             qsTr("Streams over the Steam Frame Wireless Adapter plugged into your PC."),
                                             usbCable ? qsTr("Streams over the USB cable between the headset and your PC. Cable detected.")
                                                      : qsTr("Streams over a USB data cable between the headset and your PC. No cable detected yet; plug it in and it is noticed within ten seconds.")]
    function networkIndex() {
        return Math.max(0, networkValues.indexOf(MlxrPrefs.networkPath))
    }

    // Native (lane wave4b-abr): [stream] bitrate_wifi, bitrate_adapter, bitrate_wired, bitrate_usb of the VR client's
    // config.ini, kbps. 0 is "Same as above" (the Video bitrate). A running stream's bitrate cannot change, so the
    // client picks it at the start, for the connection it ended up on
    readonly property var pathBitrateSteps: [0, 10000, 20000, 30000, 40000, 50000, 60000, 80000, 100000, 120000, 150000,
                                             200000, 300000, 400000, 500000]
    function pathBitrateText(kbps) {
        return kbps <= 0 ? qsTr("Same as above") : (Math.round(kbps / 100) / 10) + " " + qsTr("Mbps")
    }
    function nextPathBitrate(current) {   // the next listed step above; past the last, or a value typed into config.ini, wraps to 0
        for (var i = 0; i < pathBitrateSteps.length; i++) {
            if (pathBitrateSteps[i] > current) return pathBitrateSteps[i]
        }
        return 0
    }
    readonly property string pathBitrateSummary: qsTr("The bitrate a stream starts with when it goes over this connection. Same as above uses the Video bitrate. A stream's bitrate cannot change once it runs, so this is chosen at the start, for the connection Network connection ends up using. Tap to step up.") + " " +
        qsTr("The Steam Frame Wireless Adapter can have worse latency than a good 5 GHz network. Automatic measures latency and drops on each connection before it chooses, so do not assume the adapter wins.")

    // The headset keyboard's letters ([vr] keyboard_keymap in the VR client's config.ini). The keys send the
    // US keyboard's positions, so the PC has to be on the same layout
    readonly property var keymapValues: ["qwerty", "azerty", "qwertz"]
    readonly property var keymapNames: [qsTr("QWERTY (US)"), qsTr("AZERTY (French)"), qsTr("QWERTZ (German)")]
    // Native (PLAN 4.3): [3d] stereo_source of the VR client's config.ini, where the 3D comes from
    readonly property var stereoSourceValues: ["depth", "sbs", "top-bottom"]
    readonly property var stereoSourceNames: [qsTr("Depth model"), qsTr("SBS from the PC"), qsTr("Top-bottom from the PC")]
    function stereoSourceIndex() {
        return Math.max(0, stereoSourceValues.indexOf(MlxrPrefs.stereoSource))
    }

    // [vr] notices of the VR client's config.ini
    readonly property var noticeValues: ["all", "important", "off"]
    readonly property var noticeNames: [qsTr("All"), qsTr("Important only"), qsTr("Off")]
    function noticeIndex() {
        return Math.max(0, noticeValues.indexOf(MlxrPrefs.notices))
    }

    // [vr] scroll_speed: the steps the row goes through, percent
    readonly property var scrollSpeedSteps: [50, 75, 100, 150, 200, 300]
    function nextScrollSpeed() {
        for (var i = 0; i < scrollSpeedSteps.length; i++) {
            if (scrollSpeedSteps[i] > MlxrPrefs.scrollSpeed) return scrollSpeedSteps[i]
        }
        return scrollSpeedSteps[0]
    }

    // Native (lane wave3-visual): the surroundings. [vr] background_dim (percent), [display] glow_feather and
    // [3d] eye_balance (percent) step through these; [vr] void_color is typed
    readonly property var backgroundDimSteps: [0, 25, 50, 75, 100]
    readonly property var glowFeatherValues: ["off", "soft", "wide"]
    readonly property var glowFeatherNames: [qsTr("Off"), qsTr("Soft"), qsTr("Wide")]
    readonly property var eyeBalanceSteps: [-100, -50, 0, 50, 100]
    // [3d] edge_sharpness: the steps the Edge row goes through (0 is the 3D's own soft edges)
    readonly property var edgeSteps: [0, 25, 50, 75, 100]
    function nextEdge() {
        for (var i = 0; i < edgeSteps.length; i++) {
            if (edgeSteps[i] > MlxrPrefs.edgeSharpness) return edgeSteps[i]
        }
        return edgeSteps[0]
    }

    // [3d] motion_reset: the steps the Motion row goes through (0 is off)
    readonly property var motionSteps: [0, 5, 10, 20, 30]
    function nextMotion() {
        for (var i = 0; i < motionSteps.length; i++) {
            if (motionSteps[i] > MlxrPrefs.motionReset) return motionSteps[i]
        }
        return motionSteps[0]
    }

    // [3d] depth_scale and depth_global: the steps their rows go through, percent (100 is as made)
    readonly property var depthSteps: [50, 75, 100, 125, 150, 200]
    function nextDepthStep(v) {
        for (var i = 0; i < depthSteps.length; i++) {
            if (depthSteps[i] > v) return depthSteps[i]
        }
        return depthSteps[0]
    }

    // [vr] controller_model: which controller the headset draws in each hand
    readonly property var modelValues: ["off", "frame", "generic", "gun", "custom"]
    readonly property var modelNames: [qsTr("Off"), qsTr("Steam Frame controller"), qsTr("Generic controller"), qsTr("Gun"), qsTr("Your own model")]
    function modelIndex() {
        return Math.max(0, modelValues.indexOf(MlxrPrefs.controllerModel))
    }

    // [stream] doff_disconnect (seconds) and idle_disconnect (minutes) of the VR client's config.ini: 0 is never
    readonly property var doffValues: [0, 15, 30, 60, 120, 300]
    readonly property var idleValues: [0, 10, 30, 60, 120]
    function limitText(v, unitOne, unitMany) {
        return v === 0 ? qsTr("Never") : (v + " " + (v === 1 ? unitOne : unitMany))
    }
    function nextLimit(values, current) {
        var i = values.indexOf(current)
        return values[(i + 1) % values.length]   // a value typed into config.ini that is not listed steps to Never
    }
    // Native (lane wave1-burst): [stream] rfi_idr_ms of the VR client's config.ini: 0 is no limit
    readonly property var idrCapValues: [0, 250, 500, 1000, 2000]
    function idrCapText(v) {
        return v === 0 ? qsTr("No limit") : (v + " ms")
    }
    // Native (lane wave2b-padmouse): [vr] pad_mouse_chord, pad_mouse_scroll and pad_arbitration of the VR client
    readonly property var padMouseChordValues: ["start_hold", "start_back", "off"]
    readonly property var padMouseChordNames: [qsTr("Hold Start"), qsTr("Hold Start and Back"), qsTr("Off")]
    readonly property var padMouseScrollValues: ["right", "left", "none"]
    readonly property var padMouseScrollNames: [qsTr("Right stick"), qsTr("Left stick"), qsTr("None")]
    readonly property var padArbitrationValues: ["pointer", "also_gamepad", "sleep"]
    readonly property var padArbitrationNames: [qsTr("Pointer"), qsTr("Also gamepad"), qsTr("Sleep")]
    // Native (lane wave3-cards): [vr] pad_card of the VR client, stepped by pressing its row
    readonly property var padCardValues: ["auto", "on", "off"]
    readonly property var padCardNames: [qsTr("Auto"), qsTr("On"), qsTr("Off")]
    function padCardIndex() {
        return Math.max(0, padCardValues.indexOf(MlxrPrefs.padCard))
    }

    readonly property var audioNames: [qsTr("Stereo"), qsTr("5.1 Surround Sound"), qsTr("7.1 Surround Sound")]

    // Artemis video_format; AV1 is listed greyed (the VR client has no AV1 decoder)
    readonly property var codecEntries: [
        { text: qsTr("Automatic (Recommended)"), val: StreamingPreferences.VCC_AUTO, reason: "" },
        { text: qsTr("Prefer AV1 (Experimental)"), val: StreamingPreferences.VCC_FORCE_AV1,
          reason: qsTr("the Frame decodes H.264 and HEVC only") },
        { text: qsTr("Prefer HEVC"), val: StreamingPreferences.VCC_FORCE_HEVC, reason: "" },
        { text: qsTr("Prefer H.264"), val: StreamingPreferences.VCC_FORCE_H264, reason: "" }
    ]
    function codecIndex() {
        var v = StreamingPreferences.videoCodecConfig
        if (v === StreamingPreferences.VCC_FORCE_H264) return 3
        if (v === StreamingPreferences.VCC_FORCE_HEVC || v === StreamingPreferences.VCC_FORCE_HEVC_HDR_DEPRECATED) return 2
        return 0 // auto; a saved AV1 choice streams as auto (HEVC, H.264 fallback)
    }

    readonly property var languages: [
        { text: qsTr("Automatic"), val: StreamingPreferences.LANG_AUTO },
        { text: "Deutsch", val: StreamingPreferences.LANG_DE },
        { text: "English", val: StreamingPreferences.LANG_EN },
        { text: "Français", val: StreamingPreferences.LANG_FR },
        { text: "简体中文", val: StreamingPreferences.LANG_ZH_CN },
        { text: "Norwegian Bokmål", val: StreamingPreferences.LANG_NB_NO },
        { text: "русский", val: StreamingPreferences.LANG_RU },
        { text: "Español", val: StreamingPreferences.LANG_ES },
        { text: "日本語", val: StreamingPreferences.LANG_JA },
        { text: "Tiếng Việt", val: StreamingPreferences.LANG_VI },
        { text: "ภาษาไทย", val: StreamingPreferences.LANG_TH },
        { text: "한국어", val: StreamingPreferences.LANG_KO },
        { text: "Magyar", val: StreamingPreferences.LANG_HU },
        { text: "Nederlands", val: StreamingPreferences.LANG_NL },
        { text: "Svenska", val: StreamingPreferences.LANG_SV },
        { text: "Türkçe", val: StreamingPreferences.LANG_TR },
        { text: "Українська", val: StreamingPreferences.LANG_UK },
        { text: "繁體中文", val: StreamingPreferences.LANG_ZH_TW },
        { text: "Português", val: StreamingPreferences.LANG_PT },
        { text: "Português do Brasil", val: StreamingPreferences.LANG_PT_BR },
        { text: "Ελληνικά", val: StreamingPreferences.LANG_EL },
        { text: "Italiano", val: StreamingPreferences.LANG_IT },
        { text: "हिन्दी, हिंदी", val: StreamingPreferences.LANG_HI },
        { text: "Język polski", val: StreamingPreferences.LANG_PL },
        { text: "Čeština", val: StreamingPreferences.LANG_CS },
        { text: "עִבְרִית", val: StreamingPreferences.LANG_HE },
        { text: "کرمانجیی خواروو", val: StreamingPreferences.LANG_CKB },
        { text: "Lietuvių kalba", val: StreamingPreferences.LANG_LT },
        { text: "Eesti", val: StreamingPreferences.LANG_ET }
    ]
    function languageIndex() {
        for (var i = 0; i < languages.length; i++) {
            if (languages[i].val === StreamingPreferences.language) return i
        }
        return 0
    }

    // ---- row types (Artemis's preference widgets) ----
    component PrefCategory: Label {
        Layout.fillWidth: true
        Layout.topMargin: 22
        Layout.leftMargin: 16
        Layout.bottomMargin: 2
        font.pointSize: 11
        font.bold: true
        color: settingsPage.accentColor
    }

    // A row: title, summary, an optional reason it is greyed, a control on the right
    component PrefRow: ItemDelegate {
        id: prefRow
        property string title
        property string summary
        property string reason   // non-empty: not available here, greyed, with this reason
        property string valueText
        default property alias trailing: trailingSlot.data
        property alias extra: extraSlot.data

        Layout.fillWidth: true
        enabled: reason === ""
        highlighted: visualFocus
        focusPolicy: Qt.StrongFocus
        activeFocusOnTab: true
        topPadding: 10
        bottomPadding: 10
        leftPadding: 16
        rightPadding: 16

        Keys.onReturnPressed: clicked()
        Keys.onEnterPressed: clicked()

        contentItem: RowLayout {
            spacing: 16
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3
                Label {
                    Layout.fillWidth: true
                    text: prefRow.title
                    font.pointSize: 13
                    wrapMode: Text.Wrap
                    opacity: prefRow.enabled ? 1.0 : 0.45
                }
                Label {
                    Layout.fillWidth: true
                    visible: text !== ""
                    text: prefRow.valueText
                    font.pointSize: 10
                    font.bold: true
                    color: settingsPage.accentColor
                    wrapMode: Text.Wrap
                    opacity: prefRow.enabled ? 1.0 : 0.45
                }
                Label {
                    Layout.fillWidth: true
                    visible: text !== ""
                    text: prefRow.summary
                    font.pointSize: 10
                    color: settingsPage.summaryColor
                    wrapMode: Text.Wrap
                    opacity: prefRow.enabled ? 1.0 : 0.45
                }
                Label {
                    Layout.fillWidth: true
                    visible: text !== ""
                    text: prefRow.reason !== "" ? qsTr("Not available: %1").arg(prefRow.reason) : ""
                    font.pointSize: 10
                    color: settingsPage.reasonColor
                    wrapMode: Text.Wrap
                }
                Item {
                    id: extraSlot
                    Layout.fillWidth: true
                    implicitHeight: childrenRect.height
                    visible: children.length > 0
                }
            }
            Item {
                id: trailingSlot
                implicitWidth: childrenRect.width
                implicitHeight: childrenRect.height
                Layout.alignment: Qt.AlignVCenter
            }
        }
    }

    component PrefSwitch: PrefRow {
        id: switchRow
        property bool isOn
        signal switched(bool on)
        onClicked: switched(!isOn)
        Switch {
            checked: switchRow.isOn && switchRow.enabled
            enabled: switchRow.enabled
            focusPolicy: Qt.NoFocus
            onClicked: switchRow.switched(checked)
        }
    }

    // Artemis's SeekBarPreference, inline
    component PrefSlider: PrefRow {
        id: sliderRow
        property real from: 0
        property real to: 100
        property real stepSize: 1
        property real value
        property string suffix
        property int decimals: 0
        signal moved(real v)
        focusPolicy: Qt.NoFocus
        activeFocusOnTab: false
        extra: RowLayout {
            width: parent ? parent.width : 0
            Slider {
                id: slider
                Layout.fillWidth: true
                from: sliderRow.from
                to: sliderRow.to
                stepSize: sliderRow.stepSize
                snapMode: Slider.SnapAlways
                value: sliderRow.value
                enabled: sliderRow.enabled
                activeFocusOnTab: true
                onMoved: sliderRow.moved(value)
            }
            Label {
                text: slider.value.toFixed(sliderRow.decimals) + sliderRow.suffix
                font.pointSize: 11
                Layout.minimumWidth: 80
                horizontalAlignment: Text.AlignRight
            }
        }
    }

    // ---- shared dialogs ----
    NavigableDialog {
        id: listDialog
        property var entries: []      // [{text, reason}]
        property int current: -1
        property var pickFn: null
        modal: true
        standardButtons: Dialog.Cancel
        // a plain panel: the Material elevation effect is not drawn by every scene graph backend
        background: Rectangle {
            color: "#424242"
            radius: 4
            border.color: "#55FFFFFF"
        }
        width: Math.min(settingsPage.width - 40, 560)

        function show(title, entries, current, fn) {
            listDialog.title = title
            listDialog.entries = entries
            listDialog.current = current
            listDialog.pickFn = fn
            open()
        }
        onOpened: listView.forceActiveFocus()

        ListView {
            id: listView
            implicitHeight: Math.min(contentHeight, settingsPage.height - 220)
            implicitWidth: parent ? parent.width : 400
            clip: true
            model: listDialog.entries
            currentIndex: listDialog.current
            keyNavigationEnabled: true
            delegate: RadioDelegate {
                width: listView.width
                text: modelData.reason ? modelData.text + "\n" + qsTr("Not available: %1").arg(modelData.reason) : modelData.text
                checked: index === listDialog.current
                enabled: !modelData.reason
                highlighted: ListView.isCurrentItem
                onClicked: {
                    var f = listDialog.pickFn
                    listDialog.close()
                    if (f) f(index)
                }
                Keys.onReturnPressed: clicked()
                Keys.onEnterPressed: clicked()
            }
        }
    }

    NavigableDialog {
        id: editDialog
        property var doneFn: null
        property string pattern: ".*"
        property string hint
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        // a plain panel: the Material elevation effect is not drawn by every scene graph backend
        background: Rectangle {
            color: "#424242"
            radius: 4
            border.color: "#55FFFFFF"
        }
        width: Math.min(settingsPage.width - 40, 520)

        function show(title, hint, text, pattern, fn) {
            editDialog.title = title
            editDialog.hint = hint
            editDialog.pattern = pattern
            editDialog.doneFn = fn
            editField.text = text
            open()
        }
        onOpened: editField.forceActiveFocus()
        onAccepted: {
            if (!editField.acceptableInput) return
            var f = editDialog.doneFn
            if (f) f(editField.text.trim())
        }

        ColumnLayout {
            width: parent ? parent.width : 400
            Label {
                Layout.fillWidth: true
                text: editDialog.hint
                wrapMode: Text.Wrap
                font.pointSize: 10
                color: settingsPage.summaryColor
            }
            TextField {
                id: editField
                Layout.fillWidth: true
                validator: RegularExpressionValidator { regularExpression: new RegExp(editDialog.pattern) }
                Keys.onReturnPressed: editDialog.accept()
                Keys.onEnterPressed: editDialog.accept()
            }
        }
    }

    // ---- the page ----
    ColumnLayout {
        id: prefColumn
        width: Math.min(settingsPage.width - 20, 980)
        x: Math.round((settingsPage.width - width) / 2)
        spacing: 0

        // ======== Connection (the owner's: the first thing on the page) ========
        PrefCategory { text: qsTr("Connection") }

        PrefRow {
            id: networkRowItem
            objectName: "networkRow"
            title: qsTr("Network connection")
            valueText: settingsPage.networkNames[settingsPage.networkIndex()]
            summary: settingsPage.networkSummaries[settingsPage.networkIndex()]
            onClicked: {
                var e = []
                for (var i = 0; i < settingsPage.networkNames.length; i++)
                    e.push({ text: settingsPage.networkNames[i] + "\n" + settingsPage.networkSummaries[i], reason: "" })
                listDialog.show(title, e, settingsPage.networkIndex(), function(i) {
                    MlxrPrefs.networkPath = settingsPage.networkValues[i]
                })
            }
        }

        PrefRow {
            objectName: "bitrateWifiRow"
            title: qsTr("Bitrate over Wi-Fi")
            valueText: settingsPage.pathBitrateText(MlxrPrefs.bitrateWifi)
            summary: settingsPage.pathBitrateSummary
            onClicked: MlxrPrefs.bitrateWifi = settingsPage.nextPathBitrate(MlxrPrefs.bitrateWifi)
        }

        PrefRow {
            objectName: "bitrateAdapterRow"
            title: qsTr("Bitrate over the Steam Frame Wireless Adapter")
            valueText: settingsPage.pathBitrateText(MlxrPrefs.bitrateAdapter)
            summary: settingsPage.pathBitrateSummary
            onClicked: MlxrPrefs.bitrateAdapter = settingsPage.nextPathBitrate(MlxrPrefs.bitrateAdapter)
        }

        PrefRow {
            objectName: "bitrateWiredRow"
            title: qsTr("Bitrate over a wired network")
            valueText: settingsPage.pathBitrateText(MlxrPrefs.bitrateWired)
            summary: settingsPage.pathBitrateSummary
            onClicked: MlxrPrefs.bitrateWired = settingsPage.nextPathBitrate(MlxrPrefs.bitrateWired)
        }

        PrefRow {
            objectName: "bitrateUsbRow"
            title: qsTr("Bitrate over the USB cable")
            valueText: settingsPage.pathBitrateText(MlxrPrefs.bitrateUsb)
            summary: settingsPage.pathBitrateSummary
            onClicked: MlxrPrefs.bitrateUsb = settingsPage.nextPathBitrate(MlxrPrefs.bitrateUsb)
        }

        // Native (lane wave1-burst): the cap on the headset's full-frame requests after lost video. Pressing
        // the row steps No limit -> 250 -> 500 -> 1000 -> 2000 ms.
        PrefRow {
            objectName: "idrCapRow"
            title: qsTr("Full-frame requests after lost video")
            valueText: settingsPage.idrCapText(MlxrPrefs.rfiIdrMs)
            summary: qsTr("At most one request for a full picture in this time when video is lost. A full picture is large; asked for again while the Wi-Fi is still losing packets, it makes the freeze longer. 500 ms suits most links.")
            onClicked: MlxrPrefs.rfiIdrMs = settingsPage.nextLimit(settingsPage.idrCapValues, MlxrPrefs.rfiIdrMs)
        }

        // ======== Video Settings ========
        PrefCategory { text: qsTr("Video Settings") }

        PrefRow {
            id: resolutionRow
            objectName: "resolutionRow"
            title: qsTr("Video resolution")
            valueText: settingsPage.resolutionText()
            summary: qsTr("Increase to improve image clarity. Decrease for better performance on lower end devices and slower networks.")
            onClicked: {
                var e = []
                for (var i = 0; i < settingsPage.resolutions.length; i++) {
                    var r = settingsPage.resolutions[i]
                    e.push({ text: r.text + " (" + r.w + "x" + r.h + ")", reason: "" })
                }
                listDialog.show(title, e, settingsPage.resolutionIndex(), function(i) {
                    StreamingPreferences.width = settingsPage.resolutions[i].w
                    StreamingPreferences.height = settingsPage.resolutions[i].h
                    settingsPage.resetBitrate()
                })
            }
        }

        PrefRow {
            objectName: "refreshRow"
            title: qsTr("Refresh rate")
            valueText: settingsPage.rateText()
            summary: qsTr("How many frames per second the PC streams. Match headset uses the rate SteamVR runs the Steam Frame's display at for this app (90 Hz unless changed in SteamVR's settings). Picking 72, 90, 120 or 144 Hz also sets the headset to that rate in SteamVR's settings for this app. 120 and 144 Hz need the stream and the PC to keep up. The stream never goes above the headset's rate.") +
                     " " + qsTr("Custom takes rates such as 59.94 (Apollo).")
            onClicked: {
                var e = []
                for (var i = 0; i < settingsPage.rateNames.length; i++) e.push({ text: settingsPage.rateNames[i], reason: "" })
                listDialog.show(title, e, settingsPage.rateIndex(), function(i) {
                    if (i === 0) {
                        MlxrPrefs.matchHeadsetFps = true
                        MlxrPrefs.customRefreshRate = ""
                        StreamingPreferences.fps = settingsPage.matchBitrateFps
                        settingsPage.resetBitrate()
                    }
                    else if (i < settingsPage.rateNames.length - 1) {
                        MlxrPrefs.matchHeadsetFps = false
                        MlxrPrefs.customRefreshRate = ""
                        StreamingPreferences.fps = settingsPage.fixedRates[i - 1]
                        settingsPage.resetBitrate()
                    }
                    else {
                        editDialog.show(qsTr("Custom refresh rate"), qsTr("Enter refresh rate manually, for example 59.94. Only supports Apollo. Capped at the headset's rate (%1 Hz at most).").arg(MlxrPrefs.headsetMaxFps),
                                        MlxrPrefs.customRefreshRate, "^\\s*\\d{2,3}(\\.\\d{1,3})?\\s*$", function(t) {
                            var v = Math.min(Math.max(parseFloat(t), 10), MlxrPrefs.headsetMaxFps)
                            MlxrPrefs.matchHeadsetFps = false
                            MlxrPrefs.customRefreshRate = "" + v
                            StreamingPreferences.fps = Math.round(v)
                            settingsPage.resetBitrate()
                        })
                    }
                })
            }
        }

        PrefSlider {
            title: qsTr("Video bitrate")
            summary: qsTr("Increase for better image quality. Decrease to improve performance on slower connections.") +
                     " " + qsTr("Goes up to 500 Mbps, for a good 5 GHz Wi-Fi or a wired link.")
            from: 0.5
            to: 500
            stepSize: 0.5
            decimals: 1
            suffix: " " + qsTr("Mbps")
            value: StreamingPreferences.bitrateKbps / 1000.0
            onMoved: function(v) { StreamingPreferences.bitrateKbps = Math.round(v * 1000) }
        }

        PrefRow {
            objectName: "pacingRow"
            title: qsTr("Video frame pacing")
            valueText: settingsPage.pacingNames[Math.max(0, settingsPage.pacingValues.indexOf(MlxrPrefs.framePacing))]
            summary: qsTr("Specify how to balance video latency and smoothness")
            onClicked: {
                var e = []
                for (var i = 0; i < settingsPage.pacingNames.length; i++) e.push({ text: settingsPage.pacingNames[i], reason: "" })
                listDialog.show(title, e, settingsPage.pacingValues.indexOf(MlxrPrefs.framePacing), function(i) {
                    MlxrPrefs.framePacing = settingsPage.pacingValues[i]
                })
            }
        }

        PrefSwitch {
            title: qsTr("Ultra Low Latency (experimental)")
            summary: qsTr("Runs the stream's network and decoder threads at realtime priority on the Frame.")
            isOn: MlxrPrefs.ultraLowLatency
            onSwitched: function(on) { MlxrPrefs.ultraLowLatency = on }
        }

        PrefSwitch {
            title: qsTr("Use Virtual Display")
            summary: qsTr("Use Virtual Display by default.\nVirtual Display can automatically match your client side resolution and frame rate settings, giving you a seamless streaming experience.") + "\n" + settingsPage.apolloNote
            isOn: MlxrPrefs.virtualDisplay
            onSwitched: function(on) { MlxrPrefs.virtualDisplay = on }
        }

        PrefSlider {
            title: qsTr("Resolution Scale Factor")
            summary: qsTr("Server side requested virtual screen resolution will be multiplied by this factor. Streaming resolution are not affected.\ne.g. 120% will make 2000x1000 become 2400x1200.\nSetting a higher scale factor will improve image quality for some games.") + "\n" + qsTr("Requires Apollo to take effect.")
            from: 20
            to: 200
            stepSize: 1
            suffix: "%"
            value: MlxrPrefs.scaleFactor
            onMoved: function(v) { MlxrPrefs.scaleFactor = Math.round(v) }
        }

        PrefRow {
            title: qsTr("Custom Resolution")
            valueText: StreamingPreferences.width + "x" + StreamingPreferences.height
            summary: qsTr("Add a custom resolution, separated by x, strange resolutions may not work.\nFor example: 1920x1080")
            onClicked: editDialog.show(title, summary, StreamingPreferences.width + "x" + StreamingPreferences.height,
                                       "^\\s*\\d{3,4}\\s*[xX]\\s*\\d{3,4}\\s*$", function(t) {
                var p = t.toLowerCase().split("x")
                var w = parseInt(p[0]), h = parseInt(p[1])
                if (w >= 256 && h >= 256 && w <= 8192 && h <= 8192) {
                    StreamingPreferences.width = w
                    StreamingPreferences.height = h
                    settingsPage.resetBitrate()
                }
            })
        }

        PrefRow {
            title: qsTr("Custom bitrate")
            valueText: (StreamingPreferences.bitrateKbps / 1000.0) + " " + qsTr("Mbps")
            summary: qsTr("Enter bitrate manually.\nCan exceed the maximum range of the seekbar.")
            onClicked: editDialog.show(title, qsTr("Mbps, 0.5 to 500"), "" + (StreamingPreferences.bitrateKbps / 1000.0),
                                       "^\\s*\\d{1,3}(\\.\\d{1,3})?\\s*$", function(t) {
                var v = parseFloat(t)
                if (v >= 0.5 && v <= 500) StreamingPreferences.bitrateKbps = Math.round(v * 1000)
            })
        }

        PrefRow {
            title: qsTr("Render Mode")
            valueText: MlxrPrefs.renderMode ? qsTr("AI SBS 3D") : qsTr("Default 2D")
            summary: qsTr("Default 2D or AI 2D to 3D conversion: the headset turns the stream into 3D with its depth model (the bar's 3D button switches it too).")
            onClicked: listDialog.show(title, [{ text: qsTr("Default 2D"), reason: "" }, { text: qsTr("AI SBS 3D"), reason: "" }],
                                       MlxrPrefs.renderMode, function(i) { MlxrPrefs.renderMode = i })
        }

        PrefSlider {
            title: qsTr("3D Effect Strength")
            summary: qsTr("Higher more effect, but maybe strain on eyes and unnatural. Lower, easier on eyes and smoother experience.") +
                     (MlxrPrefs.strengthIsDefault ? " " + qsTr("(Now: the depth model's own.)") : "")
            reason: MlxrPrefs.renderMode !== 1 ? qsTr("Render Mode is Default 2D")
                    : MlxrPrefs.stereoSource !== "depth" ? qsTr("3D source is the PC's own pair") : ""
            from: 1
            to: 100
            value: MlxrPrefs.strength
            onMoved: function(v) { MlxrPrefs.strength = Math.round(v); MlxrPrefs.strengthIsDefault = false }
        }

        PrefSlider {
            title: qsTr("3D Convergence")
            summary: qsTr("Reference: 50 - Balanced / 0: InFrontOfScreen /  100 - BehindOfScreen") +
                     (MlxrPrefs.convergenceIsDefault ? " " + qsTr("(Now: the depth model's own.)") : "")
            reason: MlxrPrefs.renderMode === 1 ? "" : qsTr("Render Mode is Default 2D")
            from: 0
            to: 100
            value: MlxrPrefs.convergence
            onMoved: function(v) { MlxrPrefs.convergence = Math.round(v); MlxrPrefs.convergenceIsDefault = false }
        }

        // Native (PLAN 4.3): the headset's 3D tab's 3D source row; pressing steps Depth -> SBS -> Top-bottom
        PrefRow {
            objectName: "stereoSourceRow"
            title: qsTr("3D source")
            valueText: settingsPage.stereoSourceNames[settingsPage.stereoSourceIndex()]
            summary: qsTr("Depth model: the headset makes the 3D from a 2D stream. SBS or Top-bottom: the PC renders the two eyes itself (a 3D film, a game's own SBS mode, ReShade SuperDepth3D) and each eye sees its half; no depth model runs and Convergence moves the halves apart or together.") + " " +
                     qsTr("(also on the headset's 3D tab)")
            onClicked: MlxrPrefs.stereoSource = settingsPage.stereoSourceValues[(settingsPage.stereoSourceIndex() + 1) % settingsPage.stereoSourceValues.length]
        }

        PrefSwitch {
            objectName: "sbsFullRow"
            title: qsTr("Full-size SBS")
            summary: qsTr("On: the PC's pair holds a full picture per eye (set a 3840x1080 Custom Resolution for two 1920x1080 eyes). Off: half-size, each eye squeezed into half of a 1920x1080 frame.")
            reason: MlxrPrefs.stereoSource === "depth" ? qsTr("3D source is the depth model") : ""
            isOn: MlxrPrefs.sbsFull
            onSwitched: function(on) { MlxrPrefs.sbsFull = on }
        }

        // Native (lane wave5-depth): how closely the depth keeps to the edges in the picture. Pressing the
        // row steps 0, 25, 50, 75, 100 ([3d] edge_sharpness of the VR client's config.ini)
        PrefRow {
            objectName: "edgeRow"
            title: qsTr("3D Edge Sharpness")
            valueText: MlxrPrefs.edgeSharpness === 0 ? qsTr("0 (soft, the default)") : ("" + MlxrPrefs.edgeSharpness)
            summary: qsTr("Higher keeps hair, outlines and thin text on their own depth instead of blending them into what is behind. Too high can make edges look cut out.")
            reason: MlxrPrefs.renderMode === 1 ? "" : qsTr("Render Mode is Default 2D")
            onClicked: if (enabled) MlxrPrefs.edgeSharpness = settingsPage.nextEdge()
        }

        // Native (lane wave5-depth): the depth's smoothing over time restarts where the picture moved, so a
        // fast edge leaves no trail. Pressing the row steps Off, 5, 10, 20, 30 percent ([3d] motion_reset)
        PrefRow {
            objectName: "motionRow"
            title: qsTr("3D Motion Reset")
            valueText: MlxrPrefs.motionReset === 0 ? qsTr("Off (the default)") : (MlxrPrefs.motionReset + "%")
            summary: qsTr("For fast scenes: where the picture changes by more than this between two depth maps, the 3D stops smoothing the old depth into the new there, so moving edges leave no trail. Lower reacts to smaller changes; too low brings back flicker.")
            reason: MlxrPrefs.renderMode === 1 ? "" : qsTr("Render Mode is Default 2D")
            onClicked: if (enabled) MlxrPrefs.motionReset = settingsPage.nextMotion()
        }

        // Native (lane wave5-depth): upstream's depth scale, and the overall shape's gain beside it. Pressing a
        // row steps 50, 75, 100, 125, 150, 200 percent ([3d] depth_scale, depth_global)
        PrefRow {
            objectName: "depthScaleRow"
            title: qsTr("3D Depth Detail")
            valueText: MlxrPrefs.depthScale + "%" + (MlxrPrefs.depthScale === 100 ? " " + qsTr("(as the model makes it)") : "")
            summary: qsTr("The depth between things near each other, like a face in front of a wall. Higher makes them stand apart more, without pushing the whole picture further in or out.")
            reason: MlxrPrefs.renderMode === 1 ? "" : qsTr("Render Mode is Default 2D")
            onClicked: if (enabled) MlxrPrefs.depthScale = settingsPage.nextDepthStep(MlxrPrefs.depthScale)
        }

        PrefRow {
            objectName: "depthGlobalRow"
            title: qsTr("3D Depth Shape")
            valueText: MlxrPrefs.depthGlobal + "%" + (MlxrPrefs.depthGlobal === 100 ? " " + qsTr("(as the model makes it)") : "")
            summary: qsTr("The depth of the picture as a whole, from the near ground to the far sky. Lower flattens it toward the screen, which is easier on the eyes, while Depth Detail keeps things standing apart.")
            reason: MlxrPrefs.renderMode === 1 ? "" : qsTr("Render Mode is Default 2D")
            onClicked: if (enabled) MlxrPrefs.depthGlobal = settingsPage.nextDepthStep(MlxrPrefs.depthGlobal)
        }

        PrefSwitch {
            objectName: "hdrRow"
            title: qsTr("Enable HDR (Experimental)")
            summary: qsTr("Stream HDR when the game and PC GPU support it. HDR requires a GPU with HEVC Main 10 encoding support.")
            reason: qsTr("the Frame's video path is 8-bit SDR (H.264/HEVC Main decoded to NV12, shown on an SDR panel)")
            isOn: false
        }

        // ======== Audio Settings ========
        PrefCategory { text: qsTr("Audio Settings") }

        PrefRow {
            title: qsTr("Surround sound configuration")
            valueText: settingsPage.audioNames[StreamingPreferences.audioConfig]
            summary: qsTr("Enable 5.1 or 7.1 surround sound for home-theater systems")
            onClicked: {
                var e = []
                for (var i = 0; i < settingsPage.audioNames.length; i++) e.push({ text: settingsPage.audioNames[i], reason: "" })
                listDialog.show(title, e, StreamingPreferences.audioConfig, function(i) { StreamingPreferences.audioConfig = i })
            }
        }

        // ======== Gamepad Settings ========
        PrefCategory { text: qsTr("Gamepad Settings") }

        PrefSlider {
            title: qsTr("Adjust analog stick deadzone")
            summary: qsTr("Positive values set a normal dead zone, negative values apply dead zone compensation. Ensure your controller can adjust dead zones when using dead zone compensation, or centering may be difficult.") +
                     " " + qsTr("Physical gamepads; 0 sends the sticks as read.")
            from: -20
            to: 20
            suffix: "%"
            value: MlxrPrefs.deadzone
            onMoved: function(v) { MlxrPrefs.deadzone = Math.round(v) }
        }

        PrefSwitch {
            objectName: "flipRow"
            title: qsTr("Flip face buttons")
            summary: qsTr("Switches the face buttons A/B and X/Y for gamepads and the headset's gamepad mode")
            isOn: MlxrPrefs.flipFaceButtons
            onSwitched: function(on) { MlxrPrefs.flipFaceButtons = on }
        }

        PrefRow {
            objectName: "padMouseChordRow"
            title: qsTr("Gamepad as mouse")
            valueText: settingsPage.padMouseChordNames[Math.max(0, settingsPage.padMouseChordValues.indexOf(MlxrPrefs.padMouseChord))]
            summary: qsTr("Hold the chord on a gamepad to make it the PC's mouse: the left stick points, A, B and X click, the shoulder buttons are back and forward, Y opens the headset keyboard and the d-pad is the arrow keys. Hold it again to give the gamepad back.")
            onClicked: {
                var e = []
                for (var i = 0; i < settingsPage.padMouseChordNames.length; i++) e.push({ text: settingsPage.padMouseChordNames[i], reason: "" })
                listDialog.show(title, e, settingsPage.padMouseChordValues.indexOf(MlxrPrefs.padMouseChord), function(i) {
                    MlxrPrefs.padMouseChord = settingsPage.padMouseChordValues[i]
                })
            }
        }

        PrefRow {
            objectName: "padMouseScrollRow"
            title: qsTr("Gamepad mouse scroll stick")
            valueText: settingsPage.padMouseScrollNames[Math.max(0, settingsPage.padMouseScrollValues.indexOf(MlxrPrefs.padMouseScroll))]
            summary: qsTr("The stick that scrolls while a gamepad is the mouse. The other stick points.")
            onClicked: {
                var e = []
                for (var i = 0; i < settingsPage.padMouseScrollNames.length; i++) e.push({ text: settingsPage.padMouseScrollNames[i], reason: "" })
                listDialog.show(title, e, settingsPage.padMouseScrollValues.indexOf(MlxrPrefs.padMouseScroll), function(i) {
                    MlxrPrefs.padMouseScroll = settingsPage.padMouseScrollValues[i]
                })
            }
        }

        PrefRow {
            objectName: "padArbitrationRow"
            title: qsTr("When a gamepad is connected")
            valueText: settingsPage.padArbitrationNames[Math.max(0, settingsPage.padArbitrationValues.indexOf(MlxrPrefs.padArbitration))]
            summary: qsTr("What the headset controllers do once you use a gamepad. Pointer: their gamepad mode steps aside so the PC never sees two gamepads. Also gamepad: the PC sees both. Sleep: as Pointer, and their pointer rests too.")
            onClicked: {
                var e = []
                for (var i = 0; i < settingsPage.padArbitrationNames.length; i++) e.push({ text: settingsPage.padArbitrationNames[i], reason: "" })
                listDialog.show(title, e, settingsPage.padArbitrationValues.indexOf(MlxrPrefs.padArbitration), function(i) {
                    MlxrPrefs.padArbitration = settingsPage.padArbitrationValues[i]
                })
            }
        }

        // Native (lane wave3-cards): the gamepad card in the headset's lap; pressing the row steps the mode
        PrefRow {
            objectName: "padCardRow"
            title: qsTr("Gamepad card in the headset")
            valueText: settingsPage.padCardNames[settingsPage.padCardIndex()]
            summary: qsTr("A picture of the gamepad you are playing with, in your lap, that lights up as you press it. Auto: up while you play, gone 4 seconds after you stop. On: up while a gamepad is in use. Off: never.")
            onClicked: MlxrPrefs.padCard = settingsPage.padCardValues[(settingsPage.padCardIndex() + 1) % settingsPage.padCardValues.length]
        }

        // Native (lane wave3-cards): the first-run controls card comes up once; this brings it back
        PrefSwitch {
            objectName: "firstRunRow"
            title: qsTr("Controls card on the next stream")
            summary: qsTr("The card that shows the headset's controls when a stream starts. It comes up once by itself; switch this on to see it again.")
            isOn: !MlxrPrefs.firstRunShown
            onSwitched: function(on) { MlxrPrefs.firstRunShown = !on }
        }

        PrefSwitch {
            title: qsTr("Enable Rumble")
            summary: qsTr("Enable rumble feedback on supported gamepads.") + " " + qsTr("Also the headset controllers.")
            isOn: MlxrPrefs.rumble
            onSwitched: function(on) { MlxrPrefs.rumble = on }
        }

        // ======== Input Settings ========
        PrefCategory { text: qsTr("Input Settings") }

        PrefRow {
            objectName: "keymapRow"
            title: qsTr("Headset keyboard layout")
            valueText: settingsPage.keymapNames[Math.max(0, settingsPage.keymapValues.indexOf(MlxrPrefs.keyboardKeymap))]
            summary: qsTr("The letters on the headset keyboard. The PC's Windows keyboard layout has to be the same one: the keys send their positions and Windows turns them into letters.")
            onClicked: {
                var e = []
                for (var i = 0; i < settingsPage.keymapNames.length; i++) e.push({ text: settingsPage.keymapNames[i], reason: "" })
                listDialog.show(title, e, settingsPage.keymapValues.indexOf(MlxrPrefs.keyboardKeymap), function(i) {
                    MlxrPrefs.keyboardKeymap = settingsPage.keymapValues[i]
                })
            }
        }

        // Pressing the row steps Off -> Steam Frame -> Generic -> Gun -> Your own model
        PrefRow {
            objectName: "controllerModelRow"
            title: qsTr("Show controllers")
            valueText: settingsPage.modelNames[settingsPage.modelIndex()]
            summary: qsTr("The controllers drawn in the headset. Your own model reads custom_left.model and custom_right.model (or one custom.model, mirrored for the left hand) from the models folder next to the headset's config file, and falls back to the Steam Frame controllers when they are missing.") + " " +
                     qsTr("(the headset's Display tab has the first four)")
            onClicked: MlxrPrefs.controllerModel = settingsPage.modelValues[(settingsPage.modelIndex() + 1) % settingsPage.modelValues.length]
        }

        PrefRow {
            objectName: "bindingsRow"
            title: qsTr("Controller bindings")
            summary: qsTr("What each button on the Frame's controllers does, in pointer mode and in gamepad mode, for every app or for one app. Saved in bindings.ini; takes effect when the next stream starts.")
            onClicked: settingsPage.StackView.view.push("qrc:/gui/XrBindingsView.qml")
        }

        PrefSwitch {
            title: qsTr("Enable forward/back mouse buttons")
            summary: qsTr("Sends a physical mouse's back and forward buttons to the PC")
            isOn: MlxrPrefs.mouseNavButtons
            onSwitched: function(on) { MlxrPrefs.mouseNavButtons = on }
        }

        // ======== Host Settings ========
        PrefCategory { text: qsTr("Host Settings") }

        PrefSwitch {
            title: qsTr("Optimize game settings")
            summary: qsTr("Allow GFE to modify game settings for optimal streaming")
            isOn: StreamingPreferences.gameOptimizations
            onSwitched: function(on) { StreamingPreferences.gameOptimizations = on }
        }

        PrefSwitch {
            title: qsTr("Play audio on PC")
            summary: qsTr("Play audio from the computer and this device")
            isOn: StreamingPreferences.playAudioOnHost
            onSwitched: function(on) { StreamingPreferences.playAudioOnHost = on }
        }

        // ======== General Settings ========
        PrefCategory { text: qsTr("General Settings") }

        PrefSwitch {
            objectName: "clipboardRow"
            title: qsTr("Smart Clipboard Sync (Experimental)")
            summary: qsTr("Automatically upload clipboard content to server when the stream starts, download clipboard from server when you leave the stream (requires Apollo)")
            isOn: MlxrPrefs.clipboardSync
            onSwitched: function(on) { MlxrPrefs.clipboardSync = on }
        }

        // ======== UI Settings ========
        PrefCategory { text: qsTr("UI Settings") }

        PrefSwitch {
            objectName: "quickResumeRow"
            title: qsTr("Quick Resume")
            summary: qsTr("Do not show resume menu when clicking resume button, resume the app immediately.")
            isOn: MlxrPrefs.quickResume
            onSwitched: function(on) { MlxrPrefs.quickResume = on }
        }

        PrefSwitch {
            title: qsTr("Enable Full Screen")
            summary: qsTr("Show this window full screen (from the next start)")
            isOn: StreamingPreferences.uiDisplayMode === StreamingPreferences.UI_FULLSCREEN
            onSwitched: function(on) {
                StreamingPreferences.uiDisplayMode = on ? StreamingPreferences.UI_FULLSCREEN : StreamingPreferences.UI_WINDOWED
            }
        }

        // ======== Performance Monitor ========
        PrefCategory { text: qsTr("Performance Monitor") }

        PrefSwitch {
            objectName: "statsRow"
            title: qsTr("Show performance stats while streaming")
            summary: qsTr("Display real-time stream performance information while streaming") + " " +
                     qsTr("(the headset's stats panel; also on its Display tab)")
            isOn: MlxrPrefs.perfOverlay
            onSwitched: function(on) { MlxrPrefs.perfOverlay = on }
        }

        // The headset's toast: pressing the row steps All -> Important -> Off
        PrefRow {
            objectName: "noticesRow"
            title: qsTr("Headset notices")
            valueText: settingsPage.noticeNames[settingsPage.noticeIndex()]
            summary: qsTr("The small pop-ups in the headset. Important leaves out the display rate and 3D on/off and keeps mode changes and errors.") + " " +
                     qsTr("(also on its Display tab)")
            onClicked: MlxrPrefs.notices = settingsPage.noticeValues[(settingsPage.noticeIndex() + 1) % settingsPage.noticeValues.length]
        }

        // The controllers' thumbstick wheel in the headset: pressing the row steps the speed
        PrefRow {
            objectName: "scrollSpeedRow"
            title: qsTr("Headset scroll speed")
            valueText: MlxrPrefs.scrollSpeed + "%"
            summary: qsTr("How fast a controller's thumbstick scrolls in pointer mode. Tilt it sideways to scroll sideways.")
            onClicked: MlxrPrefs.scrollSpeed = settingsPage.nextScrollSpeed()
        }

        // Native (lane wave3-visual): the Picture tab's Background and Glow edge rows, the void's colour and
        // the 3D eye balance. Pressing a row steps through its choices.
        PrefRow {
            objectName: "backgroundDimRow"
            title: qsTr("Dim the surroundings")
            valueText: MlxrPrefs.backgroundDim + "%"
            summary: qsTr("How far the room, the passthrough cameras or the void are turned down, the picture itself not. 100% is black.") + " " +
                     qsTr("(the headset's Picture tab has it too)")
            onClicked: MlxrPrefs.backgroundDim = settingsPage.nextLimit(settingsPage.backgroundDimSteps, MlxrPrefs.backgroundDim)
        }

        PrefRow {
            objectName: "glowFeatherRow"
            title: qsTr("Glow edge")
            valueText: settingsPage.glowFeatherNames[Math.max(0, settingsPage.glowFeatherValues.indexOf(MlxrPrefs.glowFeather))]
            summary: qsTr("Fades the picture's outer edge into the glow around it so the screen has no hard border. Needs the glow on.") + " " +
                     qsTr("(the headset's Picture tab has it too)")
            onClicked: MlxrPrefs.glowFeather = settingsPage.nextLimit(settingsPage.glowFeatherValues, MlxrPrefs.glowFeather)
        }

        PrefRow {
            objectName: "voidColorRow"
            title: qsTr("Void colour")
            valueText: MlxrPrefs.voidColor
            summary: qsTr("The colour of the void environment, as on a web page: #rrggbb. Black draws nothing.")
            onClicked: editDialog.show(title, qsTr("#rrggbb"), MlxrPrefs.voidColor,
                                       "^\\s*#?([0-9a-fA-F]{3}|[0-9a-fA-F]{6})\\s*$", function(t) {
                MlxrPrefs.voidColor = "#" + t.trim().replace("#", "").toLowerCase()
            })
        }

        PrefRow {
            objectName: "eyeBalanceRow"
            title: qsTr("3D eye balance")
            valueText: MlxrPrefs.eyeBalance > 0 ? qsTr("Left eye %1%").arg(MlxrPrefs.eyeBalance)
                     : MlxrPrefs.eyeBalance < 0 ? qsTr("Right eye %1%").arg(-MlxrPrefs.eyeBalance) : qsTr("Even")
            summary: qsTr("How the 3D's shift is shared by the two eyes. Even moves each eye the same; at 100% only the left eye's picture moves and the right eye's is as streamed. The depth you see is the same at every setting. Useful with a dominant eye.")
            onClicked: MlxrPrefs.eyeBalance = settingsPage.nextLimit(settingsPage.eyeBalanceSteps, MlxrPrefs.eyeBalance)
        }

        // The headset coming off: the sound mutes and the input is let go at once; after the time the stream
        // is disconnected and the PC's app keeps running. Pressing a row steps through the choices.
        PrefRow {
            objectName: "doffRow"
            title: qsTr("Disconnect when the headset is off")
            valueText: settingsPage.limitText(MlxrPrefs.doffDisconnect, qsTr("second"), qsTr("seconds"))
            summary: qsTr("How long the headset can stay off before the stream is disconnected. The app keeps running on the PC. Putting the headset back on sooner carries on.")
            onClicked: MlxrPrefs.doffDisconnect = settingsPage.nextLimit(settingsPage.doffValues, MlxrPrefs.doffDisconnect)
        }

        PrefRow {
            objectName: "idleRow"
            title: qsTr("Disconnect when idle")
            valueText: settingsPage.limitText(MlxrPrefs.idleDisconnect, qsTr("minute"), qsTr("minutes"))
            summary: qsTr("Disconnect the stream after this long with no input from the controllers, hands, keyboard, mouse or pads. The app keeps running on the PC.")
            onClicked: MlxrPrefs.idleDisconnect = settingsPage.nextLimit(settingsPage.idleValues, MlxrPrefs.idleDisconnect)
        }

        // ======== PC VR (lane wave4-pcvr): Valve's Steam Link stream of the PC ========
        PrefCategory { text: qsTr("PC VR") }

        PrefRow {
            objectName: "pcvrCheckRow"
            title: qsTr("Switch to PC VR")
            valueText: MlxrPrefs.pcvrBusy ? qsTr("Asking Steam...") : ""
            summary: (MlxrPrefs.pcvrStatus !== "" ? MlxrPrefs.pcvrStatus + " " : "") +
                     qsTr("The Host sheet's PC VR button ends the stream and has the headset's Steam stream a VR game from your PC. That is Valve's Steam Link, not Moonlight: its picture, its tracking and its limits. Press here to check Steam and list the PCs you paired with and their games.")
            onClicked: MlxrPrefs.pcvrRefresh()
        }

        PrefRow {
            objectName: "pcvrPcRow"
            title: qsTr("PC to stream from")
            valueText: MlxrPrefs.pcvrClientName !== "" ? MlxrPrefs.pcvrClientName : qsTr("The only paired PC")
            summary: qsTr("A PC this headset is paired with in Steam (Remote Play). Press the first row above to list them.")
            onClicked: {
                var e = [{ text: qsTr("The only paired PC"), reason: "" }]
                var cur = 0
                var d = MlxrPrefs.pcvrDevices
                for (var i = 0; i < d.length; i++) {
                    e.push({ text: d[i].clientName + (d[i].streaming ? " " + qsTr("(streaming now)") : ""), reason: "" })
                    if (d[i].clientId === MlxrPrefs.pcvrClient) cur = i + 1
                }
                listDialog.show(title, e, cur, function(i) { MlxrPrefs.pcvrChooseDevice(i - 1) })
            }
        }

        PrefRow {
            objectName: "pcvrAppRow"
            title: qsTr("PC VR game")
            valueText: MlxrPrefs.pcvrApp === "" ? qsTr("None: open Steam's library")
                       : (MlxrPrefs.pcvrAppName !== "" ? MlxrPrefs.pcvrAppName : qsTr("Steam app %1").arg(MlxrPrefs.pcvrApp))
            summary: qsTr("The game the PC VR button streams from the PC. With none, Steam's library opens and you press \"Stream from your PC\" on a game yourself.")
            onClicked: {
                var e = [{ text: qsTr("None: open Steam's library"), reason: "" }]
                var cur = 0
                var a = MlxrPrefs.pcvrApps
                for (var i = 0; i < a.length; i++) {
                    e.push({ text: a[i].name + (a[i].vr === true ? "  (VR)" : ""), reason: "" })
                    if (a[i].appid === MlxrPrefs.pcvrApp) cur = i + 1
                }
                listDialog.show(title, e, cur, function(i) { MlxrPrefs.pcvrChooseApp(i - 1) })
            }
        }

        PrefRow {
            objectName: "pcvrAppIdRow"
            title: qsTr("PC VR game: Steam app id")
            valueText: MlxrPrefs.pcvrApp
            summary: qsTr("The same choice typed by hand, for when Steam does not list the PC's games. Empty for none.")
            onClicked: editDialog.show(title, qsTr("The game's Steam app id, digits only (it is in its store page address)."),
                                       MlxrPrefs.pcvrApp, "^\\d{0,10}$", function(t) { MlxrPrefs.pcvrSetAppId(t) })
        }

        // ======== Advanced Settings ========
        PrefCategory { text: qsTr("Advanced Settings") }

        PrefRow {
            objectName: "codecRow"
            title: qsTr("Change codec settings")
            valueText: settingsPage.codecEntries[settingsPage.codecIndex()].text
            summary: qsTr("Newer codecs can lower video bandwidth requirements if your device supports them. Codec selections may be ignored if not supported by the host software or GPU.")
            onClicked: listDialog.show(title, settingsPage.codecEntries, settingsPage.codecIndex(), function(i) {
                StreamingPreferences.videoCodecConfig = settingsPage.codecEntries[i].val
            })
        }

        PrefSwitch {
            title: qsTr("Unlock all possible frame rates")
            summary: qsTr("Streaming at 90 or 120 FPS may reduce latency on high-end devices but can cause lag or instability on devices that can't support it")
            reason: qsTr("the VR client caps the frame rate at the headset's rate, set in SteamVR for this app (%1 Hz at most)").arg(MlxrPrefs.headsetMaxFps)
            isOn: false
        }

        PrefSwitch {
            title: qsTr("Force full range video (Experimental)")
            summary: qsTr("This will cause loss of detail in light and dark areas if your device doesn't properly display full range video content.") +
                     " " + qsTr("Hardware decoding only.")
            isOn: MlxrPrefs.fullRange
            onSwitched: function(on) { MlxrPrefs.fullRange = on }
        }

        PrefSwitch {
            title: qsTr("Force software decoding")
            summary: qsTr("From Moonlight: decode on the CPU instead of the Frame's hardware decoder (slow; for troubleshooting)")
            isOn: StreamingPreferences.videoDecoderSelection === StreamingPreferences.VDS_FORCE_SOFTWARE
            onSwitched: function(on) {
                StreamingPreferences.videoDecoderSelection = on ? StreamingPreferences.VDS_FORCE_SOFTWARE : StreamingPreferences.VDS_AUTO
            }
        }

        // ======== MISC ========
        PrefCategory { text: qsTr("MISC") }

        PrefRow {
            title: qsTr("Language")
            valueText: settingsPage.languages[settingsPage.languageIndex()].text
            summary: qsTr("Language to use for Moonlight XR")
            onClicked: {
                var e = []
                for (var i = 0; i < settingsPage.languages.length; i++) e.push({ text: settingsPage.languages[i].text, reason: "" })
                listDialog.show(title, e, settingsPage.languageIndex(), function(i) {
                    var lang = settingsPage.languages[i].val
                    if (StreamingPreferences.language !== lang) {
                        StreamingPreferences.language = lang
                        if (StreamingPreferences.retranslate()) {
                            window.clearOnBack = true
                            settingsPage.languageChanged()
                        }
                    }
                })
            }
        }

        Label {
            Layout.fillWidth: true
            Layout.topMargin: 24
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            text: qsTr("More settings for the headset (screen, room, pointer, keyboard) are in its cog menu, saved in %1.").arg(MlxrPrefs.configIniPath)
            font.pointSize: 9
            color: settingsPage.summaryColor
            wrapMode: Text.Wrap
        }
    }
}
