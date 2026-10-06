import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
ColumnLayout {
    id: root
    required property var controller
    property var evidence: controller.state.rig
    Layout.fillWidth: true; spacing: 14
    Label { text: "Review & activate"; font.pixelSize: 24; font.bold: true }
    ArtifactSelector { controller: root.controller; slot: "rig"; title: "Select the exact Rig revision to review" }
    Label { visible: !!root.evidence.id; text: "Rig revision " + (root.evidence.revision || 0) + " · " + (root.evidence.logicalId || ""); font.bold: true; Layout.fillWidth: true; wrapMode: Text.Wrap }
    Label {
        visible: !!root.evidence.id; font.pixelSize: 20; Layout.fillWidth: true; wrapMode: Text.Wrap
        text: "Baseline " + Number(root.evidence.baseline || 0).toFixed(3) + " mm\nRelative rotation " + Number(root.evidence.angleRad || 0).toFixed(6) + " rad"
    }
    SolveEvidence { evidence: root.evidence; stereo: true }
    Label { Layout.fillWidth: true; wrapMode: Text.Wrap; text: "Pixel reprojection and epipolar residuals are calibration evidence, not scanner accuracy or metrology acceptance."; opacity: 0.7 }
    Button { id: engineering; text: "Engineering details"; checkable: true }
    ColumnLayout {
        visible: engineering.checked && !!root.evidence.id; Layout.fillWidth: true
        Label {
            Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: 11
            text: "Rig artifact · " + (root.evidence.id || "") + "\nLogical ID · " + (root.evidence.logicalId || "") + "\nRevision · " + (root.evidence.revision || "") + "\nDataset · " + (root.evidence.dataset || "") + "\nLEFT CameraCalibration · " + (root.evidence.left || "") + "\nRIGHT CameraCalibration · " + (root.evidence.right || "") + "\nTarget · " + (root.evidence.target || "")
        }
        Label { Layout.fillWidth: true; wrapMode: Text.Wrap; font.family: "Monospace"; font.pixelSize: 11; text: root.evidence.transforms || "" }
    }
    Label { text: "Selected device · " + root.controller.state.device.name; Layout.fillWidth: true; wrapMode: Text.Wrap }
    Button {
        text: "Activate this Rig revision…"; enabled: !!root.evidence.id && !!root.controller.state.deviceId && !root.controller.state.pending.activation
        onClicked: { confirm.deviceId = root.controller.state.deviceId; confirm.rigId = root.evidence.id; confirm.revision = root.evidence.revision; confirm.open() }
    }
    Label { Layout.fillWidth: true; wrapMode: Text.Wrap; text: "Active calibration applies to future captures. Historical captures retain the exact calibration revision recorded with them."; opacity: 0.7 }
    Label { Layout.fillWidth: true; wrapMode: Text.Wrap; text: root.controller.state.active.id ? "Current active · r" + root.controller.state.active.revision + " · " + root.controller.state.active.id : "No active calibration" }
    Button {
        text: "Clear active calibration…"; enabled: !!root.controller.state.active.id && !root.controller.state.pending.activation
        onClicked: { clearConfirm.deviceId = root.controller.state.deviceId; clearConfirm.open() }
    }
    StageError { controller: root.controller; slot: "activation"; Layout.fillWidth: true }
    Dialog {
        id: confirm
        property string deviceId
        property string rigId
        property var revision
        title: "Activate calibration?"; modal: true; standardButtons: Dialog.Ok | Dialog.Cancel
        anchors.centerIn: Overlay.overlay; width: 520
        contentItem: Label { wrapMode: Text.Wrap; text: "Device · " + confirm.deviceId + "\nRig artifact · " + confirm.rigId + "\nExact revision · " + confirm.revision + "\nApplies to future captures only." }
        onAccepted: root.controller.activate(deviceId, rigId)
    }
    Dialog {
        id: clearConfirm
        property string deviceId
        title: "Clear active calibration?"; modal: true; standardButtons: Dialog.Ok | Dialog.Cancel
        anchors.centerIn: Overlay.overlay; width: 520
        contentItem: Label { wrapMode: Text.Wrap; text: "Remove the active binding for " + clearConfirm.deviceId + "? Calibration artifacts and historical captures are retained." }
        onAccepted: root.controller.clearActive(deviceId)
    }
}
