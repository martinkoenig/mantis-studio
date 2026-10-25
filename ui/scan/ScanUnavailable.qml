import QtQuick
import QtQuick.Controls
import "../components"
StudioButton {
    property string reason: "No supported control in this Scan foundation. Use Classic Acquisition for existing workflows."
    enabled: false
    Accessible.description: reason
    HoverHandler { id: hover; objectName: "scanUnavailableHover" }
    ToolTip.visible: hover.hovered
    ToolTip.text: reason
    ToolTip.delay: 350
}
