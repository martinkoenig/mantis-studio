import QtQuick
import QtQuick.Controls
import "../design"
Button {
    id: control
    property bool primary: false
    implicitHeight: Theme.buttonHeight
    horizontalPadding: Theme.gap
    hoverEnabled: true
    activeFocusOnTab: true
    opacity: enabled ? 1 : 0.45
    contentItem: Text {
        text: control.text
        font: control.font
        color: control.primary ? Theme.canvas : Theme.text
        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    background: Rectangle {
        radius: Theme.radius
        color: control.down ? Theme.pressed : control.primary ? Theme.accent : control.hovered ? Theme.hover : Theme.raised
        border.width: control.visualFocus ? 2 : 1
        border.color: control.visualFocus ? Theme.focus : control.primary ? Theme.accent : Theme.border
    }
    Accessible.name: text
}
