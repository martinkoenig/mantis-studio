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
    contentItem: Text {
        text: control.text
        textFormat: Text.PlainText
        font: control.font
        color: !control.enabled ? Theme.muted : control.primary ? Theme.canvas : Theme.text
        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    background: Rectangle {
        radius: Theme.radius
        color: !control.enabled ? Theme.raised : control.primary ? (control.down ? Theme.accentPressed : control.hovered ? Theme.accentHover : Theme.accent) : control.down ? Theme.pressed : control.hovered ? Theme.hover : Theme.raised
        border.width: control.visualFocus ? 2 : 1
        border.color: control.enabled && control.visualFocus ? Theme.focus : control.enabled && control.primary ? color : Theme.border
    }
    HoverHandler { cursorShape: control.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }
    Accessible.name: text
}
