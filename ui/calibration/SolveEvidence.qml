import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
ColumnLayout {
    id: root
    required property var evidence
    property bool stereo: false
    Layout.fillWidth: true
    visible: !!evidence.id
    Label {
        Layout.fillWidth: true; wrapMode: Text.Wrap; font.bold: true
        text: root.stereo ? "Symmetric epipolar residuals" : ((root.evidence.camera || {}).role || "") + " · " + ((root.evidence.camera || {}).identity || "") + " · " + ((root.evidence.camera || {}).width || 0) + " × " + ((root.evidence.camera || {}).height || 0)
    }
    Repeater {
        model: ["training", "heldout", "final"]
        delegate: ColumnLayout {
            required property string modelData
            id: evidenceRow
            property var sampleEvidence: root.evidence[modelData] || ({})
            Layout.fillWidth: true
            Label { text: modelData === "heldout" ? "Held-out" : modelData.charAt(0).toUpperCase() + modelData.slice(1); opacity: 0.7 }
            Label {
                Layout.fillWidth: true; wrapMode: Text.Wrap
                text: "RMS " + Number(evidenceRow.sampleEvidence.rms || 0).toFixed(4) + " px  ·  median " + Number(evidenceRow.sampleEvidence.median || 0).toFixed(4) + " px\np95 " + Number(evidenceRow.sampleEvidence.p95 || 0).toFixed(4) + " px  ·  max " + Number(evidenceRow.sampleEvidence.max || 0).toFixed(4) + " px\n" + (evidenceRow.sampleEvidence.count || 0) + (root.stereo ? " pairs" : " views")
            }
            Label { visible: evidenceRow.sampleEvidence.opencvRms !== undefined; text: "OpenCV solver RMS " + Number(evidenceRow.sampleEvidence.opencvRms || 0).toFixed(4) + " px"; opacity: 0.7 }
            Label {
                visible: !root.stereo; Layout.fillWidth: true; wrapMode: Text.Wrap; opacity: 0.7
                text: "Coverage (normalized image) · X " + Number((evidenceRow.sampleEvidence.coverage || {}).minX || 0).toFixed(3) + "–" + Number((evidenceRow.sampleEvidence.coverage || {}).maxX || 0).toFixed(3) + " · Y " + Number((evidenceRow.sampleEvidence.coverage || {}).minY || 0).toFixed(3) + "–" + Number((evidenceRow.sampleEvidence.coverage || {}).maxY || 0).toFixed(3) + " · area " + Number((evidenceRow.sampleEvidence.coverage || {}).area || 0).toFixed(3)
            }
        }
    }
    Label {
        Layout.fillWidth: true; wrapMode: Text.Wrap; opacity: 0.7
        text: "Implementation · OpenCV " + ((root.evidence.implementation || {}).opencv || "") + " · Mantis " + ((root.evidence.implementation || {}).mantis || "") + "\n" + ((root.evidence.implementation || {}).build || "")
    }
}
