import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mantis.Render 1.0
ColumnLayout {
    id: root
    required property var controller
    required property var studio
    Layout.fillWidth: true; spacing: 14
    Label { text: "Record or select captures"; font.pixelSize: 24; font.bold: true }
    Label { Layout.fillWidth: true; wrapMode: Text.Wrap; opacity: 0.7; text: "Move the target across the field of view. Vary distance, scale and perspective, with diverse poses visible to both cameras. Use the previews to check visibility; target detection is evaluated when you build a Dataset." }
    RowLayout {
        Button { text: "Record RawCapture"; enabled: !!root.controller.state.deviceId && !root.controller.state.recording && !root.controller.state.pending.captures; onClicked: root.controller.startCapture() }
        Button { text: "Stop recording"; enabled: root.controller.state.recording && !root.controller.state.pending.captures; onClicked: root.controller.stopCapture() }
    }
    Label { text: root.controller.state.captureState + (root.controller.state.recording ? " · " + (root.controller.state.captureFrames || 0) + " recorded FrameSets" : ""); Layout.fillWidth: true; wrapMode: Text.Wrap }
    RowLayout {
        visible: root.studio.dualPreview; Layout.fillWidth: true; Layout.preferredHeight: 230
        ColumnLayout {
            Layout.fillWidth: true; Layout.fillHeight: true
            Label { text: "LEFT · grayscale" }
            MeasurementView { id: left; Layout.fillWidth: true; Layout.fillHeight: true }
        }
        ColumnLayout {
            Layout.fillWidth: true; Layout.fillHeight: true
            Label { text: "RIGHT · grayscale" }
            MeasurementView { id: right; Layout.fillWidth: true; Layout.fillHeight: true }
        }
    }
    onVisibleChanged: if (visible) root.studio.attachPreview(left, right)
    Label { text: "Finalized RawCaptures · " + root.controller.state.rawIds.length + " selected"; font.bold: true }
    Repeater {
        model: root.controller.captures
        delegate: CheckBox {
            required property var modelData
            Layout.fillWidth: true
            enabled: modelData.ready; checked: modelData.selected
            text: modelData.id.slice(0, 18) + "… · " + modelData.state + " · schema " + modelData.schema
            onClicked: root.controller.setCaptureSelected(modelData.id, checked)
        }
    }
    Label { Layout.fillWidth: true; wrapMode: Text.Wrap; opacity: 0.7; text: "Recordings become selectable after finalization. The Dataset build checks camera identity and image geometry across your selected recordings." }
    StageError { controller: root.controller; slot: "captures"; Layout.fillWidth: true }
}
