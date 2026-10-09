import QtQuick
import QtQuick.Controls
import Mantis.Studio 1.0
Flickable {
    id: root
    clip: true
    contentWidth: Math.max(0, width - 12)
    flickableDirection: Flickable.VerticalFlick
    boundsBehavior: Flickable.StopAtBounds
    // The measured laptop swipes span ~500–5400 logical px/s. Qt's default
    // 2500px/s ceiling flattened most releases; keep that range distinguishable.
    maximumFlickVelocity: 6000
    ProjectsScrollInput { id: input; objectName: root.objectName + "Input"; viewport: root }
    function cancelScroll() { input.cancelPending(); cancelFlick() }
    onVisibleChanged: { input.cancelPending(); if (!visible) cancelFlick() }
    onHeightChanged: input.cancelPending()
    onContentHeightChanged: input.cancelPending()
    // Precise phased pixels move immediately; OS momentum is preserved. On Linux
    // gestures lacking momentum, the adapter requests one Qt native release flick.
    ScrollBar.vertical: ScrollBar { objectName: root.objectName + "ScrollBar"; policy: ScrollBar.AsNeeded; interactive: true }
    Keys.onPressed: function(event) {
        let delta = 0
        if (event.key === Qt.Key_PageDown) delta = height * .85
        else if (event.key === Qt.Key_PageUp) delta = -height * .85
        else if (event.key === Qt.Key_Home) delta = -contentHeight
        else if (event.key === Qt.Key_End) delta = contentHeight
        else return
        cancelScroll()
        contentY = Math.max(0, Math.min(Math.max(0, contentHeight - height), contentY + delta))
        event.accepted = true
    }
}
