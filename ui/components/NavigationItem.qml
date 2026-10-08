import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../design"
Button {
    id: control
    required property string route
    property bool selected: false
    implicitHeight: Theme.rowHeight
    hoverEnabled: true
    activeFocusOnTab: true
    opacity: enabled ? 1 : 0.45
    Accessible.name: text
    Accessible.description: selected ? "Current workspace" : "Open workspace"
    background: Rectangle {
        radius: Theme.radius
        color: control.down ? Theme.pressed : control.selected ? Theme.selection : control.hovered ? Theme.hover : "transparent"
        border.color: control.visualFocus ? Theme.focus : "transparent"
        border.width: 2
        Rectangle { width: 3; radius: 1; anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom; anchors.margins: 5; visible: control.selected; color: Theme.accent }
    }
    contentItem: RowLayout {
        spacing: 12
        StudioIcon { name: control.route; color: control.selected ? Theme.accent : Theme.secondary; Layout.leftMargin: 8 }
        Label { text: control.text; color: control.selected ? Theme.text : Theme.secondary; Layout.fillWidth: true; font.pixelSize: Theme.bodySize }
    }
}
