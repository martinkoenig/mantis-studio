import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../design"
import "../components"
Rectangle {
    id: root
    required property var workspace
    color: Theme.canvas; radius: Theme.radius; border.color: Theme.border; clip: true
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 12; spacing: 8
        RowLayout {
            Layout.fillWidth: true; spacing: 5
            ScanText { text: "Display:"; visible: root.width > 500; font.pixelSize: 11 }
            Repeater {
                model: ["Coverage", "Geometry", "Texture", "Confidence"]
                StudioButton {
                    required property string modelData
                    objectName: "scanDisplay" + modelData
                    text: modelData; primary: root.workspace.displayStyle === modelData
                    Layout.fillWidth: true; Layout.maximumWidth: 104; implicitHeight: 32; font.pixelSize: 11
                    Accessible.description: "Presentation tab only. No geometry, coverage or measurement mode is changed."
                    onClicked: root.workspace.displayStyle = modelData
                }
            }
            Item { Layout.fillWidth: true }
        }
        Item {
            id: stage
            objectName: "scanViewportStage"
            Layout.fillWidth: true; Layout.fillHeight: true
            // Static Qt primitives: no Canvas, animation, render backend, pixels or point packets.
            Repeater {
                model: 14
                Rectangle { required property int index; x: index * stage.width / 13; width: 1; height: stage.height; color: "#142127" }
            }
            Repeater {
                model: 10
                Rectangle { required property int index; y: index * stage.height / 9; height: 1; width: stage.width; color: "#142127" }
            }
            Image {
                objectName: "scanDemoStudy"
                visible: root.workspace.illustrative
                anchors.horizontalCenter: parent.horizontalCenter; anchors.top: parent.top
                anchors.topMargin: Math.max(12, parent.height * 0.04)
                width: parent.width * 0.85; height: Math.max(0, parent.height - demoCaption.implicitHeight - 62)
                source: "../home/assets/housing.png"; fillMode: Image.PreserveAspectFit
            }
            Column {
                visible: !root.workspace.illustrative
                anchors.centerIn: parent; width: Math.min(410, parent.width - 28); spacing: 16
                StudioIcon { anchors.horizontalCenter: parent.horizontalCenter; name: "scan"; width: 50; height: 50; color: Theme.accent }
                ScanText { width: parent.width; text: "Your scan workspace"; font.pixelSize: 24; color: Theme.text; font.bold: true; horizontalAlignment: Text.AlignHCenter }
                ScanText { width: parent.width; text: "Active capture, camera previews and geometry are available in Classic Acquisition."; horizontalAlignment: Text.AlignHCenter; font.pixelSize: 13 }
                StudioButton { objectName: "scanViewportClassic"; text: "Open Classic Acquisition"; primary: true; anchors.horizontalCenter: parent.horizontalCenter; enabled: root.workspace.mode !== "mock"; onClicked: root.workspace.openClassic() }
            }
            ScanText {
                id: demoCaption
                visible: root.workspace.illustrative
                anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.bottomMargin: 10
                text: "Mechanical housing · illustrative study\n" + root.workspace.displayStyle + " preview chrome · no reconstructed geometry or measured coverage"
                horizontalAlignment: Text.AlignHCenter; color: Theme.warning; font.pixelSize: 12
            }
            ScanText {
                anchors.left: parent.left; anchors.top: parent.top; anchors.margins: 8
                text: root.workspace.illustrative ? "ILLUSTRATIVE / DEMO" : "VIEWPORT INTEGRATION PENDING"
                font.pixelSize: 10; color: root.workspace.illustrative ? Theme.warning : Theme.muted
            }
            ScanText { anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 8; text: "+X right\n+Y forward\n+Z up · mm"; font.pixelSize: 10; color: Theme.muted }
        }
        ScanText { objectName: "scanDisplayAvailability"; text: root.workspace.displayStyle + " · presentation selection only / data unavailable"; font.pixelSize: 11; Layout.fillWidth: true }
        ScanText {
            visible: !root.workspace.illustrative
            text: "Selected cloud: " + root.workspace.presentation.selectedArtifact + "\nLast advertised cloud (source order): " + root.workspace.presentation.latestPointCloud
            font.pixelSize: 10; Layout.fillWidth: true; maximumLineCount: 2; elide: Text.ElideRight
        }
    }
}
