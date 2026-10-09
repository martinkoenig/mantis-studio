import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mantis.Studio 1.0
import "../design"
import "../home"
ScrollView {
    id: root
    required property var bridge
    property string mode: "live"
    // Logical pixels: allow modest growth beyond the approved composition.
    readonly property real maximumContentWidth: 1440
    readonly property alias liveModel: live
    signal navigate(string route)
    clip: true
    contentWidth: availableWidth
    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
    ScrollBar.vertical.policy: contentHeight > availableHeight ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
    function showExamples() {
        const target = mode === "hybrid" ? showcase : dashboard.projectGallery
        const flick = contentItem
        flick.contentY = Math.max(0, Math.min(target.mapToItem(flick.contentItem, 0, 0).y, flick.contentHeight - flick.height))
        target.forceActiveFocus(Qt.TabFocusReason)
    }
    HomeModel { id: live; bridge: root.mode === "mock" ? null : root.bridge }
    HomeDemo { id: demo }
    Item {
        width: root.availableWidth
        implicitHeight: content.implicitHeight
        ColumnLayout {
            id: content
            objectName: "homeContent"
            width: Math.min(parent.width, root.maximumContentWidth)
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: Theme.padding
            HomeDashboard {
                id: dashboard
                Layout.fillWidth: true
                hasExamples: root.mode !== "live"
                onRevealExamples: root.showExamples()
                snapshot: root.mode === "mock" ? demo.data : live.data
                demoProjects: demo.projects
                onNavigate: function(route) { root.navigate(route) }
            }
            ColumnLayout {
                visible: root.mode === "hybrid"
                Layout.fillWidth: true; spacing: Theme.gap
                HomeText { objectName: "homeHybridSeparation"; text: "Demo / Mock showcase · separate from the live snapshot above"; color: Theme.warning; font.bold: true; Layout.fillWidth: true }
                Item {
                    Layout.fillWidth: true; implicitHeight: showcase.implicitHeight
                    HomeProjects { id: showcase; width: parent.width; projects: demo.projects }
                }
            }
        }
    }
}
