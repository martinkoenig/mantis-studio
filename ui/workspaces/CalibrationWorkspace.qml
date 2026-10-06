import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../calibration"
Item {
    id: root
    required property var controller
    required property var studio
    objectName: "calibrationWorkspace"
    property color panelColor: "#18212a"
    property color borderColor: "#273441"
    property color accentColor: "#6fe0bc"
    ColumnLayout {
        anchors.fill: parent; spacing: 16
        Frame {
            Layout.fillWidth: true
            background: Rectangle { color: root.panelColor; border.color: root.borderColor; radius: 8 }
            ColumnLayout {
                width: parent.width; spacing: 6
                Label { text: "Calibration · " + root.controller.state.device.name; font.pixelSize: 22; font.bold: true }
                Label { text: root.controller.state.device.status + " · " + (root.controller.state.active.id ? "Active r" + root.controller.state.active.revision + " · " + root.controller.state.active.id : "No active calibration"); Layout.fillWidth: true; wrapMode: Text.Wrap; opacity: 0.7 }
                StageError { controller: root.controller; slot: "connection"; Layout.fillWidth: true }
                StageError { controller: root.controller; slot: "cancel"; Layout.fillWidth: true }
            }
        }
        RowLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 16
            ColumnLayout {
                Layout.preferredWidth: 170; Layout.alignment: Qt.AlignTop; spacing: 10
                Repeater {
                    model: root.controller.stages
                    delegate: Button {
                        required property string modelData
                        required property int index
                        objectName: "calibrationStage" + index
                        Layout.fillWidth: true; text: (index + 1) + "  " + modelData
                        highlighted: root.controller.stage === index
                        onClicked: root.controller.stage = index
                    }
                }
                Button { text: "Refresh artifacts"; Layout.fillWidth: true; onClicked: root.controller.refresh() }
            }
            Frame {
                Layout.fillWidth: true; Layout.fillHeight: true
                background: Rectangle { color: root.panelColor; border.color: root.borderColor; radius: 8 }
                ScrollView {
                    id: content; anchors.fill: parent; clip: true
                    ColumnLayout {
                        width: content.availableWidth; spacing: 16
                        ColumnLayout {
                            visible: root.controller.stage === 0; Layout.fillWidth: true; spacing: 14
                            Label { text: "Choose a calibration device"; font.pixelSize: 24; font.bold: true }
                            Label { Layout.fillWidth: true; wrapMode: Text.Wrap; opacity: 0.7; text: "Solve offline with existing captures, or select a compatible scanner to record and activate." }
                            ComboBox {
                                Layout.fillWidth: true; model: root.controller.devices; textRole: "name"; valueRole: "id"
                                currentIndex: -1; displayText: root.controller.state.device.name
                                onActivated: root.controller.selectDevice(currentValue)
                            }
                            Button { text: "Work offline"; onClicked: root.controller.selectDevice("") }
                            Label { text: root.controller.state.deviceId; Layout.fillWidth: true; wrapMode: Text.Wrap }
                            Label { text: root.controller.state.device.capabilities.join("\n"); Layout.fillWidth: true; wrapMode: Text.Wrap; opacity: 0.7 }
                            Label { text: "Current active binding is shown above. Activation requires a selected logical device."; Layout.fillWidth: true; wrapMode: Text.Wrap; opacity: 0.7 }
                        }
                        TargetStage { controller: root.controller; visible: root.controller.stage === 1 }
                        CapturesStage { controller: root.controller; studio: root.studio; visible: root.controller.stage === 2 }
                        DatasetStage { controller: root.controller; visible: root.controller.stage === 3 }
                        CamerasStage { controller: root.controller; visible: root.controller.stage === 4 }
                        RigStage { controller: root.controller; visible: root.controller.stage === 5 }
                        ReviewStage { controller: root.controller; visible: root.controller.stage === 6 }
                        ColumnLayout {
                            visible: root.width < 1220; Layout.fillWidth: true
                            Button { id: revisions; text: "Artifact / revision browser"; checkable: true }
                            Repeater {
                                model: revisions.checked ? root.controller.artifacts : []
                                delegate: Label {
                                    required property var modelData
                                    Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: 11
                                    text: modelData.type.replace("org.mantis.", "") + " · " + modelData.label + (modelData.error.message ? "\n" + modelData.error.component + " · " + modelData.error.status + " · " + modelData.error.message : "")
                                }
                            }
                        }
                    }
                }
            }
            Frame {
                Layout.preferredWidth: 220; Layout.fillHeight: true; visible: root.width >= 1220
                background: Rectangle { color: root.panelColor; border.color: root.borderColor; radius: 8 }
                ScrollView {
                    anchors.fill: parent; clip: true
                    ColumnLayout {
                        width: parent.availableWidth; spacing: 12
                        Label { text: "Immutable revisions"; font.bold: true; color: root.accentColor }
                        Label { text: "Resume with finalized artifacts. Open revisions remain visible here for diagnostics."; Layout.fillWidth: true; wrapMode: Text.Wrap; opacity: 0.7 }
                        Repeater {
                            model: root.controller.artifacts
                            delegate: Label {
                                required property var modelData
                                Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: 11
                                text: modelData.type.replace("org.mantis.", "") + "\n" + modelData.label + (modelData.error.message ? "\n" + modelData.error.component + " · " + modelData.error.status + " · " + modelData.error.message : "")
                            }
                        }
                    }
                }
            }
        }
    }
}
