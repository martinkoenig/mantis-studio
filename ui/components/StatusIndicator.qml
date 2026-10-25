import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../design"
RowLayout {
    id: root
    required property var status
    spacing: Theme.small
    Rectangle { implicitWidth: 7; implicitHeight: 7; radius: 4; color: root.status.connected ? Theme.accent : root.status.source === "mock" ? Theme.warning : Theme.muted }
    Label { objectName: "runtimeStatusLabel"; text: root.status.label; color: Theme.secondary; font.pixelSize: Theme.bodySize }
    Accessible.name: status.label
}
