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
    // 90 or 72), a fixed rate (stock "fps"), or Artemis's custom rate (Apollo). Never above 90.
    readonly property var fixedRates: [90, 72, 60, 30]
    readonly property var rateNames: [qsTr("Match headset"), qsTr("90 Hz"), qsTr("72 Hz"), qsTr("60 Hz"), qsTr("30 Hz"), qsTr("Custom...")]
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
    readonly property var networkValues: ["auto", "home", "adapter"]
    readonly property var networkNames: [qsTr("Automatic"), wifiLabel, qsTr("Steam Frame Wireless Adapter") + " (6 GHz)"]
    readonly property var networkSummaries: [qsTr("Uses whichever connection to your PC is faster when a stream starts."),
                                             qsTr("Streams over your Wi-Fi network."),
                                             qsTr("Streams over the Steam Frame Wireless Adapter plugged into your PC.")]
    function networkIndex() {
        return Math.max(0, networkValues.indexOf(MlxrPrefs.networkPath))
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
            summary: qsTr("How many frames per second the PC streams. Match headset uses the Steam Frame's own display rate (90 Hz, or 72 Hz when the headset runs at 72); the stream never goes above it.") +
                     " " + qsTr("Custom takes rates such as 59.94 (Apollo).")
            onClicked: {
                var e = []
                for (var i = 0; i < settingsPage.rateNames.length; i++) e.push({ text: settingsPage.rateNames[i], reason: "" })
                listDialog.show(title, e, settingsPage.rateIndex(), function(i) {
                    if (i === 0) {
                        MlxrPrefs.matchHeadsetFps = true
                        MlxrPrefs.customRefreshRate = ""
                        StreamingPreferences.fps = MlxrPrefs.headsetMaxFps
                        settingsPage.resetBitrate()
                    }
                    else if (i < settingsPage.rateNames.length - 1) {
                        MlxrPrefs.matchHeadsetFps = false
                        MlxrPrefs.customRefreshRate = ""
                        StreamingPreferences.fps = settingsPage.fixedRates[i - 1]
                        settingsPage.resetBitrate()
                    }
                    else {
                        editDialog.show(qsTr("Custom refresh rate"), qsTr("Enter refresh rate manually, for example 59.94. Only supports Apollo. Capped at %1 Hz.").arg(MlxrPrefs.headsetMaxFps),
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
            summary: qsTr("Increase for better image quality. Decrease to improve performance on slower connections.")
            from: 0.5
            to: 150
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
            reason: MlxrPrefs.renderMode === 1 ? "" : qsTr("Render Mode is Default 2D")
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

        PrefSwitch {
            title: qsTr("Enable Rumble")
            summary: qsTr("Enable rumble feedback on supported gamepads.") + " " + qsTr("Also the headset controllers.")
            isOn: MlxrPrefs.rumble
            onSwitched: function(on) { MlxrPrefs.rumble = on }
        }

        // ======== Input Settings ========
        PrefCategory { text: qsTr("Input Settings") }

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
            reason: qsTr("the Steam Frame's display runs at up to %1 Hz, so the VR client caps the frame rate there").arg(MlxrPrefs.headsetMaxFps)
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
