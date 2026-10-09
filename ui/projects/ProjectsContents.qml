import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../design"
import "../components"
Panel {
    id: root
    property bool illustrative: true
    property var sample: ({name:""})
    property var live: ({artifacts:[],selectedId:"",summary:""})
    property string tab: illustrative ? "Versions" : "Artifacts"
    readonly property var tabs: ["Artifacts", "Versions", "Scans", "Meshes", "Textures", "CAD Models", "Measurements", "Reports", "Notes"]
    readonly property bool versions: illustrative && tab === "Versions"
    readonly property var rows: {
        if (!illustrative) {
            const a = live.artifacts || []
            if (tab === "Artifacts") return a
            // Only published RawCapture and PointCloud identities justify scan grouping.
            if (tab === "Scans") return a.filter(function(r) { return r.scan === true })
            if (tab === "Meshes") return a.filter(function(r) { return r.mesh === true })
            return []
        }
        if (tab === "Notes" || tab === "Measurements") return []
        if (tab === "Versions") return [
            {id:"v1.3",type:"2026-10-05 14:32",state:"JS",chunks:"620 MB",detail:"Added fillets and final CAD model"},
            {id:"v1.2",type:"2026-10-03 11:21",state:"JS",chunks:"480 MB",detail:"Processed mesh study"},
            {id:"v1.1",type:"2026-10-02 16:08",state:"JS",chunks:"310 MB",detail:"Initial processing study"},
            {id:"v1.0",type:"2026-10-01 10:14",state:"JS",chunks:"950 MB",detail:"Raw scan example"}]
        return [{id:sample.name + " · " + tab,type:tab,state:"Illustrative",chunks:"Unavailable",detail:"Read-only sample contents; no runtime artifact"}]
    }
    signal selectArtifact(string id)
    implicitHeight: body.implicitHeight + 20
    Accessible.role: Accessible.Pane
    Accessible.name: "Project contents"
    ColumnLayout {
        id: body
        anchors.fill: parent; anchors.margins: 10; spacing: 8
        Flow {
            id: tabFlow
            Layout.fillWidth: true
            spacing: 2
            Repeater {
                model: root.tabs
                delegate: StudioButton {
                    required property string modelData
                    objectName: "projectsTab" + modelData.replace(/ /g, "")
                    width: Math.max(80, implicitWidth); implicitHeight: 32
                    text: modelData; primary: root.tab === modelData
                    onClicked: root.tab = modelData
                    Accessible.role: Accessible.PageTab
                    Accessible.description: root.illustrative ? "Read-only illustrative contents" : modelData === "Artifacts" || modelData === "Scans" || modelData === "Meshes" ? "Read-only snapshot artifact descriptors" : "Opens explanation of unavailable runtime content"
                    Accessible.onPressAction: if (enabled && visible) clicked()
                }
            }
        }
        ProjectsText { objectName: "projectsContentsSource"; text: root.illustrative ? "Illustrative contents / versions / author / sizes · " + root.sample.name : root.live.summary || "Artifact descriptors unavailable"; wrapMode: Text.Wrap; Layout.fillWidth: true; color: Theme.warning; font.pixelSize: 10 }
        Item {
            Layout.fillWidth: true; implicitHeight: root.rows.length ? 30 + root.rows.length * 32 : 100
            Rectangle { width: parent.width; height: 28; color: Theme.chrome }
            Row {
                width: parent.width; height: 28
                ProjectsText { width: parent.width * .25; height: 28; verticalAlignment: Text.AlignVCenter; text: root.versions ? "Version" : "Artifact ID"; color: Theme.muted }
                ProjectsText { width: parent.width * .32; height: 28; verticalAlignment: Text.AlignVCenter; text: root.versions ? "Date / description" : "Type"; color: Theme.muted }
                ProjectsText { width: parent.width * .25; height: 28; verticalAlignment: Text.AlignVCenter; text: root.versions ? "Author" : "State"; color: Theme.muted }
                ProjectsText { width: parent.width * .18; height: 28; verticalAlignment: Text.AlignVCenter; text: root.versions ? "Size" : "Chunks"; color: Theme.muted }
            }
            Repeater {
                model: 128
                delegate: Button {
                    id: artifact
                    required property int index
                    readonly property var row: root.rows[index] || ({id:"",displayId:"",type:"",state:"",chunks:"",detail:""})
                    objectName: "projectsArtifactRow" + index
                    x: 0; y: 30 + index * 32; width: parent.width; height: 32
                    visible: index < root.rows.length
                    padding: 0; hoverEnabled: true; activeFocusOnTab: !root.illustrative
                    onClicked: if (!root.illustrative) root.selectArtifact(row.id)
                    Accessible.role: root.illustrative ? Accessible.StaticText : Accessible.Button
                    Accessible.name: (row.displayId || row.id) + " · " + row.type + " · " + row.state + " · chunks " + row.chunks
                    Accessible.description: root.illustrative ? "Illustrative metadata only" : "Select bounded read-only descriptor. Does not load artifact data."
                    Accessible.onPressAction: if (enabled && visible && !root.illustrative) clicked()
                    contentItem: Row {
                        ProjectsText { width: parent.width * .25; height: 32; verticalAlignment: Text.AlignVCenter; text: artifact.row.displayId || artifact.row.id; font.pixelSize: 11 }
                        ProjectsText { width: parent.width * .32; height: 32; verticalAlignment: Text.AlignVCenter; text: root.versions ? artifact.row.type + "  " + artifact.row.detail : artifact.row.type; font.pixelSize: 11 }
                        ProjectsText { width: parent.width * .25; height: 32; verticalAlignment: Text.AlignVCenter; text: artifact.row.state; font.pixelSize: 11; color: Theme.secondary }
                        ProjectsText { width: parent.width * .18; height: 32; verticalAlignment: Text.AlignVCenter; text: artifact.row.chunks; font.pixelSize: 11; color: Theme.secondary }
                    }
                    background: Rectangle {
                        color: artifact.visualFocus ? Theme.hover : (!root.illustrative && root.live.selectedId === artifact.row.id) || (root.versions && artifact.index === 0) ? Theme.selection : artifact.hovered ? Theme.hover : Theme.canvas
                        border.width: artifact.visualFocus ? 2 : 0; border.color: Theme.focus
                        Rectangle { y: parent.height - 1; width: parent.width; height: 1; color: Theme.border }
                    }
                }
            }
            ProjectsText {
                objectName: "projectsContentsEmpty"
                visible: !root.rows.length; x: 12; y: 44; width: parent.width - 24
                wrapMode: Text.Wrap
                text: root.illustrative ? "No illustrative " + root.tab.toLowerCase() + " supplied for this study." : root.tab === "Artifacts" || root.tab === "Scans" || root.tab === "Meshes" ? "No matching descriptors in the available snapshot view. Clear filters to inspect other types." : root.tab + " unavailable · no public runtime contract. Artifacts retain their actual type; they are never presented as versions or CAD output."
                color: Theme.secondary
            }
        }
    }
}
