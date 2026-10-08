import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../design"
import "../components"

ScrollView {
    id: root
    required property var page
    required property var state
    signal openAcquisition()
    signal openDevices()
    signal openCalibration(string deviceId)
    clip: true
    ScrollBar.vertical.policy: root.contentHeight > root.availableHeight ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
    contentWidth: availableWidth
    ColumnLayout {
        width: root.availableWidth; spacing: Theme.gap
        Panel {
            Layout.fillWidth: true
            implicitHeight: intro.implicitHeight + 64
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: "#10312b" }
                GradientStop { position: 1; color: Theme.panel }
            }
            RowLayout {
                id: intro
                anchors.fill: parent; anchors.margins: 32; spacing: 24
                ColumnLayout {
                    Layout.fillWidth: true; spacing: 14
                    Label { text: "MANTIS STUDIO  /  " + root.page.milestone; font.pixelSize: Theme.captionSize; font.letterSpacing: 1.5; color: Theme.accent }
                    Label {
                        text: root.page.route === "home" ? "Scan. Process. Inspect. Engineer." : root.page.planned
                        font.pixelSize: root.width < 950 ? 25 : 32; font.bold: true
                        Layout.fillWidth: true; wrapMode: Text.Wrap
                    }
                    Label { text: root.page.detail; color: Theme.secondary; Layout.fillWidth: true; wrapMode: Text.Wrap }
                    Label { text: "Workspace foundation · tools planned for " + root.page.milestone; color: Theme.muted; Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: 12 }
                }
                StudioIcon { name: root.page.route; color: "#2b8e71"; Layout.preferredWidth: 72; Layout.preferredHeight: 72; visible: root.width > 820 }
            }
        }
        GridLayout {
            columns: root.width < 1000 ? 1 : 2
            columnSpacing: Theme.gap; rowSpacing: Theme.gap
            Layout.fillWidth: true
            Panel {
                Layout.fillWidth: true; Layout.alignment: Qt.AlignTop
                implicitHeight: next.implicitHeight + Theme.padding * 2
                ColumnLayout {
                    id: next
                    anchors.fill: parent; anchors.margins: Theme.padding; spacing: 16
                    Label { text: "Workspace availability"; font.pixelSize: 17; font.bold: true }
                    Label {
                        text: "This workspace is prepared for the next UI milestone. Use the existing acquisition tools for capture, replay, pipelines, artifacts, export, plugins and jobs."
                        color: Theme.secondary; Layout.fillWidth: true; wrapMode: Text.Wrap
                    }
                    StudioButton { objectName: "openAcquisition"; text: "Open existing acquisition"; primary: true; onClicked: root.openAcquisition() }
                    Label {
                        visible: root.state.mode === "mock"
                        text: "Mock is an offline preview. Existing acquisition and calibration controls are disabled in this mode."
                        color: Theme.warning; Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: 12
                    }
                    StudioButton { text: root.page.route === "devices" ? "Open Calibration · existing workflow" : "View devices & calibration"; objectName: "openCalibration"; onClicked: root.page.route === "devices" ? root.openCalibration("") : root.openDevices() }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true; Layout.alignment: Qt.AlignTop; spacing: Theme.gap
                Panel {
                    Layout.fillWidth: true; implicitHeight: connection.implicitHeight + Theme.padding * 2
                    ColumnLayout {
                        id: connection
                        anchors.fill: parent; anchors.margins: Theme.padding; spacing: 16
                        RowLayout {
                            Label { text: "Runtime & devices"; font.pixelSize: 17; font.bold: true; Layout.fillWidth: true }
                            SourceBadge { source: root.state.runtime.source }
                        }
                        StatusIndicator { status: root.state.runtime }
                        Label {
                            text: root.state.mode === "mock" ? "Deterministic sample content. No daemon or authentication token is required." : root.state.runtimeConnected ? "Device metadata comes from the current runtime snapshot. A connection alone does not establish device readiness or calibration validity." : "No runtime connection. Check mantisd, port and authentication. Live device actions are unavailable."
                            color: Theme.secondary; Layout.fillWidth: true; wrapMode: Text.Wrap
                        }
                        Label { visible: root.state.mode !== "mock" && root.state.devices.length === 0; text: root.state.runtimeConnected ? "No devices discovered." : "No live device data available."; color: Theme.muted }
                    }
                }
                // Keep dynamic delegates outside the surrounding Layout's item cache.
                // Qt 6.4 can query a removed Repeater item before its next layout polish.
                Item {
                    Layout.fillWidth: true
                    implicitHeight: deviceRows.implicitHeight
                    Column {
                        id: deviceRows
                        width: parent.width; spacing: Theme.gap
                        Repeater {
                            model: root.state.devices
                            delegate: Column {
                                required property var modelData
                                width: deviceRows.width; spacing: Theme.small
                                DeviceSummary { width: parent.width; device: modelData }
                                StudioButton {
                                    visible: root.page.route === "devices" && !modelData.synthetic
                                    enabled: modelData.actionable && modelData.capabilities.indexOf("org.mantis.camera.frameset-stream.v1") !== -1
                                    text: "Calibrate " + modelData.name
                                    onClicked: root.openCalibration(modelData.id)
                                }
                            }
                        }
                        Repeater {
                            model: root.state.demoDevices
                            delegate: DeviceSummary { required property var modelData; width: deviceRows.width; device: modelData }
                        }
                    }
                }
            }
        }
        Item { Layout.fillHeight: true }
    }
}
