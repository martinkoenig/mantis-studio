import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../design"
import "../components"
Rectangle {
    id: root
    required property var workspace
    color: Theme.panel; radius: Theme.radius; border.color: Theme.border
    ScrollView {
        id: scroll
        objectName: "scanCameraScroll"
        anchors.fill: parent; anchors.margins: 10; clip: true
        contentWidth: availableWidth
        ScrollBar.vertical.policy: scroll.contentHeight > scroll.availableHeight + 1 ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
        ScrollBar.vertical.active: true
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        Column {
            width: parent.width; spacing: 10
            ScanText { text: "Camera Views"; color: Theme.text; font.bold: true; font.pixelSize: 13 }
            Repeater {
                model: ["Left Camera (L)", "Right Camera (R)", "RGB Camera (Texture)"]
                delegate: Rectangle {
                    required property string modelData
                    required property int index
                    width: parent.width; height: Math.min(218, Math.max(166, root.height / 3 - 24))
                    color: Theme.canvas; border.color: Theme.border; radius: 5; clip: true
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 8; spacing: 6
                        ScanText { text: modelData; color: Theme.text; font.pixelSize: 11; Layout.fillWidth: true }
                        Item {
                            Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 70
                            Rectangle { anchors.fill: parent; color: Theme.previewBottom; radius: 4 }
                            Image {
                                anchors.fill: parent; anchors.margins: 8
                                visible: root.workspace.illustrative && index < 2
                                source: "../home/assets/housing.png"; fillMode: Image.PreserveAspectFit
                                opacity: 0.75
                                rotation: index === 0 ? -12 : 10
                            }
                            StudioIcon {
                                visible: !root.workspace.illustrative || index === 2
                                anchors.centerIn: parent; width: 32; height: 32; name: "scan"; color: Theme.muted
                            }
                            ScanText {
                                anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                                padding: 5; font.pixelSize: 10; horizontalAlignment: Text.AlignHCenter
                                text: index === 2 ? "Not available · no RGB preview contract" : root.workspace.illustrative ? "Illustrative grayscale study" : "Preview in Classic Acquisition"
                                color: root.workspace.illustrative && index < 2 ? Theme.warning : Theme.secondary
                                background: Rectangle { color: "#de080f12"; radius: 3 }
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            ScanText { text: index === 2 ? "Unavailable" : root.workspace.illustrative ? "Demo / Mock" : root.workspace.presentation.confirmed ? "Stream not shown here" : "Current stream unknown"; font.pixelSize: 10; Layout.fillWidth: true }
                            StudioIcon { name: "scan"; width: 15; height: 15; color: Theme.muted }
                        }
                    }
                }
            }
        }
    }
}
