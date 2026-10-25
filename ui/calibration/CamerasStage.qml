import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
ColumnLayout {
    id: root
    required property var controller
    Layout.fillWidth: true; spacing: 14
    Label { text: "Solve the cameras independently"; font.pixelSize: 24; font.bold: true; Layout.fillWidth: true; wrapMode: Text.Wrap }
    Label { text: "Select existing solutions for this exact Dataset, or submit independent LEFT and RIGHT jobs. A failed side can be retried without losing the other solution."; Layout.fillWidth: true; wrapMode: Text.Wrap; opacity: 0.7 }
    RowLayout {
        Label { text: "Held-out views per camera" }
        SpinBox { id: heldout; from: 1; to: 100000; value: 5; editable: true }
    }
    Button {
        text: "Solve LEFT + RIGHT"; enabled: root.controller.state.canSolveBoth
        onClicked: root.controller.solveBoth(heldout.value)
    }
    Repeater {
        model: ["left", "right"]
        delegate: ColumnLayout {
            required property string modelData
            Layout.fillWidth: true; spacing: 10
            ArtifactSelector { controller: root.controller; slot: modelData; title: modelData.toUpperCase() + " · compatible CameraCalibrations" }
            Button {
                text: "Solve / retry " + modelData.toUpperCase()
                enabled: root.controller.state.canSolveCamera[modelData]
                onClicked: root.controller.solveCamera(modelData, heldout.value)
            }
            JobStatus { controller: root.controller; slot: modelData }
            StageError { controller: root.controller; slot: modelData; Layout.fillWidth: true }
            SolveEvidence { evidence: root.controller.state[modelData] }
        }
    }
}
