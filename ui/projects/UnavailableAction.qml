import QtQuick
import QtQuick.Controls
import "../components"
StudioButton {
    id: root
    property string reason: "No public runtime contract is available."
    enabled: false
    Accessible.description: reason
    Accessible.onPressAction: { /* Deliberately inert, including direct accessible press. */ }
    ToolTip.text: reason
    ToolTip.visible: hover.hovered
    HoverHandler { id: hover }
}
