import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mantis.Render 1.0

ScrollView {
    id: root
    required property var studio
    clip: true
    ScrollBar.horizontal.policy: root.contentWidth > root.availableWidth ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
    ScrollBar.vertical.policy: root.contentHeight > root.availableHeight ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
    contentWidth: Math.max(980, availableWidth)
    contentHeight: Math.max(600, availableHeight)
    component Caption: Label { color: "#8a9caf"; font.pixelSize: 11; font.letterSpacing: 1.4 }
    component Panel: Rectangle { color: "#18212a"; radius: 9; border.color: "#273441" }
    component Action: Button { implicitHeight: 38 }
    Item {
        width: root.contentWidth; height: root.contentHeight
        RowLayout {
            onVisibleChanged: if (visible) studio.attachPreview(leftPreview, rightPreview)
            anchors.fill: parent; spacing: 16
            ColumnLayout {
                Layout.minimumWidth: 255; Layout.maximumWidth: 255; Layout.preferredWidth: 255; Layout.fillHeight: true; spacing: 16
                Panel {
                    Layout.fillWidth: true; implicitHeight: deviceColumn.implicitHeight + 32
                    ColumnLayout {
                        id: deviceColumn; anchors.fill: parent; anchors.margins: 16; spacing: 12
                        Caption { text: "ACQUISITION" }
                        Label {
                            objectName: "acquisitionFreshness"
                            visible: !studio.connected
                            text: "Runtime state unconfirmed · last-known snapshot. Current capture and device availability are unknown."
                            color: "#edc078"; font.pixelSize: 11; Layout.fillWidth: true; wrapMode: Text.Wrap
                        }
                        Label {
                            objectName: "acquisitionCaptureStatus"
                            text: studio.captureStatusText
                            color: studio.connected ? "#6fe0bc" : "#edc078"; font.pixelSize: 11
                            Layout.fillWidth: true; Layout.minimumWidth: 0; wrapMode: Text.Wrap
                        }
                        Repeater {
                            model: studio.devices
                            delegate: ColumnLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                Label { text: modelData.name; font.pixelSize: 17; font.bold: true; Layout.fillWidth: true; wrapMode: Text.Wrap }
                                Label { text: studio.connected ? "Discovered · readiness unknown" : "Last-known descriptor · availability unknown"; color: studio.connected ? "#6fe0bc" : "#edc078"; font.pixelSize: 11; Layout.fillWidth: true; Layout.minimumWidth: 0; wrapMode: Text.Wrap }
                                Action { text: "Start capture"; Layout.fillWidth: true; objectName: "startCapture_" + modelData.id; enabled: studio.connected && modelData.captureSupported && !studio.capturing && !studio.busy; onClicked: studio.startCapture(modelData.id) }
                            }
                        }
                        Action { text: "Stop capture"; Layout.fillWidth: true; enabled: studio.connected && studio.capturing && !studio.busy; onClicked: studio.stopCapture() }
                        Label { text: (studio.connected ? "" : "Last-known acquisition diagnostics\n") + studio.acquisitionText; visible: studio.acquisitionText.length > 0; Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: 11; color: "#a8bbcb" }
                        Rectangle { Layout.fillWidth: true; height: 1; color: "#2b3945" }
                        Caption { text: "PROCESSING RECIPE" }
                        ComboBox { id: recipe; Layout.fillWidth: true; model: ["example", "crash-test"] }
                        Action { text: "Run pipeline"; Layout.fillWidth: true; enabled: studio.connected && studio.capturing && !studio.busy; onClicked: studio.runPipeline(recipe.currentText) }
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
                                Button { text: "Re-enable plugin"; visible: modelData.state === "failed"; enabled: studio.connected && !studio.busy; onClicked: studio.enablePlugin(modelData.id, true) }
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
                            Caption { text: "LEFT · " + (studio.connected ? "NATIVE GRAYSCALE" : "LAST-KNOWN PREVIEW") }
                            MeasurementView { id: leftPreview; objectName: "leftPreview"; Layout.fillWidth: true; Layout.fillHeight: true }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true; Layout.fillHeight: true
                            Caption { text: "RIGHT · " + (studio.connected ? "NATIVE GRAYSCALE" : "LAST-KNOWN PREVIEW") }
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
                                Label { text: "Your geometry starts here"; font.pixelSize: 18; color: "#c9d8e5"; anchors.horizontalCenter: parent.horizontalCenter }
                                Label { text: "Capture → example pipeline"; color: "#8a9caf"; anchors.horizontalCenter: parent.horizontalCenter }
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
                                        Button { text: "Replay"; enabled: studio.connected && !studio.busy; onClicked: studio.replay(modelData.id, false) }
                                        Button { text: "Verify"; enabled: studio.connected && !studio.busy; onClicked: studio.replay(modelData.id, true) }
                                    }
                                }
                                MouseArea { anchors.fill: parent; enabled: studio.connected && modelData.type === "org.mantis.PointCloud" && modelData.state === "FINALIZED" && !studio.busy; onClicked: studio.selectArtifact(modelData.id) }
                            }
                        }
                        TextField { id: exportPath; placeholderText: "Export path, e.g. /tmp/model.ply"; placeholderTextColor: "#8499aa"; Layout.fillWidth: true }
                        Action { text: "Export selected · PLY"; Layout.fillWidth: true; enabled: studio.connected && studio.selectedArtifact.length > 0 && exportPath.text.length > 0 && !studio.busy; onClicked: studio.exportArtifact(exportPath.text) }
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
                                    Button { text: "Cancel"; visible: modelData.state === "Running" || modelData.state === "Queued"; enabled: studio.connected && !studio.busy; onClicked: studio.cancelJob(modelData.id) }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
