import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../design"
import "../components"
import "../home"
Panel {
    id: root
    property bool illustrative: true
    property bool compact: false
    property var sample: ({name:"", image:"",description:"",type:"",tag:"",date:""})
    property var live: ({name:"",path:"",selected:{},artifacts:[],jobs:[],events:[],available:false,confirmed:false})
    readonly property bool artifactSelected: !illustrative && !!live.selectedId
    readonly property string selectionName: illustrative ? sample.name : artifactSelected ? live.selected.displayId : live.name
    signal inspectRuntime()
    implicitHeight: body.implicitHeight + 28
    Accessible.role: Accessible.Pane
    Accessible.name: "Selected project details"
    ProjectsViewport {
        objectName: "projectsInspectorViewport"
        anchors.fill: parent
        contentHeight: body.implicitHeight + 28
        ColumnLayout {
            id: body
            x: 14; y: 14; width: parent.parent.contentWidth - 28; spacing: 8
            Item {
                Layout.fillWidth: true; implicitHeight: Math.min(root.compact ? 128 : 220, root.width * .54)
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
            RowLayout {
                Layout.fillWidth: true
                ProjectsText { objectName: "projectsInspectorName"; text: root.selectionName; font.bold: true; font.pixelSize: 20; Layout.fillWidth: true; wrapMode: Text.Wrap }
                ProjectsText { visible: root.illustrative && root.sample.favorite === true; text: "★"; color: Theme.warning; font.pixelSize: 21; Accessible.name: "Illustrative favorite" }
            }
            SourceBadge { source: root.illustrative ? "mock" : "live" }
            ProjectsText { text: root.illustrative ? "Illustrative sample · no runtime identity" : root.live.freshness || "Runtime state unconfirmed"; color: Theme.warning; wrapMode: Text.Wrap; Layout.fillWidth: true; font.pixelSize: 10 }
            Flow {
                Layout.fillWidth: true; spacing: 6
                ProjectsChip { text: root.illustrative ? root.sample.type : root.artifactSelected ? root.live.selected.type : "Current project" }
                ProjectsChip { visible: root.illustrative; text: root.sample.tag }
            }
            GridLayout {
                Layout.fillWidth: true; columns: 2; columnSpacing: 14; rowSpacing: 8
                ProjectsText { text: "Description"; color: Theme.muted; Layout.alignment: Qt.AlignTop; font.pixelSize: 12 }
                ProjectsText { text: root.illustrative ? root.sample.description : "Not supplied by this runtime"; wrapMode: Text.Wrap; Layout.fillWidth: true }
                ProjectsText { text: "Location"; color: Theme.muted; font.pixelSize: 12 }
                ProjectsText { text: root.illustrative ? "Sample library / " + root.sample.name : root.live.path || "Unavailable"; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true; color: Theme.secondary }
                ProjectsText { text: root.illustrative ? "Modified" : "State"; color: Theme.muted; font.pixelSize: 12 }
                ProjectsText { text: root.illustrative ? root.sample.date + " (sample)" : root.artifactSelected ? root.live.selected.state : "Unavailable"; wrapMode: Text.Wrap; Layout.fillWidth: true; color: Theme.secondary }
                ProjectsText { text: root.illustrative ? "Type" : "Chunks"; color: Theme.muted; font.pixelSize: 12 }
                ProjectsText { text: root.illustrative ? root.sample.type : root.artifactSelected ? root.live.selected.chunks : "Unavailable"; wrapMode: Text.Wrap; Layout.fillWidth: true; color: Theme.secondary }
                ProjectsText { text: "Scanner"; color: Theme.muted; font.pixelSize: 12 }
                ProjectsText { text: "Unavailable"; color: Theme.secondary; Layout.fillWidth: true }
            }
            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }
            ProjectsText { text: root.illustrative ? "Illustrative Project Contents" : "Current Project Contents"; font.bold: true }
            Item {
                Layout.fillWidth: true; implicitHeight: root.illustrative ? tree.implicitHeight : runtimeSummary.implicitHeight
                Column {
                    id: tree; width: parent.width; spacing: 6; visible: root.illustrative
                    Repeater {
                        model: ["Raw Scans", "Processed Data", "Meshes", "Textures", "CAD Models", "Measurements", "Reports"]
                        delegate: RowLayout {
                            required property string modelData
                            required property int index
                            width: tree.width; spacing: 8
                            ProjectsText { text: "›"; color: Theme.muted }
                            StudioIcon { name: index === 0 ? "scan" : index === 4 ? "reverse" : "projects"; color: ["#edc94c", "#aa6de3", "#24c88d", "#ef963e", "#3996ed", "#24c88d", "#94a1aa"][index]; Layout.preferredWidth: 16; Layout.preferredHeight: 16 }
                            ProjectsText { text: modelData; Layout.fillWidth: true; color: Theme.secondary }
                            ProjectsText { text: ["3 samples", "2 samples", "1 sample", "1 sample", "1 sample", "Unavailable", "1 sample"][index]; font.pixelSize: 11; color: Theme.muted }
                        }
                    }
                }
                ProjectsText { id: runtimeSummary; visible: !root.illustrative; width: parent.width; text: root.live.summary || "Artifact descriptors unavailable"; wrapMode: Text.Wrap; color: Theme.secondary }
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
                objectName: "projectsInspectRuntime"; implicitHeight: 32; Layout.fillWidth: true; text: "View runtime artifacts"; primary: true
                enabled: !root.illustrative && root.live.confirmed === true && root.live.available === true
                onClicked: if (enabled) root.inspectRuntime()
                Accessible.description: root.illustrative ? "Sample details are read-only; no runtime identity" : "Navigate to existing Acquisition. Does not open, load or change a project."
                Accessible.onPressAction: if (enabled && visible) clicked()
            }
            RowLayout {
                Layout.fillWidth: true
                UnavailableAction { objectName: "projectsClone"; Layout.fillWidth: true; implicitHeight: 32; text: "Duplicate"; reason: "No public project clone contract." }
                UnavailableAction { objectName: "projectsExport"; Layout.fillWidth: true; implicitHeight: 32; text: "Export project"; reason: "Project package export is unavailable; artifact export remains in Acquisition." }
            }
            UnavailableAction { objectName: "projectsArchive"; Layout.fillWidth: true; implicitHeight: 32; text: "Archive project"; reason: "No public project archive contract." }
            UnavailableAction { objectName: "projectsDelete"; Layout.fillWidth: true; implicitHeight: 32; text: "Delete project"; reason: "No public destructive project operation or permission contract." }
            ProjectsText { text: "Clone, export package, archive, delete and sharing are unavailable."; wrapMode: Text.Wrap; Layout.fillWidth: true; color: Theme.muted; font.pixelSize: 10 }
        }
    }
}
