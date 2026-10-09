import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import Mantis.Studio 1.0
import "../design"
import "../components"
import "../projects"
FocusScope {
    id: root
    required property var bridge
    property string mode: "live"
    readonly property alias liveModel: liveModel
    readonly property bool multiPane: width >= 1120
    readonly property bool showNavigator: multiPane || filtersOpen
    readonly property bool showInspector: multiPane || detailsOpen
    property bool filtersOpen: false
    property bool detailsOpen: false
    property bool listMode: false
    property string activeSource: mode === "mock" ? "mock" : "live"
    property string sampleKey: "housing"
    property string mockQuery: ""
    property string mockFacet: "All"
    property string mockSort: "Study order"
    readonly property bool illustrative: activeSource === "mock"
    readonly property var selectedSample: {
        for (let i = 0; i < demo.projects.length; ++i) if (demo.projects[i].key === sampleKey) return demo.projects[i]
        return {name:"No sample selected",image:"",description:"Clear filters to select a packaged study.",type:"Unavailable",tag:"Unavailable",date:"Unavailable"}
    }
    readonly property var sampleRows: {
        const q = mockQuery.toLowerCase()
        return demo.projects.filter(function(p, i) {
            return (!q || (p.name + " " + p.tag + " " + p.type).toLowerCase().indexOf(q) >= 0)
                && (mockFacet === "All" || mockFacet === "Recent" && i < 6 || mockFacet === "Favorites" && p.favorite || p.tag === mockFacet)
        }).slice().sort(function(a,b) { if (mockSort === "Study order") return demo.projects.indexOf(a) - demo.projects.indexOf(b); const l = mockSort === "Name" ? a.name : a.type; const r = mockSort === "Name" ? b.name : b.type; return l === r ? a.key.localeCompare(b.key) : l.localeCompare(r) })
    }
    readonly property string operationBlocker: "New/Open blocked: the runtime retains old-project replay queues and preview leases after switching. A completed replay from A can write its preview into B. Runtime isolation must be fixed first."
    onSampleRowsChanged: {
        if (!sampleRows.some(function(p) { return p.key === sampleKey })) sampleKey = sampleRows.length ? sampleRows[0].key : ""
        Qt.callLater(root.restoreSearchFocus)
    }
    function restoreSearchFocus() {
        const focus = root.Window.window ? root.Window.window.activeFocusItem : null
        if (root.visible && focus && !focus.visible) search.forceActiveFocus(Qt.TabFocusReason)
    }
    Keys.onEscapePressed: {
        if (!multiPane && detailsOpen) { detailsOpen = false; detailsToggle.forceActiveFocus(Qt.TabFocusReason) }
        else if (!multiPane && filtersOpen) { filtersOpen = false; filtersToggle.forceActiveFocus(Qt.TabFocusReason) }
    }
    function revealFocused() {
        if (!root.visible || !root.Window.window) return
        const focus = root.Window.window.activeFocusItem
        let p = focus
        while (p && p !== content) p = p.parent
        if (p === content) reveal(focus)
    }
    Connections {
        target: root.Window.window
        function onActiveFocusItemChanged() { Qt.callLater(root.revealFocused) }
    }
    Connections {
        target: scroll.contentItem
        function onContentHeightChanged() { Qt.callLater(root.revealFocused) }
    }
    Connections {
        target: content
        function onHeightChanged() { Qt.callLater(root.revealFocused) }
    }
    Connections {
        target: gallery
        function onHeightChanged() { Qt.callLater(root.revealFocused) }
        function onYChanged() { Qt.callLater(root.revealFocused) }
    }
    signal navigate(string route)
    function reveal(item) {
        if (!item || !scroll.contentItem) return
        const y = item.mapToItem(content, 0, 0).y
        const f = scroll.contentItem
        if (item.height > f.height || y < f.contentY) f.contentY = Math.max(0, Math.min(y, f.contentHeight - f.height))
        else if (y + item.height > f.contentY + f.height) f.contentY = Math.min(Math.max(0, f.contentHeight - f.height), y + item.height - f.height)
    }
    function chooseSample(key) { sampleKey = key; activeSource = "mock" }
    function reset() {
        if (illustrative) { mockQuery = ""; mockFacet = "All"; mockSort = "Study order" }
        else liveModel.clearFilters()
    }
    onModeChanged: { activeSource = mode === "mock" ? "mock" : "live"; contents.tab = mode === "mock" ? "Versions" : "Artifacts" }
    ProjectsModel { id: liveModel; bridge: root.mode === "mock" ? null : root.bridge }
    ProjectsDemo { id: demo; objectName: "projectsDemoFixture" }
    ColumnLayout {
        anchors.fill: parent; spacing: 10
        GridLayout {
            Layout.fillWidth: true; columns: root.width >= 1160 ? 2 : 1; columnSpacing: 12; rowSpacing: 8
            RowLayout {
                Layout.fillWidth: true; spacing: 8
                ColumnLayout {
                    Layout.fillWidth: true; spacing: 4
                    ProjectsText { text: "Projects"; font.bold: true; font.pixelSize: 23 }
                    ProjectsText { text: "Manage scans, models and related data"; color: Theme.secondary; font.pixelSize: 11; Layout.fillWidth: true }
                }
                UnavailableAction { objectName: "projectsNew"; text: "+ New Project"; primary: true; reason: root.operationBlocker }
                UnavailableAction { objectName: "projectsImport"; text: "Import"; reason: "No public project import contract." }
                UnavailableAction { objectName: "projectsOpen"; text: "Open"; reason: root.operationBlocker }
                UnavailableAction { objectName: "projectsRecent"; text: "Recent"; reason: "No public project history/catalog contract." }
            }
            RowLayout {
                Layout.fillWidth: true; spacing: 6
                TextField {
                    id: search
                    objectName: "projectsSearch"
                    Layout.fillWidth: true; Layout.minimumWidth: 140; Layout.maximumWidth: 380
                    implicitHeight: 36
                    placeholderText: root.illustrative ? "Search sample names / tags…" : "Search available artifact IDs / types…"
                    maximumLength: 256
                    color: Theme.text; placeholderTextColor: Theme.muted
                    background: Rectangle { color: Theme.panel; radius: Theme.radius; border.color: search.activeFocus ? Theme.focus : Theme.border; border.width: search.activeFocus ? 2 : 1 }
                    text: root.illustrative ? root.mockQuery : liveModel.query
                    onTextEdited: if (root.illustrative) root.mockQuery = text; else liveModel.query = text
                    Accessible.name: root.illustrative ? "Search illustrative samples" : "Search bounded current-project artifact metadata"
                    Accessible.description: "Search is local, source-specific and limited to displayed text in the inspected sample. No catalog or data request."
                }
                StudioButton { objectName: "projectsGrid"; text: "▦"; implicitWidth: 38; horizontalPadding: 6; primary: !root.listMode; onClicked: root.listMode = false; Accessible.name: "Grid view" }
                StudioButton { objectName: "projectsList"; text: "≡"; implicitWidth: 38; horizontalPadding: 6; primary: root.listMode; onClicked: root.listMode = true; Accessible.name: "List view" }
                StudioButton { id: filtersToggle; objectName: "projectsFiltersToggle"; visible: !root.multiPane; text: "Filters"; onClicked: { root.filtersOpen = !root.filtersOpen; if (root.filtersOpen) root.reveal(navigator) }
                    Accessible.description: "Show or hide stacked project filters" }
                StudioButton { id: detailsToggle; objectName: "projectsDetailsToggle"; visible: !root.multiPane; text: "Details"; onClicked: { root.detailsOpen = !root.detailsOpen; if (root.detailsOpen) { root.reveal(inspector); inspector.forceActiveFocus(Qt.TabFocusReason) } }
                    Accessible.description: "Show or hide selected project inspector in the vertical scroll" }
            }
        }
        ScrollView {
            id: scroll
            objectName: "projectsScroll"
            Layout.fillWidth: true; Layout.fillHeight: true
            contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ColumnLayout {
                id: content
                width: scroll.availableWidth; spacing: 10
                ProjectsText {
                    objectName: "projectsFreshness"
                    Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: 10; color: Theme.warning
                    Accessible.description: root.operationBlocker
                    text: (root.mode === "mock" ? "Illustrative sample library · offline · no runtime project IDs" : liveModel.data.freshness + " · one shared runtime project; catalog unavailable") + " · New/Open disabled: switching retains old replay/preview data. Import/Recent: API unavailable."
                }
                ProjectsText {
                    objectName: "projectsErrors"
                    visible: root.mode !== "mock" && liveModel.data.issues.length > 0
                    Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.error; font.pixelSize: 11
                    text: liveModel.data.issues.map(function(e) { return e.phase + " [" + e.component + " / " + e.code + "]: " + e.message }).join("\n")
                }
                GridLayout {
                    Layout.fillWidth: true; columns: root.multiPane ? 2 : 1; columnSpacing: 12; rowSpacing: 12
                    GridLayout {
                        Layout.fillWidth: true; Layout.alignment: Qt.AlignTop
                        columns: root.multiPane ? 2 : 1; columnSpacing: 12; rowSpacing: 10
                        ProjectsNavigator {
                            id: navigator; objectName: "projectsNavigator"
                            visible: root.showNavigator
                            Layout.row: 0; Layout.column: 0; Layout.rowSpan: root.multiPane ? 3 : 1
                            Layout.minimumWidth: root.multiPane ? 188 : 0; Layout.preferredWidth: 188; Layout.maximumWidth: root.multiPane ? 188 : Infinity; Layout.fillWidth: !root.multiPane; Layout.alignment: Qt.AlignTop
                            illustrative: root.illustrative; facet: root.illustrative ? root.mockFacet : "All"; count: demo.projects.length
                            onChoose: function(value) { if (root.illustrative) root.mockFacet = value; else liveModel.clearFilters() }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true; Layout.alignment: Qt.AlignTop; Layout.maximumHeight: implicitHeight; spacing: 10
                            Layout.row: root.multiPane ? 0 : root.showNavigator ? 1 : 0; Layout.column: root.multiPane ? 1 : 0
                            objectName: "projectsFiltersHeader"
                            Flow {
                                Layout.fillWidth: true; spacing: 6
                                ProjectsComboBox {
                                    id: sourcePicker; objectName: "projectsSource"
                                    visible: root.mode === "hybrid"; width: 160; height: 32
                                    model: ["Runtime metadata", "Demo samples"]; currentIndex: root.illustrative ? 1 : 0
                                    onActivated: root.activeSource = currentIndex ? "mock" : "live"
                                    Accessible.name: "Filter and inspector source"
                                }
                                ProjectsComboBox {
                                    objectName: "projectsSort"; width: 125; height: 32
                                    model: root.illustrative ? ["Study order", "Name", "Study type"] : ["ID", "Type", "State"]
                                    currentIndex: root.illustrative ? Math.max(0, model.indexOf(root.mockSort)) : Math.max(0, model.indexOf(liveModel.sort))
                                    onActivated: if (root.illustrative) root.mockSort = currentText; else liveModel.sort = currentText
                                    Accessible.name: "Sort available metadata"
                                }
                                ProjectsComboBox {
                                    objectName: "projectsTypeFilter"; visible: !root.illustrative; width: 145; height: 32
                                    model: ["All types"].concat(Array.prototype.slice.call(liveModel.data.types))
                                    currentIndex: Math.max(0, model.indexOf(liveModel.typeFilter))
                                    onActivated: liveModel.typeFilter = currentIndex ? currentText : ""
                                    Accessible.name: "Artifact type filter"
                                }
                                ProjectsComboBox {
                                    objectName: "projectsStateFilter"; visible: !root.illustrative; width: 116; height: 32
                                    model: ["All states"].concat(Array.prototype.slice.call(liveModel.data.states))
                                    currentIndex: Math.max(0, model.indexOf(liveModel.stateFilter))
                                    onActivated: liveModel.stateFilter = currentIndex ? currentText : ""
                                    Accessible.name: "Artifact state filter"
                                }
                                ProjectsText { visible: root.mode === "mock"; text: root.sampleRows.length + " of 12 samples"; height: 32; verticalAlignment: Text.AlignVCenter; color: Theme.secondary; font.pixelSize: 10 }
                                StudioButton { objectName: "projectsClear"; text: "Clear filters"; implicitHeight: 32; onClicked: root.reset() }
                            }
                            Button {
                                id: currentProject; objectName: "projectsCurrentProject"
                                Layout.fillWidth: true; Layout.maximumWidth: 420; Layout.alignment: Qt.AlignLeft; implicitHeight: 128; visible: root.mode !== "mock"
                                padding: 14; hoverEnabled: true; activeFocusOnTab: true
                                enabled: liveModel.data.available === true
                                onClicked: { root.activeSource = "live"; liveModel.selectArtifact("") }
                                Accessible.name: liveModel.data.name + " · current runtime project"
                                Accessible.description: "Select read-only project details. This does not open or mutate a project."
                                Accessible.onPressAction: if (enabled && visible) clicked()
                                background: Rectangle { color: Theme.panel; radius: Theme.radius; border.color: currentProject.visualFocus ? Theme.focus : root.activeSource === "live" ? Theme.accent : Theme.border; border.width: 1 }
                                contentItem: ColumnLayout {
                                    spacing: 6
                                    ProjectsText { text: liveModel.data.name; font.pixelSize: 17; font.bold: true; Layout.fillWidth: true }
                                    ProjectsText { text: liveModel.data.path; wrapMode: Text.WrapAnywhere; maximumLineCount: 2; Layout.fillWidth: true; color: Theme.secondary; font.pixelSize: 11 }
                                    ProjectsText { text: "Runtime host · last-known values are marked stale · no project catalog"; wrapMode: Text.Wrap; Layout.fillWidth: true; font.pixelSize: 10; color: Theme.muted }
                                }
                            }
                            ProjectsText { visible: root.mode !== "mock"; text: liveModel.data.summary; wrapMode: Text.Wrap; Layout.fillWidth: true; color: Theme.secondary; font.pixelSize: 11 }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true; Layout.alignment: Qt.AlignTop; spacing: 10
                            visible: root.mode !== "live"
                            Layout.row: root.multiPane ? (root.mode === "mock" ? 1 : 2) : (root.showNavigator ? 1 : 0) + (root.mode === "mock" ? 1 : 2)
                            Layout.column: root.multiPane ? 1 : 0
                            ProjectsText {
                                objectName: "projectsDemoSeparation"; visible: root.mode === "hybrid"
                                text: root.mode === "hybrid" ? "SEPARATE DEMO LIBRARY · select a sample to inspect; never part of runtime results" : "SAMPLE PROJECTS · " + root.sampleRows.length + " of 12 illustrative studies"
                                wrapMode: Text.Wrap; Layout.fillWidth: true; color: Theme.warning; font.pixelSize: 10
                            }
                            ProjectsGallery {
                                id: gallery; objectName: "projectsGallery"
                                Layout.fillWidth: true; visible: root.mode !== "live"
                                rows: root.sampleRows; selectedKey: root.sampleKey; listMode: root.listMode
                                onSelect: function(key) { root.chooseSample(key) }
                                onRevealFocusedCard: function(item) { root.reveal(item) }
                            }
                            ProjectsText { visible: root.mode !== "live" && !root.sampleRows.length; text: "No sample matches these local filters. Clear filters to restore the library."; wrapMode: Text.Wrap; Layout.fillWidth: true; color: Theme.secondary }
                        }
                        Item {
                            visible: root.multiPane && root.mode === "mock"
                            Layout.row: 2; Layout.column: 1
                            Layout.fillWidth: true; Layout.fillHeight: true
                            Layout.minimumHeight: 0; Layout.preferredHeight: 0
                        }
                        ProjectsContents {
                            id: contents; objectName: "projectsContents"
                            Layout.fillWidth: true; Layout.alignment: Qt.AlignTop
                            Layout.row: root.multiPane ? (root.mode === "mock" ? 3 : 1) : (root.showNavigator ? 1 : 0) + (root.mode === "mock" ? 2 : 1)
                            Layout.column: root.multiPane && root.mode !== "mock" ? 1 : 0
                            Layout.columnSpan: root.multiPane && root.mode === "mock" ? 2 : 1
                            illustrative: root.illustrative; sample: root.selectedSample; live: liveModel.data
                            onSelectArtifact: function(id) { root.activeSource = "live"; liveModel.selectArtifact(id) }
                        }
                    }
                    ProjectsInspector {
                        id: inspector; objectName: "projectsInspector"
                        visible: root.showInspector
                        Layout.minimumWidth: root.multiPane ? 316 : 0; Layout.preferredWidth: 316; Layout.maximumWidth: root.multiPane ? 316 : Infinity; Layout.fillWidth: !root.multiPane; Layout.alignment: Qt.AlignTop
                        illustrative: root.illustrative; sample: root.selectedSample; live: liveModel.data
                        onInspectRuntime: root.navigate("acquisition")
                    }
                }
            }
        }
    }
}
