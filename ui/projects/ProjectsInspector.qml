import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../design"
import "../components"
import "../home"
Panel {
    id: root
    property bool illustrative: true
    property var sample: ({name:"", image:"",description:"",type:"",tag:"",date:""})
    property var live: ({name:"",path:"",selected:{},artifacts:[],jobs:[],events:[],available:false,confirmed:false})
    readonly property bool artifactSelected: !illustrative && !!live.selectedId
    readonly property string selectionName: illustrative ? sample.name : artifactSelected ? live.selected.displayId : live.name
    signal inspectRuntime()
    implicitHeight: body.implicitHeight + 28
    Accessible.role: Accessible.Pane
    Accessible.name: "Selected project details"
    ColumnLayout {
        id: body
        anchors.fill: parent; anchors.margins: 14; spacing: 8
        Item {
            Layout.fillWidth: true; implicitHeight: 150
            HomePreview { anchors.fill: parent; visible: root.illustrative; source: root.sample.image ? Qt.resolvedUrl(root.sample.image) : "" }
            Rectangle {
                anchors.fill: parent; visible: !root.illustrative; color: Theme.canvas; radius: Theme.radius
                ColumnLayout {
                    anchors.centerIn: parent; spacing: 12
                    StudioIcon { name: "projects"; color: Theme.muted; Layout.alignment: Qt.AlignHCenter; Layout.preferredWidth: 44; Layout.preferredHeight: 44 }
                    ProjectsText { text: "Preview unavailable"; color: Theme.secondary }
                    ProjectsText { text: "No live geometry loaded"; color: Theme.muted; font.pixelSize: 10 }
                }
            }
        }
        ProjectsText { objectName: "projectsInspectorName"; text: root.selectionName; font.bold: true; font.pixelSize: 17; Layout.fillWidth: true; wrapMode: Text.Wrap }
        SourceBadge { source: root.illustrative ? "mock" : "live" }
        ProjectsText { text: root.illustrative ? "Illustrative sample · no runtime identity" : root.live.freshness || "Runtime state unconfirmed"; color: Theme.warning; wrapMode: Text.Wrap; Layout.fillWidth: true; font.pixelSize: 10 }
        ProjectsText { text: root.illustrative ? root.sample.type + "  ·  " + root.sample.tag : root.artifactSelected ? root.live.selected.type : "Current runtime project only"; color: "#9ccbdd"; wrapMode: Text.Wrap; Layout.fillWidth: true }
        ProjectsText { text: "Description"; color: Theme.muted; font.pixelSize: 11; Layout.topMargin: 4 }
        ProjectsText { text: root.illustrative ? root.sample.description : "Not supplied by this runtime"; wrapMode: Text.Wrap; Layout.fillWidth: true }
        ProjectsText { text: root.illustrative ? "Sample library / " + root.sample.name : root.live.path || "Project path unavailable"; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true; color: Theme.secondary; font.pixelSize: 11 }
        ProjectsText { text: root.illustrative ? "Modified  " + root.sample.date + " (illustrative)" : root.artifactSelected ? "State  " + root.live.selected.state + "\nChunks  " + root.live.selected.chunks : "Created / modified / size / scanner\nUnavailable · not supplied by runtime"; wrapMode: Text.Wrap; Layout.fillWidth: true; color: Theme.secondary; font.pixelSize: 11 }
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }
        ProjectsText { text: root.illustrative ? "Illustrative Project Contents" : "Current Project Contents"; font.bold: true }
        ProjectsText {
            text: root.illustrative ? "▸  Raw Scans                       3 examples\n▸  Processed Data               2 examples\n▸  Meshes                           1 example\n▸  Textures                          1 example\n▸  CAD Models                    1 example\n▸  Measurements                Unavailable\n▸  Reports                           1 example" : root.live.summary || "Artifact descriptors unavailable"
            wrapMode: Text.Wrap; Layout.fillWidth: true; color: Theme.secondary; font.pixelSize: 11; lineHeight: 1.6
        }
        ProjectsText { visible: !root.illustrative; text: "Runtime-wide jobs/events\nProject association unavailable"; color: Theme.warning; font.pixelSize: 10; wrapMode: Text.Wrap; Layout.fillWidth: true }
        ProjectsText { visible: !root.illustrative; text: {
                const jobs = root.live.jobs || []; const events = root.live.events || []
                return jobs.map(function(j) { return j.name + " · " + j.state + (j.detail ? "\n" + j.detail : "") }).concat(events.map(function(e) { return e.name + (e.detail ? "\n" + e.detail : "") })).join("\n") || "No entries in the bounded snapshot view"
            }
            wrapMode: Text.Wrap; Layout.fillWidth: true; color: Theme.secondary; font.pixelSize: 10 }
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }
        ProjectsText { text: "Quick Actions"; font.bold: true }
        StudioButton {
            objectName: "projectsInspectRuntime"; Layout.fillWidth: true; text: "View runtime artifacts"; primary: true
            enabled: !root.illustrative && root.live.confirmed === true && root.live.available === true
            onClicked: if (enabled) root.inspectRuntime()
            Accessible.description: root.illustrative ? "Sample details are read-only; no runtime identity" : "Navigate to existing Acquisition. Does not open, load or change a project."
            Accessible.onPressAction: if (enabled && visible) clicked()
        }
        RowLayout {
            Layout.fillWidth: true
            UnavailableAction { objectName: "projectsClone"; Layout.fillWidth: true; text: "Duplicate"; reason: "No public project clone contract." }
            UnavailableAction { objectName: "projectsExport"; Layout.fillWidth: true; text: "Export project"; reason: "Project package export is unavailable; artifact export remains in Acquisition." }
        }
        UnavailableAction { objectName: "projectsArchive"; Layout.fillWidth: true; text: "Archive project"; reason: "No public project archive contract." }
        UnavailableAction { objectName: "projectsDelete"; Layout.fillWidth: true; text: "Delete project"; reason: "No public destructive project operation or permission contract." }
        ProjectsText { text: "Clone, export package, archive, delete and sharing are unavailable."; wrapMode: Text.Wrap; Layout.fillWidth: true; color: Theme.muted; font.pixelSize: 10 }
    }
}
