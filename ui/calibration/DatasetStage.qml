import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
ColumnLayout {
    id: root
    required property var controller
    property var evidence: controller.state.dataset
    Layout.fillWidth: true; spacing: 14
    Label { text: "Build the calibration Dataset"; font.pixelSize: 24; font.bold: true }
    ArtifactSelector { controller: root.controller; slot: "dataset"; title: "Resume with an existing Dataset (restores its exact Target)" }
    Label { text: "Camera roles · left / right" }
    RowLayout {
        Label { text: "Maximum selected views per camera" }
        SpinBox { id: samples; from: 1; to: 100000; value: 40; editable: true }
    }
    Label { Layout.fillWidth: true; wrapMode: Text.Wrap; opacity: 0.7; text: "40 is an editable starting suggestion, not a quality threshold. Detection and selection run in the background; other Studio views remain available." }
    Button { text: "Build Dataset"; enabled: root.controller.state.canBuild; onClicked: root.controller.buildDataset(samples.value) }
    JobStatus { controller: root.controller; slot: "dataset" }
    StageError { controller: root.controller; slot: "dataset"; Layout.fillWidth: true }
    Label { visible: !!root.evidence.id; text: "Dataset evidence"; font.bold: true }
    Label { visible: !!root.evidence.id; text: (root.evidence.records || 0) + " records · " + (root.evidence.rawCount || 0) + " RawCaptures · selection policy v" + (root.evidence.policy || 0); Layout.fillWidth: true; wrapMode: Text.Wrap }
    Repeater {
        model: root.evidence.cameras || []
        delegate: ColumnLayout {
            required property var modelData
            Layout.fillWidth: true
            Label { text: modelData.role.toUpperCase() + " · " + modelData.identity; font.bold: true; Layout.fillWidth: true; wrapMode: Text.Wrap }
            Label { text: modelData.width + " × " + modelData.height + "\n" + modelData.analyzed + " analyzed · " + modelData.detected + " target detected · " + modelData.noTarget + " no target · " + modelData.selected + " selected views"; Layout.fillWidth: true; wrapMode: Text.Wrap }
        }
    }
}
