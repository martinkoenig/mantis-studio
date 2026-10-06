import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mantis.Render 1.0
import "../workspaces"

ApplicationWindow {
    id: window
    property string workspace: "acquisition"
    property var studioBridge: studio
    visible: true
    width: 1420; height: 900
    minimumWidth: 1080; minimumHeight: 720
    title: "Mantis Studio"
    color: "#10151c"
    palette.window: "#10151c"
    palette.windowText: "#e4edf5"
    palette.base: "#17212b"
    palette.text: "#e4edf5"
    palette.button: "#25323e"
    palette.buttonText: "#e4edf5"
    palette.highlight: "#54d6b0"
    palette.highlightedText: "#10151c"
    font.family: "Sans Serif"
    font.pixelSize: 13

    component Caption: Label { color: "#8a9caf"; font.pixelSize: 11; font.letterSpacing: 1.4 }
    component Panel: Rectangle { color: "#18212a"; radius: 9; border.color: "#273441" }
    component Action: Button { implicitHeight: 38 }

    onWorkspaceChanged: calibration.visible = workspace !== "acquisition"

    ColumnLayout {
        anchors.fill: parent; anchors.margins: 20; spacing: 16
        RowLayout {
            Layout.fillWidth: true; spacing: 14
            Label { text: "MANTIS"; font.pixelSize: 25; font.bold: true; font.letterSpacing: 4; color: "#6fe0bc" }
            Rectangle { width: 1; height: 25; color: "#34404c" }
            Label { text: "Studio"; font.pixelSize: 20 }
            Button { text: "Scan / Acquisition"; highlighted: window.workspace === "acquisition"; onClicked: window.workspace = "acquisition" }
            Button { text: "Devices / System"; highlighted: window.workspace === "devices"; onClicked: window.workspace = "devices" }
            Button { text: "Calibration"; visible: window.workspace === "calibration"; highlighted: true }
            Item { Layout.fillWidth: true }
            Rectangle { width: 8; height: 8; radius: 4; color: studio.connected ? "#6fe0bc" : "#eaad6b" }
            Label { text: studio.connected ? "Runtime connected" : "Runtime unavailable"; color: "#a8bbcb" }
        }
        DevicesWorkspace {
            Layout.fillWidth: true; Layout.fillHeight: true; visible: window.workspace === "devices"
            controller: calibration
            onCalibrate: function(deviceId) { calibration.selectDevice(deviceId); calibration.stage = 0; window.workspace = "calibration" }
        }
        CalibrationWorkspace {
            Layout.fillWidth: true; Layout.fillHeight: true; visible: window.workspace === "calibration"
            controller: calibration; studio: window.studioBridge
        }
        RowLayout {
            visible: window.workspace === "acquisition"
            onVisibleChanged: if (visible) studio.attachPreview(leftPreview, rightPreview)
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 16
            ColumnLayout {
                Layout.minimumWidth: 255; Layout.maximumWidth: 255; Layout.preferredWidth: 255; Layout.fillHeight: true; spacing: 16
                Panel {
                    Layout.fillWidth: true; implicitHeight: deviceColumn.implicitHeight + 32
                    ColumnLayout {
                        id: deviceColumn; anchors.fill: parent; anchors.margins: 16; spacing: 12
                        Caption { text: "ACQUISITION" }
                        Repeater {
                            model: studio.devices
                            delegate: ColumnLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                Label { text: modelData.name; font.pixelSize: 17; font.bold: true }
                                Label { text: studio.capturing ? "● Streaming · raw recording" : "● Ready · deterministic source"; color: "#6fe0bc"; font.pixelSize: 11 }
                                Action { text: "Start capture"; Layout.fillWidth: true; enabled: studio.connected && !studio.capturing && !studio.busy; onClicked: studio.startCapture(modelData.id) }
                            }
                        }
                        Action { text: "Stop capture"; Layout.fillWidth: true; enabled: studio.capturing && !studio.busy; onClicked: studio.stopCapture() }
                        Label { text: studio.acquisitionText; visible: text.length > 0; Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: 11; color: "#a8bbcb" }
                        Rectangle { Layout.fillWidth: true; height: 1; color: "#2b3945" }
                        Caption { text: "PROCESSING RECIPE" }
                        ComboBox { id: recipe; Layout.fillWidth: true; model: ["example", "crash-test"] }
                        Action { text: "Run pipeline"; Layout.fillWidth: true; enabled: studio.capturing && !studio.busy; onClicked: studio.runPipeline(recipe.currentText) }
                        Label { text: "Synthetic geometry · millimeters"; color: "#8a9caf"; font.pixelSize: 11 }
                    }
                }
                Panel {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 16; spacing: 12
                        Caption { text: "PLUGINS" }
                        ListView {
                            Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 14; model: studio.plugins
                            delegate: ColumnLayout {
                                required property var modelData
                                width: ListView.view.width
                                Label { text: modelData.id.replace("org.mantis.", ""); font.bold: true; wrapMode: Text.Wrap; Layout.fillWidth: true }
                                Label { text: modelData.state + " · " + modelData.execution; color: modelData.state === "failed" ? "#fa9298" : "#92adbd"; font.pixelSize: 11 }
                                Label { text: modelData.diagnostic; visible: text.length > 0; color: "#fa9298"; font.pixelSize: 11; wrapMode: Text.Wrap; Layout.fillWidth: true }
                                Button { text: "Re-enable plugin"; visible: modelData.state === "failed"; enabled: !studio.busy; onClicked: studio.enablePlugin(modelData.id, true) }
                            }
                        }
                    }
                }
            }
            ColumnLayout {
                Layout.minimumWidth: 300; Layout.preferredWidth: 750; Layout.fillWidth: true; Layout.fillHeight: true; spacing: 12
                Panel {
                    visible: studio.dualPreview
                    Layout.fillWidth: true; Layout.preferredHeight: 235
                    RowLayout {
                        anchors.fill: parent; anchors.margins: 12; spacing: 12
                        ColumnLayout {
                            Layout.fillWidth: true; Layout.fillHeight: true
                            Caption { text: "LEFT · NATIVE GRAYSCALE" }
                            MeasurementView { id: leftPreview; objectName: "leftPreview"; Layout.fillWidth: true; Layout.fillHeight: true }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true; Layout.fillHeight: true
                            Caption { text: "RIGHT · NATIVE GRAYSCALE" }
                            MeasurementView { id: rightPreview; objectName: "rightPreview"; Layout.fillWidth: true; Layout.fillHeight: true }
                        }
                    }
                    Component.onCompleted: studio.attachPreview(leftPreview, rightPreview)
                }
                Panel {
                    Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 1; spacing: 0
                        RowLayout {
                            Layout.fillWidth: true; Layout.margins: 15
                            Caption { text: "GEOMETRY VIEW" }
                            Item { Layout.fillWidth: true }
                            Label { text: cloud.pointCount.toLocaleString() + " points  ·  mm"; color: "#92adbd"; font.pixelSize: 12 }
                        }
                        PointCloudView {
                            id: cloud; objectName: "pointCloudView"
                            Layout.fillWidth: true; Layout.fillHeight: true
                            Component.onCompleted: studio.attachView(cloud)
                            MouseArea {
                                anchors.fill: parent
                                property real lastX; property real lastY
                                onPressed: function(mouse) { lastX = mouse.x; lastY = mouse.y }
                                onPositionChanged: function(mouse) { if (pressed) { cloud.yaw += (mouse.x-lastX)*0.5; cloud.pitch += (mouse.y-lastY)*0.5; lastX=mouse.x; lastY=mouse.y } }
                                onWheel: function(wheel) { cloud.zoom *= wheel.angleDelta.y > 0 ? 1.1 : 0.9 }
                            }
                            Column {
                                anchors.centerIn: parent; spacing: 12; visible: cloud.pointCount === 0
                                Label { text: "Your geometry starts here"; font.pixelSize: 22; color: "#c9d8e5"; anchors.horizontalCenter: parent.horizontalCenter }
                                Label { text: "Start a capture, then run the example pipeline."; color: "#8a9caf"; anchors.horizontalCenter: parent.horizontalCenter }
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true; Layout.margins: 12
                            Label { text: "Drag to orbit   ·   Scroll to zoom"; color: "#7d94a8"; font.pixelSize: 11 }
                            Item { Layout.fillWidth: true }
                            Button { text: "Reset view"; onClicked: { cloud.yaw=-35; cloud.pitch=35; cloud.zoom=1 } }
                        }
                    }
                }
                Panel {
                    Layout.fillWidth: true; Layout.preferredHeight: 165
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 14
                        Caption { text: "RUNTIME DIAGNOSTICS" }
                        ListView {
                            Layout.fillWidth: true; Layout.fillHeight: true; clip: true; model: studio.diagnostics
                            onCountChanged: positionViewAtEnd()
                            delegate: Label {
                                required property var modelData
                                width: ListView.view.width; wrapMode: Text.Wrap; font.family: "Monospace"; font.pixelSize: 11
                                color: modelData.kind === "error" ? "#fa9298" : "#91a7b9"
                                text: modelData.sequence + "  [" + modelData.component + "] " + modelData.message
                            }
                        }
                    }
                }
            }
            ColumnLayout {
                Layout.minimumWidth: 285; Layout.maximumWidth: 285; Layout.preferredWidth: 285; Layout.fillHeight: true; spacing: 16
                Panel {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 16; spacing: 12
                        Caption { text: "PROJECT ARTIFACTS" }
                        ListView {
                            Layout.fillWidth: true; Layout.fillHeight: true; clip: true; model: studio.artifacts; spacing: 8
                            delegate: Rectangle {
                                required property var modelData
                                width: ListView.view.width; height: modelData.type === "org.mantis.RawCapture" ? 110 : 76; radius: 6
                                color: modelData.id === studio.selectedArtifact ? "#233f40" : "#202c37"
                                border.color: modelData.id === studio.selectedArtifact ? "#55cba7" : "#2d3b48"
                                Column {
                                    anchors.fill: parent; anchors.margins: 10; spacing: 5
                                    Label { text: modelData.type.replace("org.mantis.", ""); font.bold: true }
                                    Label { text: modelData.state + " · " + modelData.chunks + " chunks"; color: "#95adbc"; font.pixelSize: 11 }
                                    Label { text: modelData.id.substring(0, 18) + "…"; color: "#728c9f"; font.pixelSize: 10; font.family: "Monospace" }
                                    Row {
                                        visible: modelData.type === "org.mantis.RawCapture" && modelData.state === "FINALIZED"
                                        Button { text: "Replay"; enabled: !studio.busy; onClicked: studio.replay(modelData.id, false) }
                                        Button { text: "Verify"; enabled: !studio.busy; onClicked: studio.replay(modelData.id, true) }
                                    }
                                }
                                MouseArea { anchors.fill: parent; enabled: modelData.type === "org.mantis.PointCloud" && modelData.state === "FINALIZED" && !studio.busy; onClicked: studio.selectArtifact(modelData.id) }
                            }
                        }
                        TextField { id: exportPath; placeholderText: "Export path, e.g. /tmp/model.ply"; placeholderTextColor: "#8499aa"; Layout.fillWidth: true }
                        Action { text: "Export selected · PLY"; Layout.fillWidth: true; enabled: studio.selectedArtifact.length > 0 && exportPath.text.length > 0 && !studio.busy; onClicked: studio.exportArtifact(exportPath.text) }
                    }
                }
                Panel {
                    Layout.fillWidth: true; Layout.preferredHeight: 215
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 16
                        Caption { text: "JOBS" }
                        ListView {
                            Layout.fillWidth: true; Layout.fillHeight: true; clip: true; model: studio.jobs; spacing: 12
                            delegate: ColumnLayout {
                                required property var modelData
                                width: ListView.view.width
                                Label { text: modelData.name; font.bold: true }
                                ProgressBar { value: modelData.progress; Layout.fillWidth: true }
                                RowLayout {
                                    Label { text: modelData.state; color: modelData.state === "Failed" ? "#fa9298" : "#95adbc"; font.pixelSize: 11 }
                                    Button { text: "Cancel"; visible: modelData.state === "Running" || modelData.state === "Queued"; onClicked: studio.cancelJob(modelData.id) }
                                }
                            }
                        }
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Label { text: studio.error.length > 0 ? studio.error : studio.project; elide: Text.ElideMiddle; Layout.fillWidth: true; color: studio.error.length > 0 ? "#fa9298" : "#7891a5"; font.pixelSize: 11 }
            Label { text: "RIGHT HANDED  ·  X RIGHT / Y FORWARD / Z UP"; font.pixelSize: 10; color: "#7891a5" }
        }
    }
}
