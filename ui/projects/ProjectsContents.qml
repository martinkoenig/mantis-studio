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
    readonly property var fractions: versions ? [.13, .54, .15, .18] : [.25, .43, .18, .14]
    function resetTable() {
        if (tableViewport) { tableViewport.cancelScroll(); tableViewport.contentY = 0 }
    }
    onTabChanged: resetTable()
    onIllustrativeChanged: resetTable()
    signal selectArtifact(string id)
    implicitHeight: 240
    Accessible.role: Accessible.Pane
    Accessible.name: "Project contents"
    ColumnLayout {
        id: body
        anchors.fill: parent; anchors.margins: 10; spacing: 8
        Flow {
            id: tabFlow
            objectName: "projectsTabStrip"
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
            Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 50
            Rectangle { width: parent.width; height: 28; color: Theme.chrome }
            Row {
                width: parent.width; height: 28
                ProjectsText { width: parent.width * root.fractions[0]; height: 28; verticalAlignment: Text.AlignVCenter; text: root.versions ? "Version" : "Artifact ID"; color: Theme.secondary }
                ProjectsText { width: parent.width * root.fractions[1]; height: 28; verticalAlignment: Text.AlignVCenter; text: root.versions ? "Date / description" : "Type"; color: Theme.secondary }
                ProjectsText { width: parent.width * root.fractions[2]; height: 28; verticalAlignment: Text.AlignVCenter; text: root.versions ? "Author" : "State"; color: Theme.secondary }
                ProjectsText { width: parent.width * root.fractions[3]; height: 28; verticalAlignment: Text.AlignVCenter; text: root.versions ? "Size" : "Chunks"; color: Theme.secondary }
            }
            ProjectsViewport {
                id: tableViewport
                objectName: "projectsTableViewport"
                y: 30; width: parent.width; height: Math.max(0, parent.height - 30)
                contentHeight: root.rows.length * 36
                activeFocusOnTab: true
                Accessible.name: "Project contents table"
                Repeater {
                    id: artifacts
                    model: 128
                    delegate: Button {
                        id: artifact
                        required property int index
                        readonly property var row: root.rows[index] || ({id:"",displayId:"",type:"",state:"",chunks:"",detail:""})
                        objectName: "projectsArtifactRow" + index
                        x: 0; y: index * 36; width: tableViewport.contentWidth; height: 36
                        visible: index < root.rows.length
                        padding: 0; hoverEnabled: true; activeFocusOnTab: true
                        Keys.onDownPressed: if (index + 1 < root.rows.length) artifacts.itemAt(index + 1).forceActiveFocus(Qt.TabFocusReason)
                        Keys.onUpPressed: if (index > 0) artifacts.itemAt(index - 1).forceActiveFocus(Qt.TabFocusReason)
                        onClicked: if (!root.illustrative) root.selectArtifact(row.id)
                        Accessible.role: root.illustrative ? Accessible.StaticText : Accessible.Button
                        Accessible.name: (row.displayId || row.id) + " · " + row.type + " · " + row.state + " · chunks " + row.chunks + (root.versions ? " · " + row.detail : "")
                        Accessible.description: root.illustrative ? "Illustrative metadata only" : "Select bounded read-only descriptor. Does not load artifact data."
                        Accessible.onPressAction: if (enabled && visible && !root.illustrative) clicked()
                        contentItem: Row {
                            ProjectsText { width: parent.width * root.fractions[0]; height: 36; verticalAlignment: Text.AlignVCenter; text: artifact.row.displayId || artifact.row.id; font.pixelSize: 12 }
                            ProjectsText { width: parent.width * root.fractions[1]; height: 36; verticalAlignment: Text.AlignVCenter; text: root.versions ? artifact.row.type + "  " + artifact.row.detail : artifact.row.type; font.pixelSize: 12 }
                            ProjectsText { width: parent.width * root.fractions[2]; height: 36; verticalAlignment: Text.AlignVCenter; text: artifact.row.state; font.pixelSize: 12; color: Theme.secondary }
                            ProjectsText { width: parent.width * root.fractions[3]; height: 36; verticalAlignment: Text.AlignVCenter; text: artifact.row.chunks; font.pixelSize: 12; color: Theme.secondary }
                        }
                        background: Rectangle {
                            color: artifact.visualFocus ? Theme.hover : (!root.illustrative && root.live.selectedId === artifact.row.id) || (root.versions && artifact.index === 0) ? Theme.selection : artifact.hovered ? Theme.hover : Theme.canvas
                            border.width: artifact.visualFocus ? 2 : 0; border.color: Theme.focus
                            Rectangle { y: parent.height - 1; width: parent.width; height: 1; color: Theme.border }
                        }
                    }
                }
            }
            ProjectsText {
                objectName: "projectsContentsEmpty"
                visible: !root.rows.length; x: 20; y: 30 + Math.max(10, (parent.height - 30 - implicitHeight) / 2); width: parent.width - 40
                wrapMode: Text.Wrap
                text: root.illustrative ? "No illustrative " + root.tab.toLowerCase() + " supplied for this study." : root.tab === "Artifacts" || root.tab === "Scans" || root.tab === "Meshes" ? "No matching descriptors in the available snapshot view. Clear filters to inspect other types." : root.tab + " unavailable · no public runtime contract. Artifacts retain their actual type; they are never presented as versions or CAD output."
                color: Theme.secondary
            }
        }
    }
}
