import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../design"
import "../components"
import "../projects"
Panel {
    id: root
    required property var workspace
    property string tab: "Settings"
    property var device: workspace.selected
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 12; spacing: 12
        Row {
            Layout.fillWidth: true
            Repeater {
                model: ["Settings", "Calibration", "Diagnostics", "Info"]
                delegate: StudioButton {
                    required property string modelData
                    objectName: "devicesTab" + modelData
                    width: (root.width - 24) / 4; height: 40
                    horizontalPadding: 2
                    text: modelData; font.pixelSize: 11
                    checked: root.tab === modelData
                    Accessible.description: "Inspector tab. " + (checked ? "Selected" : "")
                    onClicked: root.tab = modelData
                    background: Rectangle {
                        color: parent.checked ? Theme.selection : parent.down ? Theme.pressed : parent.hovered ? Theme.hover : "transparent"
                        border.color: parent.visualFocus ? Theme.focus : "transparent"; border.width: parent.visualFocus ? 2 : 0
                        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 2; color: Theme.accent; visible: root.tab === modelData }
                    }
                }
            }
        }
        Flickable {
            id: viewport
            objectName: "devicesInspectorViewport"
            Layout.fillWidth: true; Layout.fillHeight: true
            clip: true; contentWidth: width; contentHeight: content.implicitHeight
            flickableDirection: Flickable.VerticalFlick; boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            onWidthChanged: contentY = Math.min(contentY, Math.max(0, contentHeight-height))
            Column {
                id: content
                width: Math.max(0, viewport.width - 12); spacing: 16
                Column {
                    visible: root.tab === "Settings"; width: parent.width; spacing: 12
                    DevicesText { width: parent.width; text: "Camera Settings"; color: Theme.text; font.bold: true; font.pixelSize: 15 }
                    DevicesText { width: parent.width; text: "Read-only capability inspection. Values and supported ranges are not reported by this runtime."; font.pixelSize: 12 }
                    UnavailableAction { objectName: "devicesCameraResolution"; width: parent.width; text: "Resolution / format · Not reported"; reason: "Device descriptors publish no camera configuration values or ranges." }
                    UnavailableAction { objectName: "devicesCameraExposure"; width: parent.width; text: "Exposure / gain · Not reported"; reason: "An advertised capability alone grants no UI command authority." }
                    DevicesText { width: parent.width; text: "Laser Settings"; color: Theme.text; font.bold: true; font.pixelSize: 15; topPadding: 10 }
                    UnavailableAction { objectName: "devicesLaserPower"; width: parent.width; text: "Power / pattern · Not reported"; reason: "No reviewed public safe emitter-control API. Devices cannot issue power or pattern commands." }
                    DevicesText { width: parent.width; text: "Emitter state and interlocks are unknown. The daemon/plugin and independent hardware own physical safety; a software disable is no fail-off guarantee."; font.pixelSize: 12 }
                    DevicesText { width: parent.width; text: "Synchronization"; color: Theme.text; font.bold: true; font.pixelSize: 15; topPadding: 10 }
                    UnavailableAction { objectName: "devicesSync"; width: parent.width; text: "Trigger / strobe · Not reported"; reason: "No public timing configuration or output command is available to Devices." }
                    DevicesText { width: parent.width; text: "No trigger association or optical timing is inferred from a descriptor."; font.pixelSize: 12 }
                    UnavailableAction { objectName: "devicesSavePreset"; width: parent.width; text: "Save as Preset"; reason: "No public device preset persistence contract." }
                }
                Column {
                    visible: root.tab === "Calibration"; width: parent.width; spacing: 12
                    DevicesText { width: parent.width; text: "Geometric calibration"; color: Theme.text; font.bold: true; font.pixelSize: 16 }
                    DevicesText { width: parent.width; text: root.workspace.illustrative ? "Demo descriptors cannot enter device-bound calibration." : root.workspace.liveData.selected.calibrationReason }
                    StudioButton { objectName: "devicesCalibrate"; width: parent.width; text: "Open Calibration"; primary: true; enabled: !root.workspace.illustrative && root.workspace.liveData.selected.canCalibrate === true; Accessible.description: root.workspace.liveData.selected.calibrationReason; onClicked: root.workspace.calibrateSelected() }
                    DevicesText { width: parent.width; text: "Active binding: Not inspected\nOpen Calibration to inspect the exact selected device. Absence of inspection does not mean uncalibrated." }
                    DevicesText { width: parent.width; text: "Device → Target → Captures → Dataset → Cameras → Rig → Review & Activate"; font.pixelSize: 12 }
                    DevicesText { width: parent.width; text: "Opening this workflow starts no capture, solve, activation or clearing of a binding."; font.pixelSize: 12 }
                    StudioButton { objectName: "devicesOfflineCalibration"; width: parent.width; text: "Work with existing captures / offline"; font.pixelSize: 12; onClicked: root.workspace.calibrate("") }
                }
                Column {
                    visible: root.tab === "Diagnostics"; width: parent.width; spacing: 12
                    DevicesText { width: parent.width; text: "Plugin provenance"; color: Theme.text; font.bold: true; font.pixelSize: 16 }
                    DevicesText { width: parent.width; text: root.workspace.illustrative ? "Demo / Mock · no runtime plugin state" : "Plugin state: " + ((root.device.pluginInfo || {}).state || "Unknown") + "\n" + ((root.device.pluginInfo || {}).diagnostic || "No diagnostic reported") + ((root.device.pluginInfo || {}).limited ? "\nPlugin text sanitized / truncated" : "") }
                    DevicesText { width: parent.width; text: "Plugin status is not physical device readiness." + (root.workspace.liveData.pluginsLimited ? " Plugin correspondence limited to 128 entries; status unknown." : ""); font.pixelSize: 12 }
                    DevicesText { width: parent.width; text: "Snapshot / graph diagnostics"; color: Theme.text; font.bold: true; font.pixelSize: 15 }
                    DevicesText { width: parent.width; text: root.workspace.illustrative ? "Local fixture only. No runtime diagnostics inspected." : (root.workspace.liveData.anomalies || []).join("\n") || "No graph anomalies in the inspected descriptors." }
                    DevicesText { width: parent.width; text: root.workspace.illustrative ? "" : root.workspace.issueText; visible: text.length > 0; color: Theme.warning }
                    DevicesText { width: parent.width; text: "Runtime-wide events"; color: Theme.text; font.bold: true; font.pixelSize: 15 }
                    DevicesText { width: parent.width; text: root.workspace.illustrative ? "No illustrative hardware errors or metrics." : root.workspace.eventText; font.pixelSize: 12 }
                    DevicesText { width: parent.width; text: "Events retain their runtime scope; text matching does not attribute an event to this device."; font.pixelSize: 12 }
                }
                Column {
                    visible: root.tab === "Info"; width: parent.width; spacing: 12
                    DevicesText { width: parent.width; text: "Descriptor information"; color: Theme.text; font.bold: true; font.pixelSize: 16 }
                    DevicesText { width: parent.width; text: root.workspace.illustrative ? "Demo / Mock · local illustrative identity" : "Live descriptor · " + root.workspace.liveData.freshness; color: root.workspace.illustrative ? Theme.warning : Theme.secondary }
                    // Literal selectable text gives safe copy/inspect without executing metadata.
                    TextArea {
                        objectName: "devicesInfoText"
                        width: parent.width; readOnly: true; selectByMouse: true; wrapMode: TextEdit.WrapAnywhere
                        textFormat: TextEdit.PlainText; color: Theme.secondary; font.pixelSize: 12
                        padding: 10; background: Rectangle { color: Theme.canvas; radius: 5; border.color: Theme.border }
                        text: "ID: " + (root.device.id || "No selection") + "\nPlugin: " + (root.device.plugin || "Not reported") + "\nParent: " + (root.device.parent || "Top-level / no selection") + "\nChildren: " + ((root.device.children || []).join("\n") || "None") + "\n\nAdvertised capabilities\n" + ((root.device.capabilities || []).join("\n") || "None")
                        Accessible.name: "Literal device identity, relationships and advertised capabilities; read-only"
                    }
                    DevicesText { width: parent.width; text: "Metadata · untrusted literal properties"; color: Theme.text; font.bold: true }
                    TextArea {
                        objectName: "devicesMetadataText"
                        width: parent.width; readOnly: true; selectByMouse: true; wrapMode: TextEdit.WrapAnywhere
                        textFormat: TextEdit.PlainText; color: Theme.secondary; font.pixelSize: 12; padding: 10
                        background: Rectangle { color: Theme.canvas; radius: 5; border.color: Theme.border }
                        text: root.workspace.metadataText
                        Accessible.name: "Untrusted metadata, bounded read-only text"
                    }
                    DevicesText { width: parent.width; text: "Plugin version: " + ((root.device.pluginInfo || {}).version || "Not reported") + "\nPlugin kind / execution: " + ((root.device.pluginInfo || {}).kind || "Not reported") + " / " + ((root.device.pluginInfo || {}).execution || "Not reported"); visible: !root.workspace.illustrative }
                    DevicesText { width: parent.width; text: "All shown values are descriptors. Metadata is never interpreted as device commands, UI code or physical telemetry."; font.pixelSize: 12 }
                }
            }
        }
    }
    onTabChanged: viewport.contentY = 0
}
