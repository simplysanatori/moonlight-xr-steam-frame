// Moonlight XR Native launcher (lane artemis-settings): the Settings page in hand-off mode, laid out
// and worded after Artemis (ClassicOldSong/moonlight-android, res/xml/preferences.xml): its
// categories in its order, a title and a summary per row, lists in a dialog. Only Artemis settings
// the Frame and the VR client honour are shown (HDR, AV1 and the frame-rate unlock are left out); a row
// that depends on another is greyed with its reason while that one is off. Where each one goes:
// docs/dev/reports/launcher.md ("Artemis settings"). Lane flat-coherence: titles in sentence case and, where
// the headset's cog has the same setting, with the cog's name (ui/ui_strings.h). New file, copied into
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
import MlxrProfiles 1.0

Flickable {
    id: settingsPage
    objectName: qsTr("Settings")

    signal languageChanged()

    readonly property color accentColor: "#80CBC4"
    readonly property color summaryColor: "#B3FFFFFF"
    readonly property color reasonColor: "#FFB74D"
    readonly property string apolloNote: qsTr("Needs Apollo.")

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
    // The size typed in the list's Custom... entry (Artemis's Custom Resolution row, folded into the list)
    function editCustomResolution() {
        editDialog.show(qsTr("Custom resolution"), qsTr("Width x height, for example 2560x1440. A size the PC cannot make may fail to start."),
                        StreamingPreferences.width + "x" + StreamingPreferences.height,
                        "^\\s*\\d{3,4}\\s*[xX]\\s*\\d{3,4}\\s*$", function(t) {
            var p = t.toLowerCase().split("x")
            var w = parseInt(p[0]), h = parseInt(p[1])
            if (w >= 256 && h >= 256 && w <= 8192 && h <= 8192) {
                StreamingPreferences.width = w
                StreamingPreferences.height = h
                resetBitrate()
            }
        })
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
    readonly property var pacingNames: [qsTr("Prefer lowest latency"), qsTr("Balanced"), qsTr("Prefer smoothest video")]

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
                                             qsTr("Streams over the Steam Frame Wireless Adapter plugged into your PC. Its latency can be worse than a good 5 GHz Wi-Fi; Automatic measures both before it chooses."),
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
    readonly property var noticeNames: [qsTr("All"), qsTr("Important"), qsTr("Off")]
    function noticeIndex() {
        return Math.max(0, noticeValues.indexOf(MlxrPrefs.notices))
    }

    // [vr] scroll_speed: the steps the row goes through, percent
    readonly property var scrollSpeedSteps: [50, 75, 100, 150, 200, 300]
    readonly property var zoomSteps: [100, 125, 150, 175, 200, 250, 300, 350, 400]
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
    readonly property var modelValues: ["off", "frame", "gun", "custom"]
    readonly property var modelNames: [qsTr("Off"), qsTr("Frame"), qsTr("Gun"), qsTr("Your own model")]
    function modelIndex() {
        return Math.max(0, modelValues.indexOf(MlxrPrefs.controllerModel))
    }

    // [stream] doff_disconnect (seconds) and idle_disconnect (minutes) of the VR client's config.ini: 0 is never
    readonly property var doffValues: [0, 15, 30, 60, 120, 300]
    readonly property var idleValues: [0, 10, 30, 60, 120]
    // One style for every time on the page: ms below a second, s below a minute, min above
    function timeText(ms) {
        return ms < 1000 ? qsTr("%1 ms").arg(ms) : ms < 60000 ? qsTr("%1 s").arg(ms / 1000) : qsTr("%1 min").arg(ms / 60000)
    }
    function nextLimit(values, current) {
        var i = values.indexOf(current)
        return values[(i + 1) % values.length]   // a value typed into config.ini that is not listed steps to Never
    }
    // Native (lane wave1-burst): [stream] rfi_idr_ms of the VR client's config.ini: 0 is no limit
    readonly property var idrCapValues: [0, 250, 500, 1000, 2000]
    function idrCapText(v) {
        return v === 0 ? qsTr("No limit") : timeText(v)
    }
    // Native (lane wave1-failover): [stream] failover_burst_ms of the VR client's config.ini
    readonly property var failoverBurstValues: [1500, 3000, 5000, 8000]
    function failoverBurstText(v) {
        return timeText(v)
    }
    function nextFailoverBurst(current) {
        for (var i = 0; i < failoverBurstValues.length; i++)
            if (failoverBurstValues[i] > current) return failoverBurstValues[i]
        return failoverBurstValues[0]   // past the last (or typed into config.ini above it): back to the shortest
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
    readonly property var padCardNames: [qsTr("Automatic"), qsTr("On"), qsTr("Off")]
    function padCardIndex() {
        return Math.max(0, padCardValues.indexOf(MlxrPrefs.padCard))
    }
    // Native (lane wave2-lightgun-gyro): [vr] lightgun and pad_gyro of the VR client
    readonly property var lightgunValues: ["auto", "on", "off"]
    readonly property var lightgunNames: [qsTr("Automatic"), qsTr("On"), qsTr("Off")]
    readonly property var padGyroValues: ["off", "controller", "head"]
    readonly property var padGyroNames: [qsTr("Off"), qsTr("Controller"), qsTr("Head")]

    // The cog's three-way rows over two config.ini switches each (ui/settings.c settings_head_lock_cell and
    // settings_pointer_cell): Head locked is [display] head_locked / head_follow, Ray is [vr] show_ray / pointer_dot
    readonly property var headLockNames: [qsTr("Off"), qsTr("On"), qsTr("Follow")]
    function headLockIndex() {
        return MlxrPrefs.vr["display.head_locked"] === true ? 1 : (MlxrPrefs.vr["display.head_follow"] === true ? 2 : 0)
    }
    readonly property var rayNames: [qsTr("Off"), qsTr("Dot"), qsTr("Ray")]
    function rayIndex() {
        return MlxrPrefs.vr["vr.show_ray"] === true ? 2 : (MlxrPrefs.vr["vr.pointer_dot"] === true ? 1 : 0)
    }

    readonly property var audioNames: [qsTr("Stereo"), qsTr("5.1 surround sound"), qsTr("7.1 surround sound")]

    // Artemis video_format without AV1 (the Frame decodes H.264 and HEVC only; patch 0005 drops it from the stock list too)
    readonly property var codecEntries: [
        { text: qsTr("Automatic (Recommended)"), val: StreamingPreferences.VCC_AUTO, reason: "" },
        { text: qsTr("Prefer HEVC"), val: StreamingPreferences.VCC_FORCE_HEVC, reason: "" },
        { text: qsTr("Prefer H.264"), val: StreamingPreferences.VCC_FORCE_H264, reason: "" }
    ]
    function codecIndex() {
        var v = StreamingPreferences.videoCodecConfig
        if (v === StreamingPreferences.VCC_FORCE_H264) return 2
        if (v === StreamingPreferences.VCC_FORCE_HEVC || v === StreamingPreferences.VCC_FORCE_HEVC_HDR_DEPRECATED) return 1
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
    // Native (lane wave6-i18n): the headset UI's language ([ui] language in the VR client's config.ini).
    // The codes are tools/ui/gen_strings.py's LANGS (tests/ui/test_i18n_gen.py checks they agree)
    readonly property var headsetLanguageValues: ["auto", "en", "cs", "da", "de", "es", "fi", "fr", "it", "nb", "nl", "pl", "pt-BR", "sv", "tr", "ru", "uk", "ja", "ko", "zh-CN", "zh-TW"]
    readonly property var headsetLanguageNames: [qsTr("Automatic (the headset's system language)"), "English", "Čeština", "Dansk", "Deutsch", "Español", "Suomi", "Français", "Italiano", "Norsk bokmål", "Nederlands", "Polski", "Português (Brasil)", "Svenska", "Türkçe", "Русский", "Українська", "日本語", "한국어", "简体中文", "繁體中文"]
    function headsetLanguageText() {
        var i = headsetLanguageValues.indexOf(MlxrPrefs.headsetLanguage)
        return i >= 0 ? headsetLanguageNames[i] : MlxrPrefs.headsetLanguage
    }

    // Native (lane launcher-bundle, PLAN 6.4): the On start row
    readonly property var onStartValues: ["none", "desktop", "app"]
    readonly property var onStartNames: [qsTr("Nothing"), qsTr("Last PC's Desktop"), qsTr("An app")]
    function onStartText() {
        if (MlxrPrefs.onStart === "desktop")
            return MlxrPrefs.lastPc !== "" ? qsTr("Desktop on %1").arg(MlxrPrefs.lastPc) : qsTr("Last PC's Desktop (no stream yet)")
        if (MlxrPrefs.onStart === "app")
            return MlxrPrefs.onStartApp !== "" ? qsTr("%1 on %2").arg(MlxrPrefs.onStartApp).arg(MlxrPrefs.onStartPc) : qsTr("An app (none chosen)")
        return qsTr("Nothing")
    }
    function pickOnStart(i) {
        MlxrPrefs.onStart = onStartValues[Number(i)]
    }
    function pickOnStartApp(pc, app) {
        MlxrPrefs.onStartPc = pc
        MlxrPrefs.onStartApp = app
    }

    function languageIndex() {
        for (var i = 0; i < languages.length; i++) {
            if (languages[i].val === StreamingPreferences.language) return i
        }
        return 0
    }

    // ---- search and the jump list (lane wave4-launcher) ----
    // The sections, for the jump list: {title, icon, item, shown}; shown is false for a section the search left empty.
    // A row is hidden through its searchHit (PrefRow, PrefCategory), so a row with a visible: of its own keeps it.
    property var sections: []

    function collectSections() {
        var out = []
        var kids = prefColumn.children
        for (var i = 0; i < kids.length; i++) {
            if (kids[i].isCategory === true) {
                out.push({ title: kids[i].text, icon: kids[i].icon, item: kids[i], shown: true })
            }
        }
        sections = out
    }

    function searchTerms() {
        return searchField.text.toLowerCase().split(/\s+/).filter(function(t) { return t !== "" })
    }

    // Every word typed has to be in the row's title, its value, its summary or its section's name
    function rowMatches(row, terms, sectionTitle) {
        var hay = (row.title + "\n" + row.valueText + "\n" + row.summary + "\n" + sectionTitle).toLowerCase()
        for (var i = 0; i < terms.length; i++) {
            if (hay.indexOf(terms[i]) < 0) return false
        }
        return true
    }

    // Shows the rows that match and the sections that still have a row; nothing typed shows everything
    function applySearch() {
        var terms = searchTerms()
        var kids = prefColumn.children
        var titles = []
        var cat = null
        var catHas = false
        for (var i = 0; i <= kids.length; i++) {
            var it = i < kids.length ? kids[i] : null
            if (it === null || it.isCategory === true) {
                if (cat !== null) {
                    cat.searchHit = terms.length === 0 || catHas
                }
                cat = it
                catHas = false
            }
            else if (it.isPrefRow === true) {
                var m = terms.length === 0 || rowMatches(it, terms, cat !== null ? cat.text : "")
                it.searchHit = m
                if (m && it.visible) {
                    catHas = true
                    titles.push(it.title)
                }
            }
        }
        var out = []
        var names = []
        for (var s = 0; s < sections.length; s++) {
            var shown = sections[s].item.searchHit === true
            out.push({ title: sections[s].title, icon: sections[s].icon, item: sections[s].item, shown: shown })
            if (shown) names.push(sections[s].title)
        }
        sections = out
        noMatchLabel.visible = terms.length > 0 && titles.length === 0
        console.info("MLXR_SETTINGS_SEARCH: text=\"" + searchField.text + "\" rows=" + titles.length +
                     " titles=" + titles.join("|") + " sections=" + names.join("|"))
    }

    function setSearch(text) {
        searchField.text = text
    }

    function scrollTo(y) {
        autoScrollAnimation.stop()
        autoScrollAnimation.from = contentY
        autoScrollAnimation.to = Math.max(0, Math.min(y, contentHeight - height))
        autoScrollAnimation.start()
    }

    function jumpTo(item) {
        if (!item) return
        var y = item.mapToItem(contentItem, 0, 0).y - 12
        console.info("MLXR_SETTINGS_JUMP: " + item.text + " y=" + Math.round(y))
        scrollTo(y)
        // the section's first row takes the focus, so a gamepad goes on from there
        var kids = prefColumn.children
        var seen = false
        for (var i = 0; i < kids.length; i++) {
            if (kids[i] === item) {
                seen = true
            }
            else if (seen && kids[i].isPrefRow === true && kids[i].visible) {
                kids[i].forceActiveFocus(Qt.TabFocus)
                break
            }
        }
    }

    function jumpTop() {
        scrollTo(0)
        searchField.forceActiveFocus(Qt.TabFocus)
    }

    Component.onCompleted: collectSections()

    // ---- row types (Artemis's preference widgets) ----
    // A section header: an icon (XrIcon.qml, loaded on its own so a Qt without QtQuick.Shapes only loses the
    // pictures), its name and a button back to the top of the page. icon names the picture in XrIcon.qml.
    component PrefCategory: Item {
        id: category
        property string text
        property string icon
        property bool showTop: true
        property bool searchHit: true
        readonly property bool isCategory: true
        visible: searchHit
        Layout.fillWidth: true
        Layout.topMargin: 22
        Layout.leftMargin: 16
        Layout.rightMargin: 8
        Layout.bottomMargin: 2
        implicitHeight: categoryRow.implicitHeight
        RowLayout {
            id: categoryRow
            anchors.left: parent.left
            anchors.right: parent.right
            spacing: 10
            Loader {
                Layout.alignment: Qt.AlignVCenter
                source: category.icon !== "" ? "qrc:/gui/XrIcon.qml" : ""
                onLoaded: {
                    item.name = category.icon
                    item.color = settingsPage.accentColor
                    item.size = 22
                }
            }
            Label {
                Layout.fillWidth: true
                text: category.text
                font.pointSize: 11
                font.bold: true
                color: settingsPage.accentColor
            }
            ToolButton {
                objectName: "top_" + (category.icon !== "" ? category.icon : category.text)
                visible: category.showTop
                focusPolicy: Qt.StrongFocus
                activeFocusOnTab: true
                implicitWidth: 40
                implicitHeight: 40
                ToolTip.text: qsTr("Back to the top")
                ToolTip.visible: hovered
                ToolTip.delay: 800
                contentItem: Loader {
                    source: "qrc:/gui/XrIcon.qml"
                    onLoaded: {
                        item.name = "top"
                        item.color = settingsPage.summaryColor
                        item.size = 20
                    }
                }
                onClicked: settingsPage.jumpTop()
            }
        }
    }

    // A row: title, summary, an optional reason it is greyed, a control on the right
    component PrefRow: ItemDelegate {
        id: prefRow
        property string title
        property string summary
        property string reason   // non-empty: not available here, greyed, with this reason
        property string valueText
        // Native (lane a11y-2): a one-sentence tip, shown while the pointer is over the row or the row has the
        // keyboard's or a gamepad's focus (Game Mode); tipFocus is a control inside the row having that focus
        property string tip
        property bool tipFocus: false
        default property alias trailing: trailingSlot.data
        property alias extra: extraSlot.data
        readonly property bool isPrefRow: true
        // false while the search at the top of the page does not match this row
        property bool searchHit: true
        visible: searchHit

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

        ToolTip.text: tip
        ToolTip.visible: tip !== "" && searchHit && (hovered || visualFocus || tipFocus)
        ToolTip.delay: visualFocus || tipFocus ? 0 : 500

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
        // Native (lane a11y-2): a tick mark under every step
        property bool ticks: false
        signal moved(real v)
        focusPolicy: Qt.NoFocus
        activeFocusOnTab: false
        tipFocus: slider.visualFocus
        extra: RowLayout {
            width: parent ? parent.width : 0
            Item {
                Layout.fillWidth: true
                implicitHeight: slider.implicitHeight
                implicitWidth: slider.implicitWidth
                Slider {
                    id: slider
                    anchors.fill: parent
                    from: sliderRow.from
                    to: sliderRow.to
                    stepSize: sliderRow.stepSize
                    snapMode: Slider.SnapAlways
                    value: sliderRow.value
                    enabled: sliderRow.enabled
                    activeFocusOnTab: true
                    onMoved: sliderRow.moved(value)
                }
                // under the track, one a step, the default's (or any value's) place easy to count
                Repeater {
                    id: tickMarks
                    model: sliderRow.ticks && sliderRow.stepSize > 0
                           ? Math.round((sliderRow.to - sliderRow.from) / sliderRow.stepSize) + 1 : 0
                    Rectangle {
                        readonly property real handleW: slider.handle ? slider.handle.width : 0
                        width: 2
                        height: 8
                        radius: 1
                        color: settingsPage.summaryColor
                        opacity: sliderRow.enabled ? 0.9 : 0.35
                        x: slider.leftPadding + handleW / 2
                           + index / Math.max(1, tickMarks.count - 1) * (slider.availableWidth - handleW) - width / 2
                        y: slider.topPadding + slider.availableHeight / 2 + 10
                    }
                }
            }
            Label {
                text: slider.value.toFixed(sliderRow.decimals) + sliderRow.suffix
                font.pointSize: 11
                Layout.minimumWidth: 80
                horizontalAlignment: Text.AlignRight
            }
        }
    }

    // Native (lane launcher-bundle, PLAN 6.7): rows for the VR client's config.ini keys in MlxrPrefs.vr, by
    // "section.key". objectName is the key, for the tests.
    component VrSwitch: PrefSwitch {
        property string key
        objectName: key
        isOn: MlxrPrefs.vr[key] === true
        onSwitched: function(on) { MlxrPrefs.setVr(key, on) }
    }

    // A list in the dialog; values are the VR client's names, names what the row says. A value the list leaves out
    // (one typed into config.ini) is shown as it is.
    component VrChoice: PrefRow {
        id: choiceRow
        property string key
        property var values: []
        property var names: []
        readonly property int current: values.indexOf(MlxrPrefs.vr[key])
        objectName: key
        valueText: current >= 0 ? names[current] : String(MlxrPrefs.vr[key])
        function pick(i) {
            MlxrPrefs.setVr(key, values[i])
        }
        onClicked: {
            var e = []
            for (var i = 0; i < names.length; i++) e.push({ text: names[i], reason: "" })
            listDialog.show(title, e, current, function(i) { choiceRow.pick(i) })
        }
    }

    // The slider shows the value divided by scale (the gamma is kept in hundredths)
    component VrSlider: PrefSlider {
        property string key
        property real scale: 1
        objectName: key
        value: MlxrPrefs.vr[key] / scale
        onMoved: function(v) { MlxrPrefs.setVr(key, Math.round(v * scale)) }
    }

    // Tests (MLXR_LAUNCHER_INVOKE): what a pick in a VrChoice row's dialog does, and a value set as a slider would
    function vrPick(key, index) {
        var kids = prefColumn.children
        for (var i = 0; i < kids.length; i++) {
            if (kids[i].objectName === key && kids[i].pick !== undefined) {
                kids[i].pick(Number(index))
                return
            }
        }
        console.warn("vrPick: no row " + key)
    }
    function vrSet(key, value) {
        MlxrPrefs.setVr(key, value)
    }
    // Native (lane a11y-2), tests: a row takes the focus as a gamepad gives it, and its tip is logged; and a
    // MlxrPrefs property set as a row would
    function showTip(name) {
        var kids = prefColumn.children
        for (var i = 0; i < kids.length; i++) {
            if (kids[i].objectName === name) {
                var row = kids[i]
                row.forceActiveFocus(Qt.TabFocus)
                console.info("MLXR_SETTINGS_TIP: " + name + " visible=" + row.ToolTip.visible + " text=\"" + row.ToolTip.text + "\"")
                return
            }
        }
        console.warn("showTip: no row " + name)
    }
    function setPref(name, value) {
        MlxrPrefs[name] = Number(value)
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

        // ======== Search and jump list ========
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 10
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            spacing: 8
            TextField {
                id: searchField
                objectName: "searchField"
                Layout.fillWidth: true
                placeholderText: qsTr("Search settings")
                inputMethodHints: Qt.ImhNoPredictiveText
                selectByMouse: true
                activeFocusOnTab: true
                onTextChanged: settingsPage.applySearch()
                Keys.onEscapePressed: text = ""
            }
            ToolButton {
                objectName: "clearSearch"
                visible: searchField.text !== ""
                focusPolicy: Qt.StrongFocus
                implicitWidth: 40
                implicitHeight: 40
                ToolTip.text: qsTr("Clear the search")
                ToolTip.visible: hovered
                ToolTip.delay: 800
                contentItem: Loader {
                    source: "qrc:/gui/XrIcon.qml"
                    onLoaded: {
                        item.name = "close"
                        item.color = settingsPage.summaryColor
                        item.size = 20
                    }
                }
                onClicked: searchField.text = ""
            }
        }

        Flow {
            id: jumpList
            objectName: "jumpList"
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            Layout.topMargin: 4
            spacing: 4
            Repeater {
                model: settingsPage.sections
                delegate: Button {
                    required property var modelData
                    objectName: "jump_" + (modelData.icon !== "" ? modelData.icon : modelData.title)
                    visible: modelData.shown
                    flat: true
                    focusPolicy: Qt.StrongFocus
                    activeFocusOnTab: true
                    leftPadding: 10
                    rightPadding: 12
                    topPadding: 6
                    bottomPadding: 6
                    contentItem: Row {
                        spacing: 7
                        Item {
                            width: 20
                            height: 20
                            anchors.verticalCenter: parent.verticalCenter
                            Loader {
                                anchors.fill: parent
                                source: "qrc:/gui/XrIcon.qml"
                                onLoaded: {
                                    item.name = modelData.icon
                                    item.color = settingsPage.accentColor
                                    item.size = 20
                                }
                            }
                        }
                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.title
                            font.pointSize: 10
                        }
                    }
                    onClicked: settingsPage.jumpTo(modelData.item)
                }
            }
        }

        Label {
            id: noMatchLabel
            visible: false
            Layout.fillWidth: true
            Layout.topMargin: 24
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            text: qsTr("No setting matches \"%1\".").arg(searchField.text)
            font.pointSize: 11
            color: settingsPage.summaryColor
            wrapMode: Text.Wrap
        }

        // ======== Connection (the owner's: the first thing on the page) ========
        PrefCategory { text: qsTr("Connection"); icon: "network"; showTop: false }

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

        // Native (lane vpn-fallback): a VPN address of the PC (100.64.x.x, a VPN interface) only when it does not
        // answer directly. Off: whichever address answers best.
        VrSwitch {
            key: "stream.vpn_only_away"
            title: qsTr("Use VPN addresses only away from home")
            summary: qsTr("When the PC also has a VPN address, it is used only when the PC does not answer on your home network. Off: whichever address answers fastest.")
        }

        // Native (lane polish-3): the stream's bitrate, over the per-connection rows whose "Same as above" is
        // this one (it was under Video settings, below them)
        PrefSlider {
            objectName: "bitrateRow"
            title: qsTr("Video bitrate")
            summary: qsTr("Up to 500 Mbps, for a good 5 GHz Wi-Fi or a wired link. Picking a resolution or a frame rate sets it back to their default. A stream's bitrate is chosen when it starts; the rows below can start one at a different bitrate over each connection, and Same as above is this one.")
            from: 0.5
            to: 500
            stepSize: 0.5
            decimals: 1
            suffix: " " + qsTr("Mbps")
            value: StreamingPreferences.bitrateKbps / 1000.0
            onMoved: function(v) { StreamingPreferences.bitrateKbps = Math.round(v * 1000) }
        }

        PrefRow {
            objectName: "bitrateWifiRow"
            title: qsTr("Bitrate over Wi-Fi")
            valueText: settingsPage.pathBitrateText(MlxrPrefs.bitrateWifi)
            onClicked: MlxrPrefs.bitrateWifi = settingsPage.nextPathBitrate(MlxrPrefs.bitrateWifi)
        }

        PrefRow {
            objectName: "bitrateAdapterRow"
            title: qsTr("Bitrate over the Steam Frame Wireless Adapter")
            valueText: settingsPage.pathBitrateText(MlxrPrefs.bitrateAdapter)
            onClicked: MlxrPrefs.bitrateAdapter = settingsPage.nextPathBitrate(MlxrPrefs.bitrateAdapter)
        }

        PrefRow {
            objectName: "bitrateWiredRow"
            title: qsTr("Bitrate over a wired network")
            valueText: settingsPage.pathBitrateText(MlxrPrefs.bitrateWired)
            onClicked: MlxrPrefs.bitrateWired = settingsPage.nextPathBitrate(MlxrPrefs.bitrateWired)
        }

        PrefRow {
            objectName: "bitrateUsbRow"
            title: qsTr("Bitrate over the USB cable")
            valueText: settingsPage.pathBitrateText(MlxrPrefs.bitrateUsb)
            onClicked: MlxrPrefs.bitrateUsb = settingsPage.nextPathBitrate(MlxrPrefs.bitrateUsb)
        }

        // Native (lane wave1-burst): the cap on the headset's full-frame requests after lost video. Pressing
        // the row steps No limit -> 250 ms -> 500 ms -> 1 s -> 2 s.
        PrefRow {
            objectName: "idrCapRow"
            title: qsTr("Full-frame requests after lost video")
            valueText: settingsPage.idrCapText(MlxrPrefs.rfiIdrMs)
            summary: qsTr("At most one request for a full picture in this time when video is lost. A full picture is large; asked for again while the Wi-Fi is still losing packets, it makes the freeze longer. 500 ms suits most links.")
            onClicked: MlxrPrefs.rfiIdrMs = settingsPage.nextLimit(settingsPage.idrCapValues, MlxrPrefs.rfiIdrMs)
        }

        // Native (lane wave1-failover): leave a link that keeps losing video for another one. Off until measured
        // on the Frame. The second row is the loss that counts, within 10 s.
        PrefSwitch {
            objectName: "pathFailoverRow"
            title: qsTr("Switch network when video keeps breaking up")
            summary: qsTr("With Automatic network connection: if the link the stream started on keeps losing video, move the stream to another link that measured clearly better when it started (the Steam Frame Wireless Adapter, Wi-Fi or the USB cable). It never goes to a link that was not there, and at most once in five minutes. The picture freezes for a few seconds while it reconnects.")
            isOn: MlxrPrefs.pathFailover
            onSwitched: function(on) { MlxrPrefs.pathFailover = on }
        }

        PrefRow {
            objectName: "failoverBurstRow"
            title: qsTr("Video loss before switching")
            valueText: settingsPage.failoverBurstText(MlxrPrefs.failoverBurstMs)
            summary: qsTr("How much lost video within ten seconds makes the stream leave its link. 3 s suits most places; shorter switches sooner, longer waits out brief dropouts.")
            reason: MlxrPrefs.pathFailover ? "" : qsTr("the switch above is off")
            onClicked: MlxrPrefs.failoverBurstMs = settingsPage.nextFailoverBurst(MlxrPrefs.failoverBurstMs)
        }

        // ======== Stream profiles ========
        PrefCategory { text: qsTr("Stream profiles"); icon: "profiles" }

        PrefRow {
            objectName: "profilesRow"
            title: qsTr("Profiles")
            valueText: MlxrProfiles.names.length === 0 ? qsTr("None yet") : MlxrProfiles.names.join(", ")
            summary: qsTr("Saved sets of these stream settings, such as a desktop profile at 1080p 60 Hz and a game profile at 1440p 120 Hz with a virtual display. A profile can be used for a PC or for one app: when its stream starts, the profile's settings are in force and the ones on this page come back afterwards.")
            onClicked: settingsPage.StackView.view.push("qrc:/gui/XrProfilesView.qml")
        }

        // ======== Video settings ========
        PrefCategory { text: qsTr("Video settings"); icon: "video" }

        PrefRow {
            id: resolutionRow
            objectName: "resolutionRow"
            title: qsTr("Video resolution")
            valueText: settingsPage.resolutionText()
            summary: qsTr("The size the PC renders and streams; the headset scales it to the screen. Changing it sets the Video bitrate to that size's default. Custom takes any size, such as 3840x1080 for full-size SBS.")
            onClicked: {
                var e = []
                for (var i = 0; i < settingsPage.resolutions.length; i++) {
                    var r = settingsPage.resolutions[i]
                    e.push({ text: r.text + " (" + r.w + "x" + r.h + ")", reason: "" })
                }
                e.push({ text: qsTr("Custom..."), reason: "" })
                var cur = settingsPage.resolutionIndex()
                listDialog.show(title, e, cur >= 0 ? cur : settingsPage.resolutions.length, function(i) {
                    if (i < settingsPage.resolutions.length) {
                        StreamingPreferences.width = settingsPage.resolutions[i].w
                        StreamingPreferences.height = settingsPage.resolutions[i].h
                        settingsPage.resetBitrate()
                    }
                    else {
                        settingsPage.editCustomResolution()
                    }
                })
            }
        }

        PrefRow {
            objectName: "refreshRow"
            title: qsTr("Refresh rate")
            valueText: settingsPage.rateText()
            summary: qsTr("How many frames per second the PC streams. Match headset uses the rate SteamVR runs the Steam Frame's display at for this app (90 Hz unless changed in SteamVR's settings). Picking 72, 90, 120 or 144 Hz also sets the headset to that rate in SteamVR's settings for this app. 120 and 144 Hz need the stream and the PC to keep up. The stream never goes above the headset's rate.") +
                     " " + qsTr("Custom takes a rate such as 59.94 and needs Apollo.")
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
                        editDialog.show(qsTr("Custom refresh rate"), qsTr("A rate such as 59.94, at most the headset's %1 Hz.").arg(MlxrPrefs.headsetMaxFps) + " " + settingsPage.apolloNote,
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

        PrefRow {
            objectName: "pacingRow"
            title: qsTr("Video frame pacing")
            valueText: settingsPage.pacingNames[Math.max(0, settingsPage.pacingValues.indexOf(MlxrPrefs.framePacing))]
            summary: qsTr("Lowest latency shows the newest frame at once; smoothest shows every frame in turn, a frame or two later; balanced is in between.")
            onClicked: {
                var e = []
                for (var i = 0; i < settingsPage.pacingNames.length; i++) e.push({ text: settingsPage.pacingNames[i], reason: "" })
                listDialog.show(title, e, settingsPage.pacingValues.indexOf(MlxrPrefs.framePacing), function(i) {
                    MlxrPrefs.framePacing = settingsPage.pacingValues[i]
                })
            }
        }

        PrefSwitch {
            objectName: "ullRow"
            title: qsTr("Ultra low latency (experimental)")
            summary: qsTr("Runs the stream's network and decoder threads at realtime priority on the Frame.")
            isOn: MlxrPrefs.ultraLowLatency
            onSwitched: function(on) { MlxrPrefs.ultraLowLatency = on }
        }

        PrefSwitch {
            objectName: "virtualDisplayRow"
            title: qsTr("Use virtual display")
            summary: qsTr("The PC makes a display at the stream's size and rate for this headset and leaves its own monitors alone.") + " " + settingsPage.apolloNote
            isOn: MlxrPrefs.virtualDisplay
            onSwitched: function(on) { MlxrPrefs.virtualDisplay = on }
        }

        PrefSlider {
            objectName: "scaleFactorRow"
            title: qsTr("Resolution scale factor")
            summary: qsTr("The virtual display is made this much of the stream's size (at 120%, 2000x1000 becomes 2400x1200); the stream itself keeps its size.") + " " + settingsPage.apolloNote
            from: 20
            to: 200
            stepSize: 1
            suffix: "%"
            value: MlxrPrefs.scaleFactor
            onMoved: function(v) { MlxrPrefs.scaleFactor = Math.round(v) }
        }

        // The Video bitrate typed in (Artemis's row; the owner keeps it here)
        PrefRow {
            objectName: "customBitrateRow"
            title: qsTr("Custom bitrate")
            valueText: (StreamingPreferences.bitrateKbps / 1000.0) + " " + qsTr("Mbps")
            summary: qsTr("The Video bitrate under Connection, typed in Mbps (0.5 to 500).")
            onClicked: editDialog.show(title, qsTr("Mbps, 0.5 to 500"), "" + (StreamingPreferences.bitrateKbps / 1000.0),
                                       "^\\s*\\d{1,3}(\\.\\d{1,3})?\\s*$", function(t) {
                var v = parseFloat(t)
                if (v >= 0.5 && v <= 500) StreamingPreferences.bitrateKbps = Math.round(v * 1000)
            })
        }

        // Artemis's Render Mode ([3d] stereo), named as the bar's button and the cog's tab
        PrefSwitch {
            objectName: "renderModeRow"
            title: qsTr("3D")
            summary: qsTr("The bar's 3D button switches it too.")
            isOn: MlxrPrefs.renderMode === 1
            onSwitched: function(on) { MlxrPrefs.renderMode = on ? 1 : 0 }
        }

        // Native (PLAN 4.3): the headset's 3D tab's 3D source row; pressing steps Depth -> SBS -> Top-bottom
        PrefRow {
            objectName: "stereoSourceRow"
            title: qsTr("3D source")
            valueText: settingsPage.stereoSourceNames[settingsPage.stereoSourceIndex()]
            summary: qsTr("Depth model: the headset makes the 3D from a 2D stream. SBS or Top-bottom: the PC renders the two eyes itself (a 3D film, a game's own SBS mode, ReShade SuperDepth3D) and each eye sees its half; no depth model runs and 3D convergence moves the halves apart or together.")
            onClicked: MlxrPrefs.stereoSource = settingsPage.stereoSourceValues[(settingsPage.stereoSourceIndex() + 1) % settingsPage.stereoSourceValues.length]
        }

        PrefSwitch {
            objectName: "sbsFullRow"
            title: qsTr("Full-size SBS")
            summary: qsTr("On: the PC's pair holds a full picture per eye (a 3840x1080 custom resolution for two 1920x1080 eyes). Off: half-size, each eye squeezed into half of a 1920x1080 frame.")
            reason: MlxrPrefs.stereoSource === "depth" ? qsTr("3D source is the depth model") : ""
            isOn: MlxrPrefs.sbsFull
            onSwitched: function(on) { MlxrPrefs.sbsFull = on }
        }

        VrChoice {
            key: "vr.depth_source"
            title: qsTr("Depth model")
            reason: MlxrPrefs.stereoSource === "depth" ? "" : qsTr("3D source is the PC's own pair")
            values: ["zipdepth", "zipdepth-int8", "midas", "off"]
            names: [qsTr("ZipDepth"), qsTr("ZipDepth, lighter on the CPU"), qsTr("MiDaS (older)"), qsTr("Off (2D only)")]
        }

        PrefSlider {
            objectName: "strengthRow"
            title: qsTr("3D depth")
            summary: qsTr("How far apart the two eyes' pictures are: higher is a stronger 3D effect and can tire the eyes.") +
                     (MlxrPrefs.strengthIsDefault ? " " + qsTr("(Now: the depth model's own.)") : "")
            reason: MlxrPrefs.renderMode !== 1 ? qsTr("3D is off")
                    : MlxrPrefs.stereoSource !== "depth" ? qsTr("3D source is the PC's own pair") : ""
            from: 1
            to: 100
            value: MlxrPrefs.strength
            onMoved: function(v) { MlxrPrefs.strength = Math.round(v); MlxrPrefs.strengthIsDefault = false }
        }

        PrefSlider {
            objectName: "convergenceRow"
            title: qsTr("3D convergence")
            summary: qsTr("Where the picture sits in depth: 0 in front of the screen, 50 balanced, 100 behind it.") +
                     (MlxrPrefs.convergenceIsDefault ? " " + qsTr("(Now: the depth model's own.)") : "")
            reason: MlxrPrefs.renderMode === 1 ? "" : qsTr("3D is off")
            from: 0
            to: 100
            value: MlxrPrefs.convergence
            onMoved: function(v) { MlxrPrefs.convergence = Math.round(v); MlxrPrefs.convergenceIsDefault = false }
        }

        // Native (lane wave5-depth): how closely the depth keeps to the edges in the picture. Pressing the
        // row steps 0, 25, 50, 75, 100 ([3d] edge_sharpness of the VR client's config.ini)
        PrefRow {
            objectName: "edgeRow"
            title: qsTr("3D edge")
            valueText: "" + MlxrPrefs.edgeSharpness
            summary: qsTr("Higher keeps hair, outlines and thin text on their own depth instead of blending them into what is behind; 0 is the depth model's soft edges. Too high can make edges look cut out.")
            reason: MlxrPrefs.renderMode === 1 ? "" : qsTr("3D is off")
            onClicked: if (enabled) MlxrPrefs.edgeSharpness = settingsPage.nextEdge()
        }

        // Native (lane wave5-depth): the depth's smoothing over time restarts where the picture moved, so a
        // fast edge leaves no trail. Pressing the row steps Off, 5, 10, 20, 30 percent ([3d] motion_reset)
        PrefRow {
            objectName: "motionRow"
            title: qsTr("3D motion reset")
            valueText: MlxrPrefs.motionReset === 0 ? qsTr("Off") : (MlxrPrefs.motionReset + "%")
            summary: qsTr("For fast scenes: where the picture changes by more than this between two depth maps, the 3D stops smoothing the old depth into the new there, so moving edges leave no trail. Lower reacts to smaller changes; too low brings back flicker.")
            reason: MlxrPrefs.renderMode === 1 ? "" : qsTr("3D is off")
            onClicked: if (enabled) MlxrPrefs.motionReset = settingsPage.nextMotion()
        }

        // Native (lane wave5-depth): upstream's depth scale, and the overall shape's gain beside it. Pressing a
        // row steps 50, 75, 100, 125, 150, 200 percent ([3d] depth_scale, depth_global)
        PrefRow {
            objectName: "depthScaleRow"
            title: qsTr("3D depth detail")
            valueText: MlxrPrefs.depthScale + "%"
            summary: qsTr("The depth between things near each other, like a face in front of a wall. Higher makes them stand apart more, without pushing the whole picture further in or out; 100% is as the model makes it.")
            reason: MlxrPrefs.renderMode === 1 ? "" : qsTr("3D is off")
            onClicked: if (enabled) MlxrPrefs.depthScale = settingsPage.nextDepthStep(MlxrPrefs.depthScale)
        }

        PrefRow {
            objectName: "depthGlobalRow"
            title: qsTr("3D depth shape")
            valueText: MlxrPrefs.depthGlobal + "%"
            summary: qsTr("The depth of the picture as a whole, from the near ground to the far sky. Lower flattens it toward the screen, which is easier on the eyes, while 3D depth detail keeps things standing apart.")
            reason: MlxrPrefs.renderMode === 1 ? "" : qsTr("3D is off")
            onClicked: if (enabled) MlxrPrefs.depthGlobal = settingsPage.nextDepthStep(MlxrPrefs.depthGlobal)
        }

        PrefRow {
            objectName: "eyeBalanceRow"
            title: qsTr("3D eye balance")
            valueText: MlxrPrefs.eyeBalance > 0 ? qsTr("Left eye %1%").arg(MlxrPrefs.eyeBalance)
                     : MlxrPrefs.eyeBalance < 0 ? qsTr("Right eye %1%").arg(-MlxrPrefs.eyeBalance) : qsTr("Even")
            summary: qsTr("How the 3D's shift is shared by the two eyes. Even moves each eye the same; at 100% only the left eye's picture moves and the right eye's is as streamed. The depth you see is the same at every setting. Useful with a dominant eye.")
            reason: MlxrPrefs.renderMode === 1 ? "" : qsTr("3D is off")
            onClicked: if (enabled) MlxrPrefs.eyeBalance = settingsPage.nextLimit(settingsPage.eyeBalanceSteps, MlxrPrefs.eyeBalance)
        }

        VrChoice {
            key: "display.sharpening"
            title: qsTr("Sharpen")
            values: ["off", "normal", "quality"]
            names: [qsTr("Off"), qsTr("Normal"), qsTr("Quality")]
        }

        VrChoice {
            key: "display.supersampling"
            title: qsTr("Supersample")
            summary: qsTr("Not used while the 3D is on.")
            values: ["off", "normal", "quality"]
            names: [qsTr("Off"), qsTr("Normal"), qsTr("Quality")]
        }

        VrSwitch {
            key: "display.picture_filter"
            title: qsTr("Sharper small text")
            summary: qsTr("On a curved or 3D screen: averages the stream under each of the headset's pixels.")
        }

        VrSwitch {
            key: "display.chroma_from_luma"
            title: qsTr("Sharper coloured text")
            summary: qsTr("On a curved or 3D screen: keeps coloured text in colour to its edge.")
        }

        VrSlider {
            key: "vr.picture_brightness"
            title: qsTr("Picture brightness")
            from: -50
            to: 50
        }

        VrSlider {
            key: "vr.picture_contrast"
            title: qsTr("Picture contrast")
            from: 50
            to: 150
            suffix: "%"
        }

        VrSlider {
            key: "vr.picture_gamma"
            title: qsTr("Picture gamma")
            scale: 100
            from: 0.5
            to: 2.0
            stepSize: 0.05
            decimals: 2
        }

        VrSlider {
            key: "vr.picture_saturation"
            title: qsTr("Picture saturation")
            from: 0
            to: 200
            suffix: "%"
        }

        // ======== Audio settings ========
        PrefCategory { text: qsTr("Audio settings"); icon: "audio" }

        PrefRow {
            objectName: "audioConfigRow"
            title: qsTr("Surround sound configuration")
            valueText: settingsPage.audioNames[StreamingPreferences.audioConfig]
            summary: qsTr("What the PC sends. 5.1 and 7.1 are mixed down to stereo unless Virtual surround places them around you.")
            onClicked: {
                var e = []
                for (var i = 0; i < settingsPage.audioNames.length; i++) e.push({ text: settingsPage.audioNames[i], reason: "" })
                listDialog.show(title, e, StreamingPreferences.audioConfig, function(i) { StreamingPreferences.audioConfig = i })
            }
        }

        VrSwitch {
            key: "stream.virtual_surround"
            title: qsTr("Virtual surround")
            summary: qsTr("Places stereo, 5.1 and 7.1 speakers around you, held to the screen.")
        }

        // ======== Gamepad settings ========
        PrefCategory { text: qsTr("Gamepad settings"); icon: "gamepad" }

        PrefSlider {
            objectName: "deadzoneRow"
            title: qsTr("Adjust analog stick dead zone")
            summary: qsTr("Above 0 a dead zone; below 0 compensates for the game's own, which can make centring hard. At 0 a gamepad's sticks are sent as read and the Frame controllers in gamepad mode use 7%.")
            from: -20
            to: 20
            suffix: "%"
            value: MlxrPrefs.deadzone
            onMoved: function(v) { MlxrPrefs.deadzone = Math.round(v) }
        }

        PrefSwitch {
            objectName: "flipRow"
            title: qsTr("Flip face buttons")
            summary: qsTr("Swaps A/B and X/Y on gamepads and in the headset's gamepad mode.")
            isOn: MlxrPrefs.flipFaceButtons
            onSwitched: function(on) { MlxrPrefs.flipFaceButtons = on }
        }

        // Native (lane headinput-finish, PLAN 4b.10; back here from Accessibility, lane a11y-2)
        PrefSwitch {
            objectName: "backAsGuideRow"
            title: qsTr("Back button as Guide")
            summary: qsTr("In the headset's gamepad mode the controllers' View (Back) button is the Xbox Guide button. With Start held it stays Back, so the Start and Back chords still work.")
            isOn: MlxrPrefs.backAsGuide
            onSwitched: function(on) { MlxrPrefs.backAsGuide = on }
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
            summary: qsTr("A picture of the gamepad you are playing with, in your lap, that lights up as you press it. Automatic: up while you play, gone 4 s after you stop. On: up while a gamepad is in use. Off: never.")
            onClicked: MlxrPrefs.padCard = settingsPage.padCardValues[(settingsPage.padCardIndex() + 1) % settingsPage.padCardValues.length]
        }

        PrefRow {
            objectName: "lightgunRow"
            title: qsTr("Lightgun mode")
            valueText: settingsPage.lightgunNames[Math.max(0, settingsPage.lightgunValues.indexOf(MlxrPrefs.lightgun))]
            summary: qsTr("Aim like a lightgun: the pointer never sleeps, clicks go out exactly where you aim, only the grip takes hold of the screen and a shot pulses the controller. Automatic turns it on while the headset draws the Gun.")
            onClicked: {
                var e = []
                for (var i = 0; i < settingsPage.lightgunNames.length; i++) e.push({ text: settingsPage.lightgunNames[i], reason: "" })
                listDialog.show(title, e, settingsPage.lightgunValues.indexOf(MlxrPrefs.lightgun), function(i) {
                    MlxrPrefs.lightgun = settingsPage.lightgunValues[i]
                })
            }
        }

        VrSwitch {
            key: "vr.lightgun_reload"
            title: qsTr("Lightgun reload")
            summary: qsTr("A trigger pulled off the picture is a right click.")
            reason: MlxrPrefs.lightgun === "off" ? qsTr("Lightgun mode is Off") : ""
        }

        VrSwitch {
            key: "vr.lightgun_recoil"
            title: qsTr("Lightgun recoil")
            summary: qsTr("A short pulse in the controller on every shot.")
            reason: MlxrPrefs.lightgun === "off" ? qsTr("Lightgun mode is Off") : ""
        }

        PrefRow {
            objectName: "padGyroRow"
            title: qsTr("Gyro from the headset")
            valueText: settingsPage.padGyroNames[Math.max(0, settingsPage.padGyroValues.indexOf(MlxrPrefs.padGyro))]
            summary: qsTr("Send the turn of the right controller, or of your head, to the PC as the gyro of a PlayStation gamepad while the controllers are the gamepad, for gyro aiming in Steam Input. The PC then sees a DualShock 4, with PlayStation button names.")
            onClicked: {
                var e = []
                for (var i = 0; i < settingsPage.padGyroNames.length; i++) e.push({ text: settingsPage.padGyroNames[i], reason: "" })
                listDialog.show(title, e, settingsPage.padGyroValues.indexOf(MlxrPrefs.padGyro), function(i) {
                    MlxrPrefs.padGyro = settingsPage.padGyroValues[i]
                })
            }
        }

        VrChoice {
            key: "vr.pad_gyro_ratchet"
            title: qsTr("When the gyro is sent")
            reason: MlxrPrefs.padGyro === "off" ? qsTr("Gyro from the headset is Off") : ""
            values: ["always", "stick_touch"]
            names: [qsTr("Always"), qsTr("While the thumb touches the right stick")]
        }

        VrChoice {
            key: "vr.gamepad_toggle"
            title: qsTr("Gamepad mode shortcut")
            values: ["menu_grip", "sticks", "triggers_grips"]
            names: [qsTr("Left View and left grip"), qsTr("Click both thumbsticks"), qsTr("Both triggers and both grips")]
        }

        VrChoice {
            key: "vr.pad_grip"
            title: qsTr("Grips in gamepad mode")
            values: ["bumpers", "none", "stick_clicks"]
            names: [qsTr("Bumpers (LB, RB)"), qsTr("Nothing"), qsTr("Stick clicks (LS, RS)")]
        }

        PrefSwitch {
            objectName: "rumbleRow"
            title: qsTr("Enable rumble")
            summary: qsTr("Passes the PC's rumble to gamepads and to the headset controllers.")
            isOn: MlxrPrefs.rumble
            onSwitched: function(on) { MlxrPrefs.rumble = on }
        }

        VrChoice {
            key: "vr.rumble_mix"
            title: qsTr("Rumble on the headset controllers")
            reason: MlxrPrefs.rumble ? "" : qsTr("Enable rumble is off")
            values: ["both", "split"]
            names: [qsTr("Stronger motor in both hands"), qsTr("Low motor left, high motor right")]
        }

        VrSlider {
            key: "vr.rumble_intensity"
            title: qsTr("Rumble strength on the headset controllers")
            reason: MlxrPrefs.rumble ? "" : qsTr("Enable rumble is off")
            from: 0
            to: 100
            stepSize: 5
            suffix: "%"
        }

        // ======== Input settings ========
        PrefCategory { text: qsTr("Input settings"); icon: "input" }

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

        VrChoice {
            key: "vr.keyboard_layout"
            title: qsTr("Headset keyboard size")
            values: ["compact", "full"]
            names: [qsTr("Compact"), qsTr("Full (tenkeyless)")]
        }

        VrSlider {
            key: "vr.keyboard_hold_ms"
            title: qsTr("Hold to lock Shift, Ctrl, Alt or Win")
            summary: qsTr("How long to hold one of them to lock it, on both keyboards.")
            from: 200
            to: 1500
            stepSize: 50
            suffix: " ms"
        }

        VrSwitch {
            key: "vr.pointer"
            title: qsTr("Controllers as a mouse")
            summary: qsTr("Off leaves only the switch to gamepad mode.")
        }

        // Native (lane headinput-finish, PLAN 4b.10): pressing the row steps Absolute -> Relative
        PrefRow {
            objectName: "mouseModeRow"
            title: qsTr("Mouse mode")
            valueText: MlxrPrefs.mouseMode === "relative" ? qsTr("Relative") : qsTr("Absolute")
            summary: qsTr("Absolute puts the PC's cursor where the pointer lands on the picture, as a remote desktop does. Relative sends how far the pointer moves instead, for games that capture the mouse and ignore where it is.")
            onClicked: MlxrPrefs.mouseMode = MlxrPrefs.mouseMode === "relative" ? "absolute" : "relative"
        }

        // The cog's Ray row: Off, Dot (the dot where the ray lands) or Ray (the ray and the dot)
        PrefRow {
            id: rayRow
            objectName: "rayRow"
            title: qsTr("Ray")
            valueText: settingsPage.rayNames[settingsPage.rayIndex()]
            summary: qsTr("Dot shows only where the ray lands.")
            function pick(i) {
                MlxrPrefs.setVr("vr.show_ray", Number(i) >= 2)
                MlxrPrefs.setVr("vr.pointer_dot", Number(i) >= 1)
            }
            onClicked: {
                var e = []
                for (var i = 0; i < settingsPage.rayNames.length; i++) e.push({ text: settingsPage.rayNames[i], reason: "" })
                listDialog.show(title, e, settingsPage.rayIndex(), function(i) { rayRow.pick(i) })
            }
        }

        VrSwitch {
            key: "vr.pointer_sleep"
            title: qsTr("Pointer sleep")
            summary: qsTr("The pointer rests while the controller is still.")
        }

        VrChoice {
            key: "vr.click_steadiness"
            title: qsTr("Click steadiness")
            summary: qsTr("How still the cursor is held around a click.")
            values: ["off", "normal", "steady"]
            names: [qsTr("Off"), qsTr("Normal"), qsTr("Steady")]
        }

        VrSwitch {
            key: "vr.click_sound"
            title: qsTr("Click sound")
        }

        // The controllers' thumbstick wheel in the headset: pressing the row steps the speed
        PrefRow {
            objectName: "scrollSpeedRow"
            title: qsTr("Scroll speed")
            valueText: MlxrPrefs.scrollSpeed + "%"
            summary: qsTr("How fast a controller's thumbstick scrolls in pointer mode. Tilt it sideways to scroll sideways.")
            onClicked: MlxrPrefs.scrollSpeed = settingsPage.nextScrollSpeed()
        }

        // Pressing the row steps Off -> Frame -> Gun -> Your own model
        PrefRow {
            objectName: "controllerModelRow"
            title: qsTr("Show controllers")
            valueText: settingsPage.modelNames[settingsPage.modelIndex()]
            summary: qsTr("The controllers drawn in the headset. Your own model reads custom_left.model and custom_right.model (or one custom.model, mirrored for the left hand) from the models folder next to the headset's config file, and falls back to the Steam Frame controllers when they are missing.")
            onClicked: MlxrPrefs.controllerModel = settingsPage.modelValues[(settingsPage.modelIndex() + 1) % settingsPage.modelValues.length]
        }

        PrefRow {
            objectName: "bindingsRow"
            title: qsTr("Controller bindings")
            summary: qsTr("What each button on the Frame's controllers does, in pointer mode and in gamepad mode, for every app or for one app. Saved in bindings.ini; takes effect when the next stream starts.")
            onClicked: settingsPage.StackView.view.push("qrc:/gui/XrBindingsView.qml")
        }

        PrefSwitch {
            objectName: "mouseNavRow"
            title: qsTr("Enable forward/back mouse buttons")
            summary: qsTr("Sends a physical mouse's back and forward buttons to the PC.")
            isOn: MlxrPrefs.mouseNavButtons
            onSwitched: function(on) { MlxrPrefs.mouseNavButtons = on }
        }

        VrSwitch {
            key: "vr.head_aim"
            title: qsTr("Head aim")
            summary: qsTr("Your head moves the mouse, with the screen head locked and outside a room.")
        }

        VrSlider {
            key: "vr.head_aim_sensitivity"
            title: qsTr("Aim sensitivity")
            from: 1
            to: 30
            suffix: " px/°"
        }

        VrSlider {
            key: "vr.head_aim_deadzone"
            title: qsTr("Aim dead zone")
            from: 0
            to: 20
            suffix: " °/s"
        }

        // Native (lane wave4-headinput): head-look, off by default (lane a11y-2: back under Input, named for
        // what it does). It shares head aim's dead zone, so it follows the head aim rows
        PrefSwitch {
            objectName: "headLookRow"
            title: qsTr("Head turns the game's camera (first-person games)")
            summary: qsTr("Turning your head moves the PC's mouse like a game's look control, for first-person games streamed in side by side 3D. Works in pointer mode with the screen locked to your head or in a room. Hold a grip (one bound to nothing) to pause it while you turn back. Head aim wins where both are on. Experimental.")
            isOn: MlxrPrefs.headLook
            onSwitched: function(on) { MlxrPrefs.headLook = on }
        }

        // Pressing the row steps through the speeds; one typed into config.ini that is not listed steps to the first
        PrefRow {
            objectName: "headLookPxRow"
            readonly property var steps: [4, 6, 8, 12, 16, 24, 30]
            title: qsTr("Head camera speed")
            valueText: qsTr("%1 px a degree").arg(MlxrPrefs.headLookPxDeg)
            summary: qsTr("How far the mouse moves for each degree your head turns while the head turns the game's camera. Its dead zone is head aim's.")
            onClicked: MlxrPrefs.headLookPxDeg = settingsPage.nextLimit(steps, MlxrPrefs.headLookPxDeg)
        }

        VrSwitch {
            key: "vr.gaze"
            title: qsTr("Look to point")
            summary: qsTr("The pointer follows your eyes while SteamVR gives eye tracking to apps; a press on a controller takes it back.")
        }

        VrSwitch {
            key: "vr.hand_tracking"
            title: qsTr("Hand tracking")
            summary: qsTr("Point with your hands; a pinch of thumb and index finger clicks. Three quick pinches lock the hands out.")
        }

        // Native (lane wave4-headinput): head tracking to opentrack, off by default (head-look, What points and the
        // mouse mode are under Accessibility)
        PrefSwitch {
            objectName: "opentrackRow"
            title: qsTr("Head tracking to opentrack")
            summary: qsTr("While streaming, sends your head's position and turn to opentrack on the PC every frame, for games with head tracking (TrackIR, FreeTrack). In opentrack choose the input \"UDP over network\" with the port below.")
            isOn: MlxrPrefs.opentrack
            onSwitched: function(on) { MlxrPrefs.opentrack = on }
        }

        PrefRow {
            objectName: "opentrackHostRow"
            title: qsTr("opentrack address")
            valueText: MlxrPrefs.opentrackHost === "" ? qsTr("The streaming PC") : MlxrPrefs.opentrackHost
            summary: qsTr("Where the head tracking goes: an address or a name. Leave it empty for the PC you are streaming from.")
            onClicked: editDialog.show(title, qsTr("Address or name, empty for the streaming PC"), MlxrPrefs.opentrackHost,
                                       "^\\s*[A-Za-z0-9_.:%-]*\\s*$", function(t) {
                MlxrPrefs.opentrackHost = t.trim()
            })
        }

        PrefRow {
            objectName: "opentrackPortRow"
            title: qsTr("opentrack port")
            valueText: "" + MlxrPrefs.opentrackPort
            summary: qsTr("The UDP port opentrack's UDP over network input listens on (4242 unless you changed it).")
            onClicked: editDialog.show(title, qsTr("1 to 65535"), "" + MlxrPrefs.opentrackPort,
                                       "^\\s*[0-9]{1,5}\\s*$", function(t) {
                var p = parseInt(t.trim(), 10)
                MlxrPrefs.opentrackPort = p >= 1 && p <= 65535 ? p : 4242
            })
        }

        // ======== Native (lane a11y-tab, a11y-2): Accessibility ========
        // The same rows as the headset's Accessibility tab (cog), for the person setting it up. Nothing here
        // turns on by itself: each row writes its key only when it is pressed or moved. Each row's tip (hover,
        // or the focus of a keyboard or a gamepad in Game Mode) is the sentence the headset's footer shows.
        PrefCategory { text: qsTr("Accessibility"); icon: "accessibility" }

        // Pressing the row steps Controller -> Head -> Eyes
        PrefRow {
            objectName: "pointerSourceRow"
            readonly property var values: ["controller", "head", "gaze"]
            readonly property var names: [qsTr("Controller"), qsTr("Head"), qsTr("Eyes")]
            title: qsTr("Pointer")
            tip: qsTr("What moves the pointer: a controller, your head, or where you look.")
            valueText: names[Math.max(0, values.indexOf(MlxrPrefs.pointerSource))]
            summary: qsTr("For people who cannot use their hands or have limited mobility. Head: a dot follows the middle of your view and either controller's trigger clicks. Eyes: the dot follows your eyes while SteamVR gives eye tracking to apps (the headset's Accessibility tab says whether it does) and your head while it does not. Controller turns it off.")
            onClicked: MlxrPrefs.pointerSource = values[(Math.max(0, values.indexOf(MlxrPrefs.pointerSource)) + 1) % values.length]
        }

        // Pressing the row steps Off -> 0.6 s -> 0.9 s -> 1.2 s -> 1.5 s; a time typed into config.ini that is
        // not listed steps to Off
        PrefRow {
            objectName: "headDwellRow"
            readonly property var steps: [0, 600, 900, 1200, 1500]
            title: qsTr("Dwell click")
            tip: qsTr("Rest the dot this long on something to click it.")
            valueText: MlxrPrefs.headDwellMs <= 0 ? qsTr("Off") : qsTr("%1 s").arg((MlxrPrefs.headDwellMs / 1000).toFixed(1))
            summary: qsTr("With the Head or Eyes pointer: hold the dot still on a button or on the picture for this long and it clicks by itself; move away and back to click again. 0.9 s is a good start, 0.6 s once practised.")
            onClicked: {
                var i = steps.indexOf(MlxrPrefs.headDwellMs)
                MlxrPrefs.headDwellMs = steps[(i + 1) % steps.length]
            }
        }

        // Lane a11y-2: 50 to 300 % of the dot's usual size, in 25 % steps with a tick at each
        PrefSlider {
            id: dotSizeRow
            objectName: "assistDotSizeRow"
            title: qsTr("Dot size")
            tip: qsTr("How big the dot is when you point with your head or eyes.")
            summary: qsTr("The dot and the ring of the dwell click, with the Head or Eyes pointer. 100 % is the usual size.")
            from: 50
            to: 300
            stepSize: 25
            ticks: true
            suffix: " %"
            value: MlxrPrefs.assistDotSize
            onMoved: function(v) { MlxrPrefs.assistDotSize = Math.round(v) }
        }

        // Lane a11y-2: off by default, since it takes a system button from SteamVR while it is on
        PrefSwitch {
            objectName: "assistHeadsetButtonRow"
            title: qsTr("Headset button clicks")
            tip: qsTr("The headset's side button clicks instead of opening the Steam home. Hold it 3 seconds to get the Steam home back.")
            summary: qsTr("With the Head or Eyes pointer: press once to click, twice to double click, three times to right click, hold to drag. Holding it 3 seconds gives it back to SteamVR for 15 seconds. The app needs read access to the button's input device; the headset's Accessibility tab says when it does not have it.")
            isOn: MlxrPrefs.assistHeadsetButton
            onSwitched: function(on) { MlxrPrefs.assistHeadsetButton = on }
        }

        // ======== Host settings ========
        PrefCategory { text: qsTr("Host settings"); icon: "host" }

        PrefSwitch {
            objectName: "preflightRow"
            title: qsTr("Check the PC before streaming")
            summary: qsTr("Asks the PC just before a stream starts and says if something will go wrong: this client's Apollo permissions, a virtual display that is not ready, another app already running, a resolution the PC does not list. Not part of a profile.")
            isOn: MlxrPrefs.preflight
            onSwitched: function(on) { MlxrPrefs.preflight = on }
        }

        PrefSwitch {
            objectName: "gameOptsRow"
            title: qsTr("Optimize game settings")
            summary: qsTr("Lets the PC switch its display to the stream's resolution, where the host is set to.")
            isOn: StreamingPreferences.gameOptimizations
            onSwitched: function(on) { StreamingPreferences.gameOptimizations = on }
        }

        PrefSwitch {
            objectName: "hostAudioRow"
            title: qsTr("Play audio on PC")
            summary: qsTr("The game's sound plays on the PC's speakers as well as in the headset.")
            isOn: StreamingPreferences.playAudioOnHost
            onSwitched: function(on) { StreamingPreferences.playAudioOnHost = on }
        }

        // ======== General settings ========
        PrefCategory { text: qsTr("General settings"); icon: "general" }

        PrefSwitch {
            objectName: "clipboardRow"
            title: qsTr("Smart clipboard sync (experimental)")
            summary: qsTr("Copies the Frame's clipboard to the PC when a stream starts, and the PC's back when it ends.") + " " + settingsPage.apolloNote
            isOn: MlxrPrefs.clipboardSync
            onSwitched: function(on) { MlxrPrefs.clipboardSync = on }
        }

        // Native (lane launcher-bundle, PLAN 6.4): what the launcher opens by itself once the PC is online
        PrefRow {
            objectName: "onStartRow"
            title: qsTr("On start")
            valueText: settingsPage.onStartText()
            summary: qsTr("Starts the stream as soon as the PC is online.")
            onClicked: {
                var e = []
                for (var i = 0; i < settingsPage.onStartNames.length; i++) e.push({ text: settingsPage.onStartNames[i], reason: "" })
                listDialog.show(title, e, Math.max(0, settingsPage.onStartValues.indexOf(MlxrPrefs.onStart)), function(i) {
                    settingsPage.pickOnStart(i)
                })
            }
        }

        PrefRow {
            objectName: "onStartAppRow"
            title: qsTr("App to open on start")
            visible: searchHit && MlxrPrefs.onStart === "app"
            valueText: MlxrPrefs.onStartApp !== "" ? qsTr("%1 on %2").arg(MlxrPrefs.onStartApp).arg(MlxrPrefs.onStartPc) : qsTr("None chosen")
            onClicked: {
                var c = MlxrPrefs.onStartChoices()
                var e = []
                var cur = -1
                for (var i = 0; i < c.length; i++) {
                    e.push({ text: qsTr("%1 on %2").arg(c[i].app).arg(c[i].pc), reason: "" })
                    if (c[i].pc === MlxrPrefs.onStartPc && c[i].app === MlxrPrefs.onStartApp) cur = i
                }
                listDialog.show(title, e, cur, function(i) { settingsPage.pickOnStartApp(c[i].pc, c[i].app) })
            }
        }

        // Native (lane launcher-bundle, PLAN 6.2)
        PrefSwitch {
            objectName: "checkUpdatesRow"
            title: qsTr("Check for updates")
            summary: qsTr("Once a day, asks GitHub whether a newer release is out. Nothing is downloaded or installed.")
            isOn: MlxrPrefs.checkUpdates
            onSwitched: function(on) { MlxrPrefs.checkUpdates = on }
        }

        // Native (lane wave3-cards): the first-run controls card comes up once; this brings it back
        PrefSwitch {
            objectName: "firstRunRow"
            title: qsTr("Controls card on the next stream")
            summary: qsTr("The card that shows the headset's controls when a stream starts. It comes up once by itself; switch this on to see it again.")
            isOn: !MlxrPrefs.firstRunShown
            onSwitched: function(on) { MlxrPrefs.firstRunShown = !on }
        }

        // The headset coming off: the sound mutes and the input is let go at once; after the time the stream
        // is disconnected and the PC's app keeps running. Pressing a row steps through the choices.
        PrefRow {
            objectName: "doffRow"
            title: qsTr("Disconnect when the headset is off")
            valueText: MlxrPrefs.doffDisconnect === 0 ? qsTr("Never") : settingsPage.timeText(MlxrPrefs.doffDisconnect * 1000)
            summary: qsTr("How long the headset can stay off before the stream is disconnected. The app keeps running on the PC. Putting the headset back on sooner carries on.")
            onClicked: MlxrPrefs.doffDisconnect = settingsPage.nextLimit(settingsPage.doffValues, MlxrPrefs.doffDisconnect)
        }

        PrefRow {
            objectName: "idleRow"
            title: qsTr("Disconnect when idle")
            valueText: MlxrPrefs.idleDisconnect === 0 ? qsTr("Never") : settingsPage.timeText(MlxrPrefs.idleDisconnect * 60000)
            summary: qsTr("How long without input from the controllers, hands, keyboard, mouse or gamepads before the stream is disconnected. The app keeps running on the PC.")
            onClicked: MlxrPrefs.idleDisconnect = settingsPage.nextLimit(settingsPage.idleValues, MlxrPrefs.idleDisconnect)
        }

        // ======== UI settings ========
        PrefCategory { text: qsTr("UI settings"); icon: "ui" }

        PrefSwitch {
            objectName: "quickResumeRow"
            title: qsTr("Quick resume")
            summary: qsTr("Selecting a running app resumes it at once, without the Resume / Quit menu.")
            isOn: MlxrPrefs.quickResume
            onSwitched: function(on) { MlxrPrefs.quickResume = on }
        }

        PrefSwitch {
            objectName: "fullScreenRow"
            title: qsTr("Enable full screen")
            summary: qsTr("This window, from its next start.")
            isOn: StreamingPreferences.uiDisplayMode === StreamingPreferences.UI_FULLSCREEN
            onSwitched: function(on) {
                StreamingPreferences.uiDisplayMode = on ? StreamingPreferences.UI_FULLSCREEN : StreamingPreferences.UI_WINDOWED
            }
        }

        VrChoice {
            key: "vr.environment"
            title: qsTr("Environment")
            summary: qsTr("Automatic is the black void, or the passthrough with the Passthrough switch on.")
            values: ["auto", "passthrough", "void", "home_theater", "grand_cinema", "synthwave"]
            names: [qsTr("Automatic"), qsTr("Passthrough"), qsTr("Black void"), qsTr("Home Theater"), qsTr("Grand Cinema"), qsTr("Synthwave")]
        }

        // Only Environment's Automatic reads it ([vr] environment and passthrough are two keys)
        VrSwitch {
            key: "vr.passthrough"
            title: qsTr("Passthrough")
            reason: MlxrPrefs.vr["vr.environment"] === "auto" ? "" : qsTr("Environment is not Automatic")
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

        // Native (lane wave3-visual): the Picture tab's Background row, the void's colour, the Glow edge row (below)
        // and the 3D eye balance (Video). Pressing a row steps through its choices.
        PrefRow {
            objectName: "backgroundDimRow"
            title: qsTr("Background dim")
            valueText: MlxrPrefs.backgroundDim + "%"
            summary: qsTr("How far the room, the passthrough cameras or the void are turned down, the picture itself not. 100% is black.")
            onClicked: MlxrPrefs.backgroundDim = settingsPage.nextLimit(settingsPage.backgroundDimSteps, MlxrPrefs.backgroundDim)
        }

        VrChoice {
            key: "vr.env_res"
            title: qsTr("Room detail")
            summary: qsTr("Takes effect at the next stream.")
            values: ["low", "standard", "high", "ultra"]
            names: [qsTr("Low"), qsTr("Standard"), qsTr("High"), qsTr("Ultra")]
        }

        VrSwitch {
            key: "display.screen_light"
            title: qsTr("Screen light")
            summary: qsTr("In a room, the picture lights it.")
        }

        VrSwitch {
            key: "display.glow"
            title: qsTr("Glow")
        }

        VrSlider {
            key: "display.glow_level"
            title: qsTr("Glow level")
            reason: MlxrPrefs.vr["display.glow"] === true ? "" : qsTr("Glow is off")
            from: 0
            to: 100
            stepSize: 5
            suffix: "%"
        }

        PrefRow {
            objectName: "glowFeatherRow"
            title: qsTr("Glow edge")
            valueText: settingsPage.glowFeatherNames[Math.max(0, settingsPage.glowFeatherValues.indexOf(MlxrPrefs.glowFeather))]
            summary: qsTr("Fades the picture's outer edge into the glow around it so the screen has no hard border.")
            reason: MlxrPrefs.vr["display.glow"] === true ? "" : qsTr("Glow is off")
            onClicked: MlxrPrefs.glowFeather = settingsPage.nextLimit(settingsPage.glowFeatherValues, MlxrPrefs.glowFeather)
        }

        VrSlider {
            key: "screen.distance_cm"
            title: qsTr("Screen distance")
            from: 20
            to: 800
            stepSize: 10
            suffix: " cm"
        }

        VrSlider {
            key: "screen.width_cm"
            title: qsTr("Screen size")
            summary: qsTr("The picture's width.")
            from: 80
            to: 800
            stepSize: 10
            suffix: " cm"
        }

        VrSlider {
            key: "screen.curve"
            title: qsTr("Screen curve")
            from: 0
            to: 100
            suffix: "%"
        }

        // Native (lane wave4b-zoom, PLAN 4b.6): the zoom a stream starts at; pressing the row steps it
        PrefRow {
            objectName: "zoomRow"
            title: qsTr("Zoom")
            valueText: MlxrPrefs.zoom + "%"
            summary: qsTr("How much of the picture fills the screen in the headset: 100% is all of it, 400% a quarter across and down. In the headset, bind zoom in, zoom out and zoom reset to a button, and aim at the part to zoom into; it remembers the zoom and the place for each app.")
            onClicked: MlxrPrefs.zoom = settingsPage.nextLimit(settingsPage.zoomSteps, MlxrPrefs.zoom)
        }

        VrSwitch {
            key: "screen.per_app"
            title: qsTr("Remember per app")
            summary: qsTr("Each app keeps its own screen place; an app without one starts from the rows above.")
        }

        VrSwitch {
            key: "screen.angular_lock"
            title: qsTr("Angular-size lock")
            summary: qsTr("The screen looks the same size when you push it away or pull it closer.")
        }

        // The cog's Head locked row: Off, On (the screen moves with the head) or Follow
        PrefRow {
            id: headLockRow
            objectName: "headLockRow"
            title: qsTr("Head locked")
            valueText: settingsPage.headLockNames[settingsPage.headLockIndex()]
            summary: qsTr("On: the screen moves with your head. Follow: it stays put until you turn away, then eases back in front of you.")
            function pick(i) {
                MlxrPrefs.setVr("display.head_locked", Number(i) === 1)
                MlxrPrefs.setVr("display.head_follow", Number(i) === 2)
            }
            onClicked: {
                var e = []
                for (var i = 0; i < settingsPage.headLockNames.length; i++) e.push({ text: settingsPage.headLockNames[i], reason: "" })
                listDialog.show(title, e, settingsPage.headLockIndex(), function(i) { headLockRow.pick(i) })
            }
        }

        VrSlider {
            key: "display.follow_leash_deg"
            title: qsTr("Follow after turning")
            reason: settingsPage.headLockIndex() === 2 ? "" : qsTr("Head locked is not Follow")
            from: 10
            to: 60
            suffix: "°"
        }

        // The headset's toast: pressing the row steps All -> Important -> Off
        PrefRow {
            objectName: "noticesRow"
            title: qsTr("Notices")
            valueText: settingsPage.noticeNames[settingsPage.noticeIndex()]
            summary: qsTr("The small pop-ups in the headset. Important leaves out the display rate and 3D on/off and keeps mode changes and errors.")
            onClicked: MlxrPrefs.notices = settingsPage.noticeValues[(settingsPage.noticeIndex() + 1) % settingsPage.noticeValues.length]
        }

        // ======== Performance monitor ========
        PrefCategory { text: qsTr("Performance monitor"); icon: "performance" }

        PrefSwitch {
            objectName: "statsRow"
            title: qsTr("Stats")
            summary: qsTr("The stats window in the headset while streaming.")
            isOn: MlxrPrefs.perfOverlay
            onSwitched: function(on) { MlxrPrefs.perfOverlay = on }
        }

        VrChoice {
            key: "display.stats_style"
            title: qsTr("Stats style")
            values: ["full", "hud", "lite"]
            names: [qsTr("Full"), qsTr("HUD"), qsTr("Lite")]
        }

        // ======== Advanced settings ========
        PrefCategory { text: qsTr("Advanced settings"); icon: "advanced" }

        PrefRow {
            objectName: "codecRow"
            title: qsTr("Change codec settings")
            valueText: settingsPage.codecEntries[settingsPage.codecIndex()].text
            summary: qsTr("HEVC needs less bitrate than H.264 for the same picture. Automatic uses HEVC when the PC has it, else H.264.")
            onClicked: listDialog.show(title, settingsPage.codecEntries, settingsPage.codecIndex(), function(i) {
                StreamingPreferences.videoCodecConfig = settingsPage.codecEntries[i].val
            })
        }

        PrefSwitch {
            objectName: "fullRangeRow"
            title: qsTr("Force full range video (experimental)")
            summary: qsTr("Asks the PC for full-range video instead of limited range. Hardware decoding only.")
            isOn: MlxrPrefs.fullRange
            onSwitched: function(on) { MlxrPrefs.fullRange = on }
        }

        PrefSwitch {
            objectName: "swDecodeRow"
            title: qsTr("Force software decoding")
            summary: qsTr("Decodes on the CPU instead of the Frame's hardware decoder. Slow; for troubleshooting.")
            isOn: StreamingPreferences.videoDecoderSelection === StreamingPreferences.VDS_FORCE_SOFTWARE
            onSwitched: function(on) {
                StreamingPreferences.videoDecoderSelection = on ? StreamingPreferences.VDS_FORCE_SOFTWARE : StreamingPreferences.VDS_AUTO
            }
        }

        // ======== Misc ========
        PrefCategory { text: qsTr("Misc"); icon: "misc" }

        PrefRow {
            objectName: "languageRow"
            title: qsTr("Language")
            valueText: settingsPage.languages[settingsPage.languageIndex()].text
            summary: qsTr("The language of this window. The headset's menus have their own, below.")
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

        // Native (lane wave6-i18n): the headset's own language. Pressing the row shows the languages under it
        PrefRow {
            objectName: "headsetLanguageRow"
            title: qsTr("Headset language")
            valueText: settingsPage.headsetLanguageText()
            summary: qsTr("The language of the menus, keyboard and notices in the headset. It takes effect the next time the VR client starts. Text without a translation stays English; Chinese, Japanese and Korean need the headset's Noto Sans CJK fonts, else English.")
            onClicked: headsetLanguagePicker.visible = !headsetLanguagePicker.visible
        }

        Flow {
            id: headsetLanguagePicker
            objectName: "headsetLanguagePicker"
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.bottomMargin: 8
            visible: false
            spacing: 6
            Repeater {
                model: settingsPage.headsetLanguageValues
                delegate: Button {
                    objectName: "headsetLang_" + modelData
                    text: settingsPage.headsetLanguageNames[index]
                    flat: MlxrPrefs.headsetLanguage !== modelData
                    highlighted: MlxrPrefs.headsetLanguage === modelData
                    onClicked: {
                        MlxrPrefs.headsetLanguage = modelData
                        headsetLanguagePicker.visible = false
                    }
                }
            }
        }

        Label {
            Layout.fillWidth: true
            Layout.topMargin: 24
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            text: qsTr("Rows that are also in the headset's cog menu change the same setting. The screen's place and each room's look are set in the cog menu. The headset's settings are saved in %1.").arg(MlxrPrefs.configIniPath)
            font.pointSize: 9
            color: settingsPage.summaryColor
            wrapMode: Text.Wrap
        }
    }
}
