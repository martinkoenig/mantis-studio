import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../calibration"
ScrollView {
    id: root
    required property var controller
    signal calibrate(string deviceId)
    clip: true
    ColumnLayout {
        width: root.availableWidth; spacing: 18
        Label { text: "Devices / System"; font.pixelSize: 28; font.bold: true }
        Label { text: "Geometric calibration belongs to your scanner configuration."; opacity: 0.7 }
        StageError { controller: root.controller; slot: "connection"; Layout.fillWidth: true }
        Repeater {
            model: root.controller.devices
            delegate: Frame {
                required property var modelData
                Layout.fillWidth: true
                ColumnLayout {
                    anchors.fill: parent; spacing: 10
                    Label { text: modelData.name; font.pixelSize: 20; font.bold: true }
                    Label { text: modelData.id; opacity: 0.7; Layout.fillWidth: true; wrapMode: Text.Wrap }
                    Label { text: modelData.status + " · " + modelData.capabilities.join(", "); opacity: 0.7; Layout.fillWidth: true; wrapMode: Text.Wrap }
                    Button { text: "Calibrate"; onClicked: root.calibrate(modelData.id) }
                }
            }
        }
        Button { text: "Open Calibration · existing captures / offline"; onClicked: root.calibrate("") }
    }
}
