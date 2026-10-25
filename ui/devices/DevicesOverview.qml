import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../design"
import "../components"
import "../projects"
GridLayout {
    id: root
    required property var workspace
    property var device: workspace.selected
    readonly property bool wide: width >= 1300
    columns: wide ? 2 : 1; columnSpacing: 12; rowSpacing: 12
    Panel {
        Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.preferredWidth: root.wide ? (root.width - 12) / 2 : root.width
        Layout.rowSpan: root.wide ? 2 : 1; Layout.alignment: Qt.AlignTop
        implicitHeight: Math.max(360, overview.implicitHeight + 32)
        ColumnLayout {
            id: overview
            anchors.fill: parent; anchors.margins: 16; spacing: 12
            RowLayout {
                Layout.fillWidth: true
                ColumnLayout {
                    Layout.fillWidth: true; spacing: 6
                    DevicesText { objectName: "devicesSelectedTitle"; text: root.device.name || "Select a device"; color: Theme.text; font.pixelSize: 22; font.bold: true; Layout.fillWidth: true; maximumLineCount: 2; elide: Text.ElideRight }
                    DevicesText { text: root.workspace.illustrative ? "Demo / Mock · original mechanical illustration" : root.workspace.liveData.freshness; color: root.workspace.illustrative || !root.workspace.liveData.confirmed ? Theme.warning : Theme.accent; font.pixelSize: 11; Layout.fillWidth: true }
                }
                UnavailableAction { objectName: "devicesIdentify"; text: "Identify"; reason: "No public identify-device command." }
            }
            GridLayout {
                Layout.fillWidth: true; columns: overview.width >= 620 ? 2 : 1; columnSpacing: 16; rowSpacing: 12
                Rectangle {
                    Layout.fillWidth: true; Layout.preferredHeight: overview.width >= 620 ? 295 : 220
                    Layout.minimumWidth: 140; Layout.preferredWidth: overview.width >= 620 ? overview.width * .48 : overview.width
                    radius: 6
                    gradient: Gradient { GradientStop { position: 0; color: Theme.previewTop } GradientStop { position: 1; color: Theme.previewBottom } }
                    Image {
                        anchors.fill: parent; anchors.margins: 18
                        source: root.workspace.illustrative && root.device.id ? root.device.art === "scanner" ? "../home/assets/scanner.png" : root.device.art === "rotor" ? "../home/assets/rotor.png" : root.device.art === "stage" ? "../projects/assets/bracket-qc.png" : "../home/assets/housing.png" : ""
                        sourceSize.width: 520; sourceSize.height: 520; fillMode: Image.PreserveAspectFit
                    }
                    StudioIcon { visible: !root.workspace.illustrative || !root.device.id; anchors.centerIn: parent; width: 112; height: 112; name: root.device.depth ? "scan" : "devices"; color: "#518b82" }
                    DevicesText { anchors.bottom: parent.bottom; anchors.horizontalCenter: parent.horizontalCenter; bottomPadding: 8; text: root.workspace.illustrative ? "ILLUSTRATIVE STUDY" : "CAPABILITY DESCRIPTOR"; font.pixelSize: 9; font.letterSpacing: 1.4 }
                }
                ColumnLayout {
                    Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.preferredWidth: overview.width * .45; spacing: 10
                    DevicesText { text: "DEVICE PROVENANCE"; color: Theme.muted; font.pixelSize: 10; font.letterSpacing: 1; Layout.fillWidth: true }
                    DevicesText { text: "Identifier\n" + (root.device.displayId || "No selection"); Layout.fillWidth: true; maximumLineCount: 3; elide: Text.ElideMiddle }
                    DevicesText { text: "Plugin reference\n" + (root.device.plugin || "Not reported"); Layout.fillWidth: true; maximumLineCount: 3; elide: Text.ElideMiddle }
                    DevicesText { text: root.workspace.illustrative ? root.device.description || "Local demo only" : "Transport, serial, firmware, temperature and physical readiness: not reported by this runtime."; font.pixelSize: 12; Layout.fillWidth: true }
                    StudioButton { objectName: "devicesScan"; text: "Open Scan / Acquisition"; Layout.fillWidth: true; enabled: !root.workspace.illustrative && root.workspace.mode !== "mock"; onClicked: if (enabled) root.workspace.navigate("scan") }
                    UnavailableAction { objectName: "devicesFirmware"; text: "Update Firmware"; Layout.fillWidth: true; reason: "No public firmware update contract or device firmware field." }
                }
            }
        }
    }
    Panel {
        Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.preferredWidth: root.wide ? (root.width - 12) / 2 : root.width; Layout.alignment: Qt.AlignTop
        implicitHeight: preview.implicitHeight + 32
        ColumnLayout {
            id: preview
            anchors.fill: parent; anchors.margins: 16; spacing: 12
            RowLayout {
                Layout.fillWidth: true
                DevicesText { text: root.workspace.illustrative ? "Illustrative camera studies" : "Camera preview"; color: Theme.text; font.bold: true; font.pixelSize: 15; Layout.fillWidth: true }
                SourceBadge { source: root.workspace.illustrative ? "mock" : "live" }
            }
            Item {
                visible: root.workspace.illustrative
                Layout.fillWidth: true; Layout.preferredHeight: 164
                Row {
                    anchors.fill: parent; spacing: 8
                    Repeater {
                        model: ["../home/assets/housing.png", "../projects/assets/manifold.png"]
                        delegate: Rectangle {
                            required property string modelData
                            width: (preview.width - 8) / 2; height: 164; radius: 5; color: Theme.previewBottom
                            Image { anchors.fill: parent; anchors.margins: 10; source: modelData; sourceSize.width: 520; sourceSize.height: 355; fillMode: Image.PreserveAspectFit }
                            DevicesText { anchors.bottom: parent.bottom; anchors.left: parent.left; leftPadding: 9; bottomPadding: 7; text: "DEMO ART · NO CAMERA PIXELS"; font.pixelSize: 9; color: Theme.warning }
                        }
                    }
                }
            }
            DevicesText { Layout.fillWidth: true; text: root.workspace.illustrative ? "Original offline illustrations. No stream, projected light or measurement is being shown." : "No live preview in Devices. Existing preview and replay tools are available in Scan / Acquisition." }
        }
    }
    Panel {
        Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.preferredWidth: root.wide ? (root.width - 12) / 2 : root.width; Layout.alignment: Qt.AlignTop
        implicitHeight: capabilityColumn.implicitHeight + 32
        ColumnLayout {
            id: capabilityColumn
            anchors.fill: parent; anchors.margins: 16; spacing: 10
            DevicesText { text: "Capabilities & components"; color: Theme.text; font.bold: true; font.pixelSize: 15 }
            DevicesText { Layout.fillWidth: true; text: root.workspace.illustrative ? "Demo descriptions carry no runtime capabilities." : (root.device.capabilities || []).join("\n") || "No capabilities advertised / no selection"; font.pixelSize: 12 }
            DevicesText { Layout.fillWidth: true; text: root.device.id ? "Parent: " + (root.device.parent || "Top-level descriptor") + "\nDeclared children: " + ((root.device.children || []).join(" · ") || "None") : "Select a descriptor in All Devices to inspect its graph."; font.pixelSize: 12 }
        }
    }
}
