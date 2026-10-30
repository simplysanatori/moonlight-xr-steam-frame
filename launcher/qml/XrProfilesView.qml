// Moonlight XR Native launcher (lane wave4-launcher): the Stream profiles page, reached from the Settings page and
// from an app's menu ("Stream settings profile..."). A profile is a saved copy of the stream settings; this page
// makes them from the settings in force, renames, duplicates, updates and deletes them, and says which one a PC or
// an app uses. What a profile holds and when it is in force: launcher/src/mlxrprofiles.h. New file, copied into
// app/gui by launcher/build.sh. GPLv3.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import QtQuick.Controls.Material

import ComputerManager 1.0
import ComputerModel 1.0
import SdlGamepadKeyNavigation 1.0
import MlxrBindings 1.0
import MlxrProfiles 1.0

Flickable {
    id: profPage
    objectName: qsTr("Stream profiles")

    readonly property color accentColor: "#80CBC4"
    readonly property color summaryColor: "#B3FFFFFF"
    readonly property color reasonColor: "#FFB74D"
    readonly property color errorColor: "#EF9A9A"

    // Set when the page is opened from an app's menu: the PC's place in the PC list and the app's name
    property int initialPcIndex: -1
    property string initialApp: ""

    // which PC and app the "where it is used" rows are about; an empty app is every app of the PC
    property int pcIndex: 0
    property string chosenApp: ""
    property int pcTick: 0
    property int appTick: 0
    property var appModelObj: null
    property int rev: MlxrProfiles.revision
    property string statusText: ""
    property bool statusIsError: false
    // the profile an action dialog is about
    property string actionTarget: ""

    readonly property var pcNames: { pcTick; return MlxrBindings.modelNames(pcModel) }
    readonly property var pcApps: { appTick; return appModelObj ? MlxrBindings.modelNames(appModelObj) : [] }
    readonly property string pcName: pcIndex >= 0 && pcIndex < pcNames.length ? pcNames[pcIndex] : ""
    readonly property string scopeLabel: pcName === "" ? qsTr("(no PC)")
                                         : (chosenApp === "" ? qsTr("every app on %1").arg(pcName) : qsTr("%1 on %2").arg(chosenApp).arg(pcName))
    readonly property string assigned: { rev; return MlxrProfiles.assignedTo(pcName, chosenApp) }
    readonly property string effective: { rev; return MlxrProfiles.profileFor(pcName, chosenApp) }

    boundsBehavior: Flickable.OvershootBounds
    contentWidth: width
    contentHeight: profColumn.height + 40

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

    StackView.onActivated: SdlGamepadKeyNavigation.setUiNavMode(true)
    StackView.onDeactivating: SdlGamepadKeyNavigation.setUiNavMode(false)

    // ---- the PC list and the chosen PC's app list ----
    ComputerModel {
        id: pcModel
        Component.onCompleted: {
            initialize(ComputerManager)
            profPage.pcTick++
        }
    }

    Connections {
        target: pcModel
        ignoreUnknownSignals: true
        function onRowsInserted() { profPage.pcTick++ }
        function onRowsRemoved() { profPage.pcTick++ }
        function onModelReset() { profPage.pcTick++ }
        function onDataChanged() { profPage.pcTick++ }
    }

    Connections {
        target: profPage.appModelObj
        ignoreUnknownSignals: true
        function onRowsInserted() { profPage.appTick++ }
        function onRowsRemoved() { profPage.appTick++ }
        function onModelReset() { profPage.appTick++ }
        function onLayoutChanged() { profPage.appTick++ }
        function onDataChanged() { profPage.appTick++ }
    }

    function rebuildApps() {
        if (appModelObj) {
            appModelObj.destroy()
            appModelObj = null
        }
        if (pcIndex >= 0 && pcIndex < pcNames.length) {
            var m = Qt.createQmlObject('import AppModel 1.0; AppModel {}', profPage, '')
            m.initialize(ComputerManager, pcIndex, true)
            appModelObj = m
        }
        appTick++
    }

    onPcIndexChanged: rebuildApps()
    onPcNamesChanged: {
        if (!appModelObj && pcNames.length > 0) rebuildApps()
    }

    Component.onCompleted: {
        if (initialPcIndex >= 0) {
            pcIndex = initialPcIndex
            chosenApp = initialApp
        }
        rebuildApps()
    }

    Timer {
        interval: 1500
        running: true
        onTriggered: console.info("MLXR_PROFILES_UI: pcs=" + profPage.pcNames.join(",") + " pc=" + profPage.pcName +
                                  " app=" + profPage.chosenApp + " apps=" + profPage.pcApps.join(",") +
                                  " profiles=" + MlxrProfiles.names.join(",") + " assigned=" + profPage.assigned +
                                  " effective=" + profPage.effective)
    }

    // ---- what the buttons do (also called by the tests) ----
    function report(why, ok) {
        statusIsError = why !== ""
        statusText = why !== "" ? why : ok
        console.info("MLXR_PROFILES_UI: " + (why !== "" ? "error: " + why : ok))
    }

    function doCreate(name) {
        report(MlxrProfiles.create(name), qsTr("Saved the settings now in force as %1.").arg(name))
    }
    function doRename(from, to) {
        report(MlxrProfiles.rename(from, to), qsTr("Renamed %1 to %2.").arg(from).arg(to))
    }
    function doDuplicate(name) {
        report(MlxrProfiles.duplicate(name), qsTr("Made a copy of %1.").arg(name))
    }
    function doUpdate(name) {
        report(MlxrProfiles.updateFromCurrent(name), qsTr("%1 now holds the settings in force.").arg(name))
    }
    function doLoad(name) {
        report(MlxrProfiles.loadIntoSettings(name), qsTr("The Settings page now has the settings of %1.").arg(name))
    }
    function doDelete(name) {
        report(MlxrProfiles.remove(name), qsTr("Deleted %1.").arg(name))
    }
    function doUse(name) {
        report(MlxrProfiles.assign(pcName, chosenApp, name), name === "" ? qsTr("%1 uses the Settings page's settings.").arg(scopeLabel)
                                                                           : qsTr("%1 uses %2.").arg(scopeLabel).arg(name))
    }
    function pickPc(i) {
        pcIndex = i
        chosenApp = ""
    }
    function pickApp(name) {
        chosenApp = name
    }

    // ---- row types ----
    component PrefCategory: Item {
        id: category
        property string text
        property string icon
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
                    item.color = profPage.accentColor
                    item.size = 22
                }
            }
            Label {
                Layout.fillWidth: true
                text: category.text
                font.pointSize: 11
                font.bold: true
                color: profPage.accentColor
            }
        }
    }

    component PrefRow: ItemDelegate {
        id: prefRow
        property string title
        property string summary
        property string valueText
        property bool inUse: false

        Layout.fillWidth: true
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
                }
                Label {
                    Layout.fillWidth: true
                    visible: text !== ""
                    text: prefRow.valueText
                    font.pointSize: 10
                    font.bold: true
                    color: profPage.accentColor
                    wrapMode: Text.Wrap
                }
                Label {
                    Layout.fillWidth: true
                    visible: text !== ""
                    text: prefRow.summary
                    font.pointSize: 10
                    color: profPage.summaryColor
                    wrapMode: Text.Wrap
                }
            }
            Loader {
                visible: prefRow.inUse
                Layout.alignment: Qt.AlignVCenter
                source: prefRow.inUse ? "qrc:/gui/XrIcon.qml" : ""
                onLoaded: {
                    item.name = "check"
                    item.color = profPage.accentColor
                    item.size = 22
                }
            }
        }
    }

    // ---- dialogs ----
    NavigableDialog {
        id: listDialog
        property var entries: []      // [text]
        property int current: -1
        property var pickFn: null
        modal: true
        standardButtons: Dialog.Cancel
        background: Rectangle {
            color: "#424242"
            radius: 4
            border.color: "#55FFFFFF"
        }
        width: Math.min(profPage.width - 40, 560)

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
            implicitHeight: Math.min(contentHeight, profPage.height - 220)
            implicitWidth: parent ? parent.width : 400
            clip: true
            model: listDialog.entries
            currentIndex: listDialog.current
            keyNavigationEnabled: true
            delegate: ItemDelegate {
                width: listView.width
                text: modelData
                highlighted: ListView.isCurrentItem || index === listDialog.current
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
        id: nameDialog
        property var doneFn: null
        property string hint
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        background: Rectangle {
            color: "#424242"
            radius: 4
            border.color: "#55FFFFFF"
        }
        width: Math.min(profPage.width - 40, 520)

        function show(title, hint, text, fn) {
            nameDialog.title = title
            nameDialog.hint = hint
            nameDialog.doneFn = fn
            nameField.text = text
            open()
        }
        onOpened: nameField.forceActiveFocus()
        onAccepted: {
            var f = nameDialog.doneFn
            if (f) f(nameField.text.trim())
        }

        ColumnLayout {
            width: parent ? parent.width : 400
            Label {
                Layout.fillWidth: true
                visible: text !== ""
                text: nameDialog.hint
                wrapMode: Text.Wrap
                font.pointSize: 10
                color: profPage.summaryColor
            }
            TextField {
                id: nameField
                objectName: "nameField"
                Layout.fillWidth: true
                maximumLength: 48
                Keys.onReturnPressed: nameDialog.accept()
                Keys.onEnterPressed: nameDialog.accept()
            }
        }
    }

    NavigableMessageDialog {
        id: deleteDialog
        property string target: ""
        text: qsTr("Delete the profile %1? The PCs and apps that use it go back to the Settings page's settings.").arg(target)
        standardButtons: Dialog.Yes | Dialog.No
        onAccepted: profPage.doDelete(target)
    }

    function openActions(name) {
        actionTarget = name
        var e = [qsTr("Use for %1").arg(scopeLabel), qsTr("Rename..."), qsTr("Duplicate"),
                 qsTr("Update from the settings in force now"), qsTr("Load into the Settings page"), qsTr("Delete...")]
        listDialog.show(name, e, -1, function(i) {
            var n = profPage.actionTarget
            if (i === 0) profPage.doUse(n)
            else if (i === 1) nameDialog.show(qsTr("Rename %1").arg(n), "", n, function(t) { profPage.doRename(n, t) })
            else if (i === 2) profPage.doDuplicate(n)
            else if (i === 3) profPage.doUpdate(n)
            else if (i === 4) profPage.doLoad(n)
            else if (i === 5) {
                deleteDialog.target = n
                deleteDialog.open()
            }
        })
    }

    // ---- the page ----
    ColumnLayout {
        id: profColumn
        width: Math.min(profPage.width - 20, 980)
        x: Math.round((profPage.width - width) / 2)
        spacing: 0

        Label {
            Layout.fillWidth: true
            Layout.topMargin: 14
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            text: qsTr("A profile is a saved copy of the stream settings: resolution, refresh rate, bitrate, codec, sound, the virtual display, the headset's 3D and gamepad choices and the rest of the Settings page's stream settings. Set the Settings page the way you want a profile to be, then save it here. A PC or an app that uses a profile streams with it; afterwards the Settings page's own settings are back. An app's profile wins over its PC's.")
            font.pointSize: 10
            color: profPage.summaryColor
            wrapMode: Text.Wrap
        }

        Label {
            id: statusLabel
            Layout.fillWidth: true
            Layout.topMargin: 10
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            visible: profPage.statusText !== ""
            text: profPage.statusText
            font.pointSize: 11
            color: profPage.statusIsError ? profPage.errorColor : profPage.accentColor
            wrapMode: Text.Wrap
        }

        // ======== The profiles ========
        PrefCategory { text: qsTr("Profiles"); icon: "profiles" }

        PrefRow {
            objectName: "newProfileRow"
            title: qsTr("Save the settings now in force as a profile")
            valueText: MlxrProfiles.currentSummary()
            summary: qsTr("The Settings page's stream settings, as they are now.")
            onClicked: nameDialog.show(qsTr("New profile"), qsTr("A name for the profile, for example Desktop or Cinema 3D."), "",
                                       function(t) { profPage.doCreate(t) })
        }

        Repeater {
            model: { profPage.rev; return MlxrProfiles.names }
            delegate: PrefRow {
                required property string modelData
                objectName: "profile_" + modelData
                title: modelData
                valueText: { profPage.rev; return MlxrProfiles.summary(modelData) }
                summary: profPage.assigned.toLowerCase() === modelData.toLowerCase()
                         ? qsTr("Used for %1.").arg(profPage.scopeLabel) : ""
                inUse: profPage.assigned.toLowerCase() === modelData.toLowerCase()
                onClicked: profPage.openActions(modelData)
            }
        }

        Label {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.topMargin: 8
            visible: MlxrProfiles.names.length === 0
            text: qsTr("No profiles yet. Every stream uses the Settings page's settings.")
            font.pointSize: 10
            color: profPage.summaryColor
            wrapMode: Text.Wrap
        }

        // ======== Where a profile is used ========
        PrefCategory { text: qsTr("Where a profile is used"); icon: "host" }

        PrefRow {
            objectName: "pcRow"
            title: qsTr("PC")
            valueText: profPage.pcName === "" ? qsTr("No PC saved yet") : profPage.pcName
            summary: qsTr("The PC the two rows below are about.")
            onClicked: {
                if (profPage.pcNames.length === 0) return
                listDialog.show(title, profPage.pcNames, profPage.pcIndex, function(i) { profPage.pickPc(i) })
            }
        }

        PrefRow {
            objectName: "appRow"
            title: qsTr("App")
            valueText: profPage.chosenApp === "" ? qsTr("Every app on this PC") : profPage.chosenApp
            summary: qsTr("A profile for one app wins over the PC's.")
            onClicked: {
                var e = [qsTr("Every app on this PC")]
                var cur = profPage.chosenApp === "" ? 0 : -1
                for (var i = 0; i < profPage.pcApps.length; i++) {
                    e.push(profPage.pcApps[i])
                    if (profPage.pcApps[i] === profPage.chosenApp) cur = i + 1
                }
                listDialog.show(title, e, cur, function(i) { profPage.pickApp(i === 0 ? "" : e[i]) })
            }
        }

        PrefRow {
            objectName: "useRow"
            title: qsTr("Profile for %1").arg(profPage.scopeLabel)
            valueText: profPage.assigned !== "" ? profPage.assigned : qsTr("Default (the Settings page's settings)")
            summary: profPage.assigned !== "" ? ""
                     : (profPage.effective !== "" ? qsTr("Not set for this app: %1, the PC's profile, is used.").arg(profPage.effective)
                                                  : qsTr("Nothing is set: streams use the settings on the Settings page."))
            onClicked: {
                var e = [qsTr("Default (the Settings page's settings)")]
                var names = MlxrProfiles.names
                var cur = 0
                for (var i = 0; i < names.length; i++) {
                    e.push(names[i])
                    if (names[i].toLowerCase() === profPage.assigned.toLowerCase()) cur = i + 1
                }
                listDialog.show(title, e, cur, function(i) { profPage.doUse(i === 0 ? "" : names[i - 1]) })
            }
        }

        // ======== Everything that has a profile ========
        PrefCategory {
            text: qsTr("In use")
            icon: "check"
            visible: MlxrProfiles.assignments.length > 0
        }

        Repeater {
            model: { profPage.rev; return MlxrProfiles.assignments }
            delegate: PrefRow {
                required property int index
                required property var modelData
                objectName: "use_" + index
                title: modelData.app === "" ? qsTr("%1: every app").arg(modelData.pc) : qsTr("%1: %2").arg(modelData.pc).arg(modelData.app)
                valueText: modelData.profile
                onClicked: {
                    // the rows above are about this scope now
                    for (var i = 0; i < profPage.pcNames.length; i++) {
                        if (profPage.pcNames[i].toLowerCase() === modelData.pc.toLowerCase()) {
                            profPage.pickPc(i)
                            break
                        }
                    }
                    profPage.pickApp(modelData.app)
                }
            }
        }

        // ======== What the PC said ========
        PrefCategory { text: qsTr("Last check before a stream"); icon: "performance" }

        Label {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.topMargin: 4
            text: MlxrProfiles.lastCheckWhen === "" ? qsTr("No stream has been started since the launcher opened.")
                  : (MlxrProfiles.lastCheck === "" ? qsTr("%1: nothing to report.").arg(MlxrProfiles.lastCheckWhen)
                                                    : MlxrProfiles.lastCheckWhen + "\n" + MlxrProfiles.lastCheck)
            font.pointSize: 10
            color: MlxrProfiles.lastCheck === "" ? profPage.summaryColor : profPage.reasonColor
            wrapMode: Text.Wrap
        }

        Item {
            Layout.preferredHeight: 20
        }
    }
}
