import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../design"
import "../components"
Item {
    id: root
    required property var workspace
    property string tab: "Scan Sequence"
    readonly property bool split: width >= 1100
    ColumnLayout {
        anchors.fill: parent; spacing: 8
        RowLayout {
            visible: !root.split; Layout.fillWidth: true
            Repeater {
                model: ["Scan Sequence", "Scan Timeline", "Markers"]
                StudioButton {
                    required property string modelData
                    objectName: "scanDock" + modelData.replace(" ", "")
                    text: modelData; primary: root.tab === modelData; implicitHeight: 30; font.pixelSize: 11
                    onClicked: root.tab = modelData
                }
            }
        }
        Item {
            Layout.fillWidth: true; Layout.fillHeight: true
            Rectangle {
                id: sequence
                objectName: "scanSequence"
                visible: root.split || root.tab === "Scan Sequence"
                anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
                width: root.split ? parent.width * 0.3 : parent.width
                color: Theme.panel; border.color: Theme.border; radius: Theme.radius
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 10; spacing: 6
                    ScanText { text: "Scan Sequence"; color: Theme.text; font.bold: true; Layout.fillWidth: true }
                    ScanText { text: "Available items · source order, no session chronology"; font.pixelSize: 10; Layout.fillWidth: true }
                    ListView {
                        id: items
                        objectName: "scanSequenceList"
                        Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 4
                        model: root.workspace.presentation.artifacts
                        ScrollBar.vertical: ScrollBar { policy: items.contentHeight > items.height + 1 ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff; active: true }
                        delegate: StudioButton {
                            required property var modelData
                            required property int index
                            objectName: "scanArtifactRow_" + index
                            width: items.width; height: 38; text: modelData.name + " · " + modelData.state + " · " + modelData.label
                            primary: root.workspace.selectedRow === modelData.id
                            Keys.onDownPressed: { if (index + 1 < items.count) { items.currentIndex = index + 1; items.positionViewAtIndex(items.currentIndex, ListView.Contain); if (items.currentItem) items.currentItem.forceActiveFocus(Qt.TabFocusReason) } }
                            Keys.onUpPressed: { if (index > 0) { items.currentIndex = index - 1; items.positionViewAtIndex(items.currentIndex, ListView.Contain); if (items.currentItem) items.currentItem.forceActiveFocus(Qt.TabFocusReason) } }
                            Accessible.description: "Local presentation selection only; does not load, verify or replay an artifact"
                            onClicked: root.workspace.selectedRow = modelData.id
                        }
                        ScanText { visible: items.count === 0; anchors.centerIn: parent; width: parent.width - 12; horizontalAlignment: Text.AlignHCenter; text: root.workspace.presentation.hasSnapshot ? "No suitable artifacts advertised" : "No runtime items available"; font.pixelSize: 11 }
                    }
                }
            }
            Rectangle {
                id: timeline
                objectName: "scanTimeline"
                visible: root.split || root.tab === "Scan Timeline"
                anchors.top: parent.top; anchors.bottom: parent.bottom
                x: root.split ? sequence.width + 10 : 0
                width: root.split ? parent.width * 0.4 - 20 : parent.width
                color: Theme.panel; border.color: Theme.border; radius: Theme.radius
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 10; spacing: 8
                    ScanText { text: "Scan Timeline"; color: Theme.text; font.bold: true }
                    ScanText { text: "Timing and waveform unavailable. Replay / Verify remain in Classic Acquisition."; Layout.fillWidth: true; font.pixelSize: 11 }
                    Rectangle {
                        Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 20
                        color: Theme.canvas; border.color: Theme.border; radius: 4
                        Rectangle { anchors.verticalCenter: parent.verticalCenter; anchors.left: parent.left; anchors.right: parent.right; anchors.margins: 10; height: 1; color: Theme.border }
                        ScanText { anchors.centerIn: parent; text: "No timeline contract"; font.pixelSize: 11; padding: 6; background: Rectangle { color: Theme.canvas } }
                    }
                    ScanText { text: root.workspace.presentation.jobs.length ? root.workspace.presentation.jobs[0].name + " · " + root.workspace.presentation.jobs[0].state : "Processing: not reported"; Layout.fillWidth: true; font.pixelSize: 11; maximumLineCount: 1; elide: Text.ElideRight }
                }
            }
            Rectangle {
                objectName: "scanMarkers"
                visible: root.split || root.tab === "Markers"
                anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom
                width: root.split ? parent.width * 0.3 : parent.width
                color: Theme.panel; border.color: Theme.border; radius: Theme.radius
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 10; spacing: 6
                    ScanText { text: "Markers"; color: Theme.text; font.bold: true }
                    ScanText { text: "Detection and tracking: unavailable"; Layout.fillWidth: true; font.pixelSize: 11 }
                    ScanText { text: "No marker count or saved map is reported by this source."; Layout.fillWidth: true; font.pixelSize: 11 }
                    Item { Layout.fillHeight: true }
                    ScanUnavailable { objectName: "scanMarkerMap"; text: "Capture marker map · unavailable"; Layout.fillWidth: true }
                }
            }
        }
    }
}
