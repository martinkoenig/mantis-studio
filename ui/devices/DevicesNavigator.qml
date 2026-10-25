import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../design"
import "../components"
Panel {
    id: root
    required property var workspace
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 12; spacing: 10
        DevicesText { text: "All Devices"; color: Theme.text; font.bold: true; font.pixelSize: 15 }
        DevicesText { Layout.fillWidth: true; text: root.workspace.illustrative ? root.workspace.rows.length + " demo descriptors" : root.workspace.liveCount; font.pixelSize: 11 }
        ListView {
            id: list
            objectName: "devicesNavigatorList"
            Layout.fillWidth: true; Layout.fillHeight: true
            clip: true; boundsBehavior: Flickable.StopAtBounds
            model: root.workspace.rows
            spacing: 4
            currentIndex: -1
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            function focusRow(index) {
                if (count === 0) return
                currentIndex = Math.max(0, Math.min(count - 1, index))
                positionViewAtIndex(currentIndex, ListView.Contain)
                Qt.callLater(list.focusCurrent)
            }
            function focusCurrent() { if (currentItem) currentItem.rowButton.forceActiveFocus(Qt.TabFocusReason) }
            delegate: Column {
                id: row
                required property var modelData
                required property int index
                property alias rowButton: button
                width: list.width; spacing: 6
                DevicesText {
                    visible: row.index === 0 || root.workspace.rows[row.index-1].group !== row.modelData.group
                    width: parent.width
                    text: row.modelData.group; color: Theme.secondary
                    font.pixelSize: 12; topPadding: row.index === 0 ? 0 : 14; bottomPadding: 4
                }
                Button {
                    id: button
                    objectName: "deviceRow_" + row.index
                    width: parent.width; height: 72
                    enabled: row.modelData.selectable
                    hoverEnabled: true; activeFocusOnTab: true
                    readonly property bool selected: root.workspace.selectedId === row.modelData.id
                    leftPadding: 9 + Math.min(3, row.modelData.depth) * 10; rightPadding: 8
                    Accessible.name: row.modelData.name
                    Accessible.description: (root.workspace.illustrative ? "Demo / Mock. " : "Advertised descriptor; physical readiness unknown. ") + (row.modelData.depth ? "Component of " + row.modelData.parent : "Logical device")
                    onClicked: root.workspace.choose(row.modelData.id)
                    Keys.onDownPressed: list.focusRow(row.index + 1)
                    Keys.onUpPressed: list.focusRow(row.index - 1)
                    onActiveFocusChanged: if (activeFocus) list.positionViewAtIndex(row.index, ListView.Contain)
                    background: Rectangle {
                        radius: 5
                        color: button.down ? Theme.pressed : button.selected ? Theme.selection : button.hovered ? Theme.hover : "transparent"
                        border.width: button.visualFocus ? 2 : 1
                        border.color: button.visualFocus ? Theme.focus : button.selected ? Theme.accent : "transparent"
                    }
                    contentItem: RowLayout {
                        spacing: 9
                        StudioIcon { name: row.modelData.depth ? "scan" : "devices"; color: button.selected ? Theme.accent : Theme.muted; Layout.preferredWidth: 24; Layout.preferredHeight: 24 }
                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 4
                            DevicesText { text: row.modelData.name; color: Theme.text; Layout.fillWidth: true; maximumLineCount: 1; elide: Text.ElideRight; font.bold: button.selected }
                            DevicesText { text: root.workspace.illustrative ? (row.modelData.art === "stage" ? "Demo · unavailable example" : "Demo · illustrative") : root.workspace.liveData.confirmed ? "Advertised · readiness unknown" : "Last known · stale"; Layout.fillWidth: true; font.pixelSize: 10; maximumLineCount: 2; elide: Text.ElideRight; color: root.workspace.illustrative ? Theme.warning : Theme.secondary }
                        }
                    }
                }
            }
            footer: DevicesText {
                width: list.width; topPadding: 14
                visible: list.count === 0
                text: root.workspace.liveData.confirmed ? "No devices advertised" : "Waiting for device evidence. Refresh the runtime snapshot to confirm."
            }
        }
        DevicesText { Layout.fillWidth: true; font.pixelSize: 11; text: "Descriptors describe capabilities. They do not establish physical readiness." }
    }
}
