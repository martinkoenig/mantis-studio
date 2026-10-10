import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../design"
import "../components"
Rectangle {
    id: root
    required property var workspace
    property string tab: "Parameters"
    color: Theme.panel; radius: Theme.radius; border.color: Theme.border
    ScrollView {
        id: scroll
        objectName: "scanSetupScroll"
        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: notes.top
        anchors.margins: 12; clip: true; contentWidth: availableWidth
        ScrollBar.vertical.policy: scroll.contentHeight > scroll.availableHeight + 1 ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
        ScrollBar.vertical.active: true
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ColumnLayout {
            width: scroll.availableWidth; spacing: 14
            RowLayout {
                Layout.fillWidth: true
                ScanText { text: "Scanner"; font.bold: true; color: Theme.text; Layout.fillWidth: true }
                SourceBadge { source: root.workspace.illustrative ? "mock" : "live" }
            }
            RowLayout {
                Layout.fillWidth: true; spacing: 12
                Image { source: "../home/assets/scanner.png"; visible: root.workspace.illustrative; fillMode: Image.PreserveAspectFit; Layout.preferredWidth: 62; Layout.preferredHeight: 76 }
                ColumnLayout {
                    Layout.fillWidth: true; spacing: 5
                    ScanText { text: root.workspace.presentation.devices.length ? root.workspace.presentation.devices[0].name : "No capture descriptor available"; Layout.fillWidth: true; color: Theme.text; font.bold: true; font.pixelSize: 14 }
                    ScanText { text: root.workspace.presentation.devices.length ? root.workspace.presentation.devices[0].state : "Hardware connection unknown"; Layout.fillWidth: true; font.pixelSize: 11 }
                    ScanText { text: "Readiness: Unknown"; Layout.fillWidth: true; color: Theme.warning; font.pixelSize: 11 }
                }
            }
            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }
            ScanText { text: "Scan Setup"; color: Theme.text; font.bold: true; font.pixelSize: 14 }
            RowLayout {
                Layout.fillWidth: true; spacing: 3
                Repeater {
                    model: ["Parameters", "Tracking", "Marker Map", "Plugins"]
                    StudioButton {
                        required property string modelData
                        objectName: "scanSetup" + modelData.replace(" ", "")
                        text: modelData; primary: root.tab === modelData
                        Layout.fillWidth: true; implicitHeight: 32; horizontalPadding: 3; font.pixelSize: 10
                        Accessible.description: "Read-only setup presentation tab"
                        onClicked: root.tab = modelData
                    }
                }
            }
            ColumnLayout {
                visible: root.tab === "Parameters"; Layout.fillWidth: true; spacing: 7
                Repeater {
                    model: ["Scan profile", "Exposure / gain", "Laser power", "Capture rate", "Processing rate", "Preview FPS", "Raw / texture recording"]
                    RowLayout {
                        required property string modelData
                        Layout.fillWidth: true; Layout.minimumHeight: 30
                        ScanText { text: modelData; Layout.fillWidth: true; font.pixelSize: 11 }
                        ScanText { text: "Unavailable"; font.pixelSize: 11; color: Theme.muted; padding: 6; background: Rectangle { color: Theme.raised; radius: 4; border.color: Theme.border } }
                    }
                }
                ScanText { text: "Settings need supported runtime contracts. No hardware settings are changed here."; font.pixelSize: 11; Layout.fillWidth: true }
                ScanUnavailable { objectName: "scanStart"; text: "Start Scan · unavailable"; reason: "Capture / Stop remain in Classic Acquisition. This foundation has no capture commands."; Layout.fillWidth: true }
            }
            ColumnLayout {
                visible: root.tab !== "Parameters"; Layout.fillWidth: true; spacing: 12
                ScanText { text: root.tab; color: Theme.text; font.pixelSize: 18; Layout.fillWidth: true }
                ScanText { text: root.tab === "Tracking" ? "Tracking quality and effective distance are not reported by this public source." : root.tab === "Marker Map" ? "Marker detection, map persistence and marker counts have no supported contract here." : "Plugin recovery and inventory remain in Classic Acquisition and Devices."; Layout.fillWidth: true }
                ScanUnavailable { objectName: "scanSetupDeferred"; text: root.tab === "Plugins" ? "Plugin control · unavailable" : "Configure · unavailable"; Layout.fillWidth: true }
            }
        }
    }
    ColumnLayout {
        id: notes
        objectName: "scanNotes"
        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
        anchors.margins: 12; height: 132; spacing: 6
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }
        ScanText { text: "Scan Notes"; color: Theme.text; font.bold: true; font.pixelSize: 14 }
        Rectangle {
            Layout.fillWidth: true; Layout.fillHeight: true; color: Theme.canvas; border.color: Theme.border; radius: 5
            ScanText { anchors.fill: parent; anchors.margins: 8; text: "Notes storage unavailable.\nNo notes or preferences are saved here."; font.pixelSize: 11 }
        }
        ScanUnavailable { objectName: "scanSaveNotes"; text: "Save notes · unavailable"; reason: "No public notes storage API. This panel is read-only."; Layout.fillWidth: true; implicitHeight: 30 }
    }
}
