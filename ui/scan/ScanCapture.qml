import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../design"
import "../components"
ColumnLayout {
    id: root
    required property var workspace
    spacing: 6
    ScanText { text: "Capture device · discovered, readiness unknown"; font.pixelSize: 11; Layout.fillWidth: true }
    ComboBox {
        id: devices
        objectName: "scanCaptureDevice"
        Layout.fillWidth: true
        model: root.workspace.presentation.devices
        textRole: "name"; valueRole: "id"
        currentIndex: root.workspace.presentation.devices.findIndex(function(row) { return row.id === root.workspace.presentation.selectedDevice })
        displayText: currentIndex < 0 ? "Choose capture device" : currentText
        enabled: root.workspace.captureSourceAllowed && root.workspace.presentation.confirmed && !root.workspace.presentation.busy && count > 0
        Accessible.name: "Capture device"
        Accessible.description: root.workspace.presentation.captureControlReason
        contentItem: ScanText { text: devices.displayText; color: devices.enabled ? Theme.text : Theme.muted; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight; maximumLineCount: 1; leftPadding: 8 }
        delegate: ItemDelegate {
            required property var modelData
            width: devices.width
            enabled: modelData.captureEligible === true
            contentItem: ScanText { text: modelData.name + (modelData.captureEligible ? "" : " · unavailable"); color: Theme.text; maximumLineCount: 2; elide: Text.ElideRight }
        }
        onActivated: root.workspace.chooseCaptureDevice(currentValue)
    }
    RowLayout {
        Layout.fillWidth: true
        ScanUnavailable {
            objectName: "scanStart"; text: "Start capture"; primary: true; Layout.fillWidth: true
            enabled: root.workspace.captureSourceAllowed && root.workspace.presentation.canStartCapture
            reason: root.workspace.presentation.captureControlReason
            onClicked: root.workspace.startCapture()
        }
        ScanUnavailable {
            objectName: "scanStop"; text: "Stop capture"; Layout.fillWidth: true
            enabled: root.workspace.captureSourceAllowed && root.workspace.presentation.canStopCapture
            reason: root.workspace.presentation.captureControlReason
            onClicked: root.workspace.stopCapture()
        }
    }
    ScanText { objectName: "scanCaptureAuthority"; text: root.workspace.presentation.captureControlReason; font.pixelSize: 10; Layout.fillWidth: true; color: root.workspace.presentation.busy || !root.workspace.presentation.confirmed ? Theme.warning : Theme.secondary }
}
