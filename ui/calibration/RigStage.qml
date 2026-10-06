import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
ColumnLayout {
    id: root
    required property var controller
    Layout.fillWidth: true; spacing: 14
    Label { text: "Solve the stereo rig"; font.pixelSize: 24; font.bold: true }
    ArtifactSelector { controller: root.controller; slot: "rig"; title: "Resume with an existing Rig (restores its exact lineage)" }
    Label { text: "LEFT · " + root.controller.state.selected.left + "\nRIGHT · " + root.controller.state.selected.right; Layout.fillWidth: true; wrapMode: Text.Wrap; opacity: 0.7 }
    Label {
        visible: root.controller.state.checkerboard; Layout.fillWidth: true; wrapMode: Text.Wrap
        text: "Checkerboard supports mono camera calibration. Stereo/rig calibration requires physical correspondence IDs and therefore ChArUco in v0.3."
    }
    Label { text: "Rig frame · +X right / +Y forward / +Z up"; Layout.fillWidth: true; wrapMode: Text.Wrap }
    Label { text: "X1 uses org.mantis.x1.rig. For other scanners, provide an explicit rig frame ID and name."; Layout.fillWidth: true; wrapMode: Text.Wrap; opacity: 0.7 }
    TextField { id: frameId; Layout.fillWidth: true; placeholderText: "Rig frame ID"; text: root.controller.state.rigFrameId }
    TextField { id: frameName; Layout.fillWidth: true; placeholderText: "Rig frame name"; text: root.controller.state.rigFrameName }
    RowLayout {
        Label { text: "Held-out stereo pairs" }
        SpinBox { id: heldout; from: 1; to: 100000; value: 5; editable: true }
    }
    Button { text: "Solve Rig"; enabled: root.controller.state.canSolveRig && frameId.text.length > 0 && frameName.text.length > 0; onClicked: root.controller.solveRig(heldout.value, frameId.text, frameName.text) }
    JobStatus { controller: root.controller; slot: "rig" }
    StageError { controller: root.controller; slot: "rig"; Layout.fillWidth: true }
    Label { text: "Solving creates an immutable Rig revision. Activation is a separate action after review."; Layout.fillWidth: true; wrapMode: Text.Wrap; opacity: 0.7 }
}
