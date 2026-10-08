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
    readonly property alias liveModel: live
    signal navigate(string route)
    clip: true
    contentWidth: availableWidth
    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
    ScrollBar.vertical.policy: contentHeight > availableHeight ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
    HomeModel { id: live; bridge: root.mode === "mock" ? null : root.bridge }
    HomeDemo { id: demo }
    ColumnLayout {
        width: root.availableWidth; spacing: Theme.padding
        HomeDashboard {
            Layout.fillWidth: true
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
