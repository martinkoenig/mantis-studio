import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import Mantis.Studio 1.0
import "../design"
import "../components"
import "../scan"
FocusScope {
    id: root
    required property var bridge
    property string mode: "live"
    property string activeSource: mode === "mock" ? "mock" : "live"
    property string presentationTab: "Live Scanner"
    property string displayStyle: "Coverage"
    property string compactPane: "Viewport"
    property string selectedRow: ""
    property var observedEpoch: 0
    readonly property bool illustrative: activeSource === "mock"
    readonly property bool multiPane: width >= 1180
    readonly property alias liveModel: liveModel
    readonly property var presentation: illustrative ? demo.data : liveModel.data
    signal navigate(string route)
    function openClassic() { if (mode !== "mock") navigate("acquisition") }
    function resetPresentation() { selectedRow = ""; presentationTab = "Live Scanner"; displayStyle = "Coverage" }
    function revealFocus() {
        const item = root.Window.window ? root.Window.window.activeFocusItem : null
        if (!root.visible || !item) return
        if (!item.visible) { paneViewport.forceActiveFocus(Qt.TabFocusReason); return }
        let p = item.parent
        while (p && p !== root) {
            if (p instanceof Flickable) {
                const y = item.mapToItem(p.contentItem, 0, 0).y
                p.contentY = Math.max(0, Math.min(p.contentHeight - p.height, y > p.contentY + p.height - item.height ? y + item.height - p.height : Math.min(p.contentY, y)))
                return
            }
            p = p.parent
        }
    }
    onModeChanged: { activeSource = mode === "mock" ? "mock" : "live"; resetPresentation() }
    onActiveSourceChanged: resetPresentation()
    onCompactPaneChanged: paneViewport.forceActiveFocus(Qt.TabFocusReason)
    onWidthChanged: Qt.callLater(revealFocus)
    Keys.onEscapePressed: { compactPane = "Viewport"; paneViewport.forceActiveFocus(Qt.TabFocusReason) }
    Connections { target: root.Window.window; function onActiveFocusItemChanged() { root.revealFocus() } }
    ScanModel { id: liveModel; objectName: "scanModel"; bridge: root.mode !== "mock" && !root.illustrative ? root.bridge : null }
    Connections {
        target: liveModel
        function onChanged() {
            if (root.observedEpoch !== liveModel.data.projectEpoch) {
                root.observedEpoch = liveModel.data.projectEpoch
                root.resetPresentation()
            } else if (root.selectedRow.length && !liveModel.data.artifacts.some(function(row) { return row.id === root.selectedRow })) {
                root.selectedRow = ""
            }
        }
    }
    ScanDemo { id: demo }
    ColumnLayout {
        // A persistent hidden workspace initially has zero geometry in Qt 6.4.
        // Keep its dormant layout finite without changing visible/minimum sizes.
        width: Math.max(root.width, 880); height: Math.max(root.height, 520); spacing: 8
        RowLayout {
            objectName: "scanToolbar"
            Layout.fillWidth: true; spacing: 8
            StudioButton { objectName: "scanLiveTab"; text: "Live Scanner"; primary: root.presentationTab === text; onClicked: root.presentationTab = text; Accessible.description: "Presentation tab only; does not start capture" }
            StudioButton { objectName: "scanReplayTab"; text: "Replay Capture"; primary: root.presentationTab === text; onClicked: root.presentationTab = text; Accessible.description: "Presentation tab only; does not start replay" }
            ScanUnavailable { visible: root.multiPane; objectName: "scanProfile"; text: "Scan Profile · unavailable" }
            ScanUnavailable { visible: root.multiPane; objectName: "scanPipeline"; text: "Configure Pipeline" }
            ScanUnavailable { visible: root.multiPane; objectName: "scanViewTools"; text: "View tools"; reason: "Orbit, zoom and geometry view tools remain in Classic Acquisition." }
            Item { Layout.fillWidth: true }
            StudioButton { id: classic; objectName: "scanOpenClassic"; text: "Open Classic Acquisition"; primary: true; enabled: root.mode !== "mock"; onClicked: root.openClassic(); Accessible.description: root.mode === "mock" ? "Mock has no hardware controls" : "Navigate to the existing capture, preview, replay and geometry workflow" }
        }
        RowLayout {
            Layout.fillWidth: true; spacing: 8
            SourceBadge { objectName: "scanSourceBadge"; source: root.illustrative ? "mock" : "live" }
            ScanText { objectName: "scanFreshness"; text: root.presentation.freshness; color: root.illustrative || !root.presentation.confirmed ? Theme.warning : Theme.secondary; Layout.fillWidth: true; maximumLineCount: 1; elide: Text.ElideRight }
            ScanText { objectName: "scanRuntimeStatus"; text: root.illustrative ? "No runtime authority" : root.presentation.confirmed ? "Runtime connected · readiness unknown" : "Runtime unconfirmed"; font.pixelSize: 11 }
            StudioButton { objectName: "scanLiveSource"; visible: root.mode === "hybrid"; text: "Live"; primary: !root.illustrative; onClicked: root.activeSource = "live"; implicitHeight: 30 }
            StudioButton { objectName: "scanDemoSource"; visible: root.mode === "hybrid"; text: "Demo / Mock"; primary: root.illustrative; onClicked: root.activeSource = "mock"; implicitHeight: 30 }
        }
        ScanText {
            objectName: "scanCaptureEvidence"; visible: !root.illustrative
            text: root.presentation.captureStatusText; font.pixelSize: 11
            color: root.presentation.confirmed ? Theme.secondary : Theme.warning
            Layout.fillWidth: true; maximumLineCount: 1; elide: Text.ElideRight
        }
        ScanText {
            objectName: "scanRuntimeError"; visible: !root.illustrative && root.presentation.runtimeError.length > 0
            text: root.presentation.runtimeError; color: Theme.warning; font.pixelSize: 11
            Layout.fillWidth: true; maximumLineCount: 2; elide: Text.ElideRight
        }
        RowLayout {
            visible: !root.multiPane; Layout.fillWidth: true
            StudioButton { id: paneViewport; objectName: "scanPaneViewport"; text: "Viewport"; primary: root.compactPane === "Viewport"; onClicked: root.compactPane = "Viewport"; implicitHeight: 32 }
            Repeater {
                model: ["Cameras", "Setup", "Sequence"]
                StudioButton { required property string modelData; objectName: "scanPane" + modelData; text: modelData; primary: root.compactPane === modelData; onClicked: root.compactPane = modelData; implicitHeight: 32 }
            }
            ScanText { text: "Read-only foundation"; Layout.fillWidth: true; horizontalAlignment: Text.AlignRight; font.pixelSize: 11 }
        }
        Item {
            id: panes
            objectName: "scanPanes"
            visible: root.multiPane || root.compactPane !== "Sequence"
            Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 0
            ScanCameras {
                id: cameras; objectName: "scanCameras"; workspace: root
                visible: root.multiPane || root.compactPane === "Cameras"
                anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
                width: root.multiPane ? 202 : parent.width
            }
            ScanSetup {
                id: setup; objectName: "scanSetup"; workspace: root
                visible: root.multiPane || root.compactPane === "Setup"
                anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom
                width: root.multiPane ? 304 : parent.width
            }
            ScanViewport {
                objectName: "scanViewport"; workspace: root
                visible: root.multiPane || root.compactPane === "Viewport"
                anchors.left: root.multiPane ? cameras.right : parent.left; anchors.right: root.multiPane ? setup.left : parent.right
                anchors.top: parent.top; anchors.bottom: parent.bottom
                anchors.leftMargin: root.multiPane ? 10 : 0; anchors.rightMargin: root.multiPane ? 10 : 0
            }
        }
        Rectangle {
            objectName: "scanStatusStrip"
            Layout.fillWidth: true; implicitHeight: 60; color: Theme.panel; border.color: Theme.border; radius: Theme.radius
            Item {
                id: statuses
                anchors.fill: parent; anchors.margins: 10
                Repeater {
                    model: [{name: "Capture", value: root.presentation.captureStatus}, {name: "Processing", value: root.presentation.jobs.length ? root.presentation.jobs[0].state : "Not reported"}, {name: "Preview", value: root.illustrative ? "Illustrative" : "Classic view"}, {name: "Tracking", value: "Unavailable"}, {name: "Points / distance", value: "Unavailable"}, {name: "CPU / GPU", value: "Not reported"}]
                    Item {
                        required property var modelData
                        required property int index
                        x: index * (statuses.width + 12) / 6
                        width: Math.max(0, (statuses.width - 60) / 6); height: statuses.height
                        ScanText { anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.topMargin: 4; text: modelData.value; color: Theme.text; font.pixelSize: 12; wrapMode: Text.NoWrap; elide: Text.ElideRight }
                        ScanText { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.bottomMargin: 4; text: modelData.name; font.pixelSize: 10; wrapMode: Text.NoWrap; elide: Text.ElideRight }
                    }
                }
            }
        }
        ScanDock {
            objectName: "scanDock"; workspace: root
            visible: root.multiPane || root.compactPane === "Sequence"
            Layout.fillWidth: true; Layout.fillHeight: !root.multiPane; Layout.preferredHeight: Math.min(220, root.height * 0.22)
        }
    }
}
