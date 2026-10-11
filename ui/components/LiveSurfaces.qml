import QtQuick
import Mantis.Studio 1.0
LiveViews {
    id: root
    objectName: "liveSurfaces"
    property bool interactive: false
    function resetView() { if (interactive) { cloud.yaw = -35; cloud.pitch = 35; cloud.zoom = 1 } }
    MouseArea {
        objectName: "geometryInteraction"
        parent: root.cloud
        anchors.fill: parent
        enabled: root.interactive
        property real lastX
        property real lastY
        onPressed: function(mouse) { lastX = mouse.x; lastY = mouse.y }
        onPositionChanged: function(mouse) { if (pressed) { root.cloud.yaw += (mouse.x-lastX)*0.5; root.cloud.pitch += (mouse.y-lastY)*0.5; lastX=mouse.x; lastY=mouse.y } }
        onWheel: function(wheel) { root.cloud.zoom *= wheel.angleDelta.y > 0 ? 1.1 : 0.9 }
    }
}
