import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
ColumnLayout {
    id: root
    required property var controller
    required property string slot
    property var job: controller.state.jobs[slot] || ({})
    Layout.fillWidth: true
    visible: !!job.id
    Label { text: root.slot.toUpperCase() + " · " + (root.job.state || ""); font.bold: true }
    ProgressBar { Layout.fillWidth: true; value: root.job.progress || 0 }
    Label { Layout.fillWidth: true; wrapMode: Text.Wrap; text: (root.job.status || "") + "\n" + (root.job.diagnostics || ""); visible: text.trim().length > 0 }
    Label { text: root.job.id || ""; opacity: 0.6; font.pixelSize: 11 }
    Button { text: "Cancel job"; visible: root.job.state === "Running" || root.job.state === "Queued"; enabled: !root.controller.state.pending.cancel; onClicked: root.controller.cancelJob(root.slot) }
}
