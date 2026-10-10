import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import Mantis.Studio 1.0
import "../design"
import "../components"
import "../projects"
import "../devices"
FocusScope {
    id: root
    required property var bridge
    property string mode: "live"
    readonly property alias liveModel: liveModel
    readonly property alias inspector: inspector
    readonly property bool multiPane: width >= 1180
    property string compactPane: "Overview"
    property string activeSource: mode === "mock" ? "mock" : "live"
    property string sampleId: "demo-device"
    readonly property bool illustrative: activeSource === "mock"
    readonly property var liveData: liveModel.data
    readonly property var rows: illustrative ? demo.nodes : liveModel.nodes
    readonly property string selectedId: illustrative ? sampleId : liveData.selectedId || ""
    readonly property var selected: {
        if (!illustrative) return liveData.selected || {}
        for (let i = 0; i < demo.nodes.length; ++i) if (demo.nodes[i].id === sampleId) return demo.nodes[i]
        return {}
    }
    readonly property string liveCount: !liveData.hasSnapshot ? "Live count unknown" : liveData.count + " live descriptors · " + liveData.shown + " inspected"
    readonly property string metadataText: {
        if (selected.metadataOmitted) return "Metadata omitted: more than 128 entries. See Diagnostics for inspection bounds."
        const entries = selected.metadata || []
        let lines = []
        for (let i = 0; i < entries.length; ++i) lines.push(entries[i].key + ": " + entries[i].value)
        return (selected.metadataCount > entries.length ? "Showing " + entries.length + " of " + selected.metadataCount + " entries\n" : "") + (lines.join("\n") || "No metadata reported / no selection")
    }
    readonly property string issueText: {
        const issues = liveData.issues || []
        let lines = []
        for (let i = 0; i < issues.length; ++i) lines.push(issues[i].phase + " [" + issues[i].component + " / " + (issues[i].code === undefined || issues[i].code === null ? "untyped" : issues[i].code) + "]: " + issues[i].message)
        return lines.join("\n")
    }
    readonly property string eventText: {
        const events = liveData.events || []
        let lines = []
        for (let i = 0; i < events.length; ++i) lines.push("#" + events[i].sequence + " " + events[i].kind + " / " + events[i].component + "\n" + events[i].message)
        return (liveData.eventsLimited ? "Showing last 12 of " + liveData.eventCount + " runtime-wide events\n\n" : "") + (lines.join("\n\n") || "No runtime-wide events reported")
    }
    signal calibrate(string deviceId)
    signal navigate(string route)
    function choose(id) {
        if (illustrative) sampleId = id
        else liveModel.selectDevice(id)
    }
    function calibrateSelected() {
        if (mode === "mock" || illustrative) return
        const id = liveModel.calibrationIdentity()
        if (id.length > 0) root.calibrate(id)
    }
    function revealFocus() {
        const item = root.Window.window ? root.Window.window.activeFocusItem : null
        if (!root.visible || !item) return
        if (!item.visible) { overviewToggle.forceActiveFocus(Qt.TabFocusReason); return }
        let p = item.parent
        while (p && p !== root) {
            if (p.objectName === "devicesOverviewViewport" || p.objectName === "devicesInspectorViewport") {
                const y = item.mapToItem(p.contentItem, 0, 0).y
                const maxY = Math.max(0, p.contentHeight - p.height)
                if (y < p.contentY) p.contentY = Math.max(0, Math.min(y, maxY))
                else if (y + item.height > p.contentY + p.height) p.contentY = Math.max(0, Math.min(maxY, y + Math.min(item.height,p.height) - p.height))
            }
            p = p.parent
        }
    }
    onMultiPaneChanged: Qt.callLater(root.revealFocus)
    onModeChanged: activeSource = mode === "mock" ? "mock" : "live"
    Keys.onEscapePressed: {
        if (!multiPane && compactPane !== "Overview") { compactPane = "Overview"; overviewToggle.forceActiveFocus(Qt.TabFocusReason) }
    }
    Connections { target: root.Window.window; function onActiveFocusItemChanged() { Qt.callLater(root.revealFocus) } }
    DevicesModel { id: liveModel; bridge: root.mode === "mock" ? null : root.bridge }
    DevicesDemo { id: demo }
    ColumnLayout {
        anchors.fill: parent; spacing: 10
        RowLayout {
            Layout.fillWidth: true; spacing: 12
            ColumnLayout {
                Layout.fillWidth: true; spacing: 4
                DevicesText { text: "Devices"; color: Theme.text; font.pixelSize: 24; font.bold: true }
                DevicesText { text: "Explore advertised devices, components and calibration"; font.pixelSize: 12; Layout.fillWidth: true }
            }
            UnavailableAction { objectName: "devicesAdd"; text: "+ Add Device"; reason: "Discovery is runtime/plugin-owned. No public add-device contract." }
            UnavailableAction { objectName: "devicesPresets"; text: "Device Presets"; reason: "No public device preset contract." }
        }
        RowLayout {
            Layout.fillWidth: true; spacing: 8
            SourceBadge { source: root.illustrative ? "mock" : "live" }
            DevicesText { text: root.illustrative ? "7 demo descriptors · illustrative only" : root.liveCount; Layout.fillWidth: true; font.pixelSize: 11 }
            StudioButton { visible: root.mode === "hybrid"; objectName: "devicesLiveSource"; text: "Live"; primary: !root.illustrative; onClicked: root.activeSource = "live" }
            StudioButton { visible: root.mode === "hybrid"; objectName: "devicesDemoSource"; text: "Demo / Mock"; primary: root.illustrative; onClicked: root.activeSource = "mock" }
            StudioButton { objectName: "devicesRefresh"; text: "Refresh"; enabled: root.mode !== "mock" && !root.illustrative && root.bridge && !root.bridge.busy; Accessible.description: "Refresh existing runtime snapshot; no physical device rediscovery command"; onClicked: if (enabled) root.bridge.refresh() }
            StudioButton { objectName: "openCalibration"; text: "Existing captures / offline calibration"; font.pixelSize: 12; onClicked: root.calibrate("") }
        }
        DevicesText {
            objectName: "devicesFreshness"
            Layout.fillWidth: true
            text: root.illustrative ? "Demo / Mock · no hardware, runtime commands or camera stream" : root.liveData.freshness + ((root.liveData.anomalies || []).length ? " · Graph warnings: inspect Diagnostics" : "")
            font.pixelSize: 11; color: root.illustrative || !root.liveData.confirmed || (root.liveData.anomalies || []).length ? Theme.warning : Theme.secondary
        }
        RowLayout {
            visible: !root.multiPane; Layout.fillWidth: true
            StudioButton { id: overviewToggle; objectName: "devicesCompactOverview"; text: "Overview"; primary: root.compactPane === "Overview"; onClicked: root.compactPane = "Overview" }
            StudioButton { objectName: "devicesCompactNavigator"; text: "All Devices"; primary: root.compactPane === "All Devices"; onClicked: root.compactPane = "All Devices" }
            StudioButton { objectName: "devicesCompactInspector"; text: "Inspector"; primary: root.compactPane === "Inspector"; onClicked: root.compactPane = "Inspector" }
            DevicesText { Layout.fillWidth: true; text: root.selected.name || "Choose a device in All Devices"; maximumLineCount: 1; elide: Text.ElideRight; font.pixelSize: 11 }
        }
        Item {
            id: panes
            objectName: "devicesPanes"
            // Persistent anchored panes avoid Qt 6.4 Layout visibility/size cache
            // reuse during immediate compact-view + resize + inspector-tab changes.
            Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 0
            DevicesNavigator {
                id: navigator
                objectName: "devicesNavigator"
                workspace: root
                visible: root.multiPane || root.compactPane === "All Devices"
                anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
                width: root.multiPane ? 246 : parent.width
            }
            Flickable {
                id: overviewViewport
                objectName: "devicesOverviewViewport"
                visible: root.multiPane || root.compactPane === "Overview"
                anchors.top: parent.top; anchors.bottom: parent.bottom
                anchors.left: root.multiPane ? navigator.right : parent.left
                anchors.right: root.multiPane ? inspector.left : parent.right
                anchors.leftMargin: root.multiPane ? 12 : 0; anchors.rightMargin: root.multiPane ? 12 : 0
                clip: true; contentWidth: width; contentHeight: overview.implicitHeight
                boundsBehavior: Flickable.StopAtBounds; flickableDirection: Flickable.VerticalFlick
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                DevicesOverview { id: overview; width: Math.max(0, overviewViewport.width - 12); workspace: root }
            }
            DevicesInspector {
                id: inspector
                objectName: "devicesInspector"
                workspace: root
                visible: root.multiPane || root.compactPane === "Inspector"
                anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom
                width: root.multiPane ? 340 : parent.width
            }
        }
    }
}
