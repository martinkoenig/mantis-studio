import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../design"
Panel {
    id: root
    required property var device
    implicitHeight: body.implicitHeight + Theme.padding * 2
    ColumnLayout {
        id: body
        anchors.fill: parent; anchors.margins: Theme.padding; spacing: 12
        RowLayout {
            Layout.fillWidth: true
            StudioIcon { name: "devices"; color: root.device.synthetic ? Theme.warning : Theme.accent }
            Label { text: root.device.name; font.bold: true; Layout.fillWidth: true; elide: Text.ElideRight }
            SourceBadge { source: root.device.source }
        }
        Label { text: root.device.status; color: root.device.synthetic ? Theme.warning : Theme.secondary; Layout.fillWidth: true; wrapMode: Text.Wrap }
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }
        Label { text: "CAPABILITIES"; color: Theme.muted; font.pixelSize: Theme.captionSize; font.letterSpacing: 1 }
        Label { text: root.device.capabilityLabel; color: Theme.secondary; Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: Theme.captionSize }
    }
}
