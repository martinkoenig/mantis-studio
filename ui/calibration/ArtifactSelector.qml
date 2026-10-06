import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
ColumnLayout {
    id: root
    required property var controller
    required property string slot
    property string title: "Select existing " + slot
    Layout.fillWidth: true
    Label { text: root.title; font.bold: true }
    ComboBox {
        id: picker
        Layout.fillWidth: true
        model: root.controller.choices(root.slot)
        textRole: "label"; valueRole: "id"
        currentIndex: -1
        displayText: currentIndex < 0 ? "Choose a finalized revision…" : currentText
        onActivated: root.controller.selectArtifact(root.slot, currentValue)
    }
    Label {
        Layout.fillWidth: true; wrapMode: Text.Wrap; opacity: 0.7
        text: root.controller.state.selected[root.slot] || "No revision selected"
        font.pixelSize: 11
    }
}
