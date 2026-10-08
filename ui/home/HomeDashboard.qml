import QtQuick
import QtQuick.Layouts
import "../design"
import "../components"
ColumnLayout {
    id: root
    required property var snapshot
    required property var demoProjects
    property bool stacked: width < 1100
    readonly property string staleLabel: illustrative || snapshot.confirmed ? "" : snapshot.hasSnapshot ? "Last known · current state unconfirmed" : "Runtime state unconfirmed"
    property bool illustrative: snapshot.source === "mock"
    signal navigate(string route)
    spacing: Theme.gap
    GridLayout {
        Layout.fillWidth: true
        columns: root.stacked ? 1 : 2
        columnSpacing: Theme.gap; rowSpacing: Theme.gap
        ColumnLayout {
            Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.alignment: Qt.AlignTop
            spacing: 14
            Panel {
                Layout.fillWidth: true
                implicitHeight: heroBody.implicitHeight + 48
                clip: true
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0; color: "#0d302a" }
                    GradientStop { position: 0.55; color: "#102322" }
                    GradientStop { position: 1; color: "#183031" }
                }
                GeometryArt { width: parent.width * 0.4; height: parent.height; anchors.right: parent.right; hero: true; opacity: 0.8 }
                ColumnLayout {
                    id: heroBody
                    anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                    anchors.margins: 24; spacing: 10
                    HomeText { text: "MANTIS STUDIO  /  YOUR WORKSPACE"; color: Theme.accent; font.pixelSize: 10; font.letterSpacing: 1.4; Layout.fillWidth: true }
                    HomeText { text: "Scan. Process. Inspect. Engineer."; font.pixelSize: root.width < 1000 ? 25 : 29; font.bold: true; color: Theme.text; Layout.fillWidth: true; maximumLineCount: 2 }
                    HomeText { text: "Bring real-world data into your engineering workflow.\nBegin with acquisition. Keep every result traceable."; Layout.fillWidth: true; Layout.rightMargin: root.stacked ? 120 : 140; maximumLineCount: 3 }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 10
                        StudioButton { objectName: "homeGoScan"; text: "Go to Scan"; primary: true; onClicked: root.navigate("acquisition"); Accessible.description: "Open acquisition without starting capture" }
                        StudioButton { objectName: "homeProjects"; text: "Projects · UI-M2"; onClicked: root.navigate("projects"); Accessible.description: "Open the planned project browser foundation" }
                        Item { Layout.fillWidth: true }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                HomeText { objectName: "homeFreshness"; text: root.snapshot.freshness; font.pixelSize: 11; color: root.snapshot.confirmed ? Theme.accent : Theme.warning; Layout.fillWidth: true; maximumLineCount: 2 }
                SourceBadge { source: root.snapshot.source }
            }
            // Stable Item isolates every removable delegate from Qt 6.4 nested Layout caches.
            Item {
                Layout.fillWidth: true; implicitHeight: errors.implicitHeight
                visible: root.snapshot.issues.length > 0
                Column {
                    id: errors; width: parent.width; spacing: 6
                    Repeater {
                        model: root.snapshot.issues
                        delegate: HomeText {
                            required property var modelData
                            width: errors.width; color: Theme.error; font.pixelSize: 11
                            text: modelData.phase + " failed · " + modelData.component + " / " + (modelData.code === undefined || modelData.code === null ? "untyped" : modelData.code) + " · " + modelData.message
                            maximumLineCount: 3
                        }
                    }
                }
            }
            Item {
                visible: root.illustrative
                Layout.fillWidth: true; implicitHeight: projects.implicitHeight
                HomeProjects { id: projects; width: parent.width; projects: root.demoProjects }
            }
            HomeCard {
                visible: !root.illustrative; Layout.fillWidth: true
                title: "Current project"; source: root.snapshot.source; freshness: root.staleLabel
                RowLayout {
                    Layout.fillWidth: true
                    StudioIcon { name: "projects"; color: Theme.accent; Layout.preferredWidth: 36; Layout.preferredHeight: 36 }
                    ColumnLayout {
                        Layout.fillWidth: true; Layout.minimumWidth: 0; spacing: 4
                        HomeText { objectName: "homeProjectName"; text: root.snapshot.projectName; color: Theme.text; font.pixelSize: 17; font.bold: true; Layout.fillWidth: true; maximumLineCount: 1 }
                        HomeText { objectName: "homeProjectPath"; text: root.snapshot.project; Layout.fillWidth: true; maximumLineCount: 1; elide: Text.ElideMiddle }
                    }
                    StudioButton { objectName: "homeCurrentProject"; text: "View runtime"; onClicked: root.navigate("acquisition"); Accessible.description: "Inspect current project in existing acquisition; no project selection" }
                }
                HomeText { text: "Project history, file sizes and last-opened dates are not supplied by this runtime."; font.pixelSize: 11; Layout.fillWidth: true }
            }
            ColumnLayout {
                Layout.fillWidth: true; spacing: 10
                HomeText { text: "Quick Actions"; font.bold: true; color: Theme.text }
                Item {
                    Layout.fillWidth: true; implicitHeight: actions.implicitHeight
                    Grid {
                        id: actions; width: parent.width; columns: width < 760 ? 2 : 4; spacing: 10
                        Repeater {
                            model: [
                                {title: "Acquisition", icon: "scan", detail: "Capture, replay and artifacts", button: "Open acquisition", route: "acquisition", name: "homeAcquisition"},
                                {title: "Devices", icon: "devices", detail: "Capabilities and calibration", button: "View devices", route: "devices", name: "homeDevices"},
                                {title: "Calibration", icon: "inspect", detail: "Existing guided workflow", button: "Open calibration", route: "calibration", name: "homeCalibration"},
                                {title: "Project tools", icon: "projects", detail: "New / Open / Import · UI-M2", button: "Planned · UI-M2", route: "", name: "homePlanned"}
                            ]
                            delegate: Panel {
                                required property var modelData
                                width: (actions.width - (actions.columns - 1) * actions.spacing) / actions.columns
                                implicitHeight: actionBody.implicitHeight + 24
                                ColumnLayout {
                                    id: actionBody
                                    anchors.fill: parent; anchors.margins: 12; spacing: 7
                                    RowLayout {
                                        Layout.fillWidth: true
                                        StudioIcon { name: modelData.icon; color: Theme.accent }
                                        HomeText { text: modelData.title; font.bold: true; color: Theme.text; Layout.fillWidth: true; maximumLineCount: 1 }
                                    }
                                    HomeText { text: modelData.detail; font.pixelSize: 11; Layout.fillWidth: true; maximumLineCount: 2; Layout.preferredHeight: 24 }
                                    StudioButton { objectName: modelData.name; Layout.fillWidth: true; text: modelData.button; enabled: modelData.route.length > 0; onClicked: root.navigate(modelData.route); Accessible.description: modelData.route.length ? "Open existing workspace; no automatic operation" : "Project creation, opening and import are unavailable until UI-M2" }
                                }
                            }
                        }
                    }
                }
            }
            HomeCard {
                Layout.fillWidth: true
                title: root.illustrative ? "Example artifacts" : "Current project artifacts"; source: root.snapshot.source; freshness: root.staleLabel
                Item {
                    Layout.fillWidth: true; implicitHeight: artifactRows.implicitHeight
                    HomeRows { id: artifactRows; width: parent.width; rows: root.snapshot.artifacts; kind: "artifacts"; source: root.snapshot.source }
                }
                HomeText { visible: root.snapshot.artifacts.length === 0; text: root.snapshot.confirmed && root.snapshot.artifactsAvailable && root.snapshot.artifactsCount === 0 ? "No artifacts in the current snapshot." : root.snapshot.confirmed ? "Artifact data unavailable in this snapshot." : "Artifacts unavailable until a runtime snapshot is confirmed."; Layout.fillWidth: true }
                RowLayout {
                    Layout.fillWidth: true
                    HomeText { text: root.snapshot.artifactsSummary; visible: !root.illustrative; font.pixelSize: 10; Layout.fillWidth: true }
                    StudioButton { objectName: "homeArtifacts"; text: "View artifacts"; onClicked: root.navigate("acquisition"); Accessible.description: "Open existing acquisition artifacts; no artifact automatically loaded" }
                }
            }
            HomeCard {
                Layout.fillWidth: true
                title: root.illustrative ? "Example activity" : "Runtime events"; source: root.snapshot.source; freshness: root.staleLabel
                Item {
                    Layout.fillWidth: true; implicitHeight: eventRows.implicitHeight
                    HomeRows { id: eventRows; width: parent.width; rows: root.snapshot.events; kind: "events"; source: root.snapshot.source }
                }
                HomeText { visible: root.snapshot.events.length === 0; text: root.snapshot.confirmed && root.snapshot.eventsAvailable && root.snapshot.eventsCount === 0 ? "No events in the current snapshot." : root.snapshot.confirmed ? "Event data unavailable in this snapshot." : "Runtime events unavailable. No activity is inferred."; Layout.fillWidth: true }
                HomeText { text: root.illustrative ? "Illustrative activity · no runtime operations performed" : "Sequence order · no event dates supplied · " + root.snapshot.eventsSummary; font.pixelSize: 10; Layout.fillWidth: true }
            }
        }
        ColumnLayout {
            Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.preferredWidth: root.stacked ? -1 : 320; Layout.maximumWidth: root.stacked ? Infinity : 340
            Layout.alignment: Qt.AlignTop; spacing: 14
            HomeCard {
                Layout.fillWidth: true; title: "Devices"; source: root.snapshot.source; freshness: root.staleLabel
                Item {
                    Layout.fillWidth: true; implicitHeight: deviceRows.implicitHeight
                    HomeRows { id: deviceRows; width: parent.width; rows: root.snapshot.devices; kind: "devices"; source: root.snapshot.source }
                }
                HomeText { visible: root.snapshot.devices.length === 0; text: root.snapshot.confirmed && root.snapshot.devicesAvailable && root.snapshot.devicesCount === 0 ? "No logical devices discovered in this snapshot." : root.snapshot.confirmed ? "No valid logical device descriptors available in this snapshot." : "Device state unconfirmed. Check mantisd, endpoint and authentication."; Layout.fillWidth: true }
                HomeText { text: "Firmware, serial, temperature and calibration validity are not available here."; font.pixelSize: 11; Layout.fillWidth: true }
                HomeText { text: root.snapshot.devicesSummary; visible: !root.illustrative; font.pixelSize: 10; Layout.fillWidth: true }
                StudioButton { objectName: "homeDeviceDetails"; text: "Devices & calibration"; Layout.fillWidth: true; onClicked: root.navigate("devices") }
            }
            HomeCard {
                Layout.fillWidth: true; title: "System Status"; source: root.snapshot.source
                HomeText { objectName: "homeMetrics"; text: "Metrics not available from this runtime"; Layout.fillWidth: true }
                HomeText { text: "CPU  —     GPU  —\nMemory  —     Storage  —"; font.pixelSize: 11; color: Theme.muted; Layout.fillWidth: true }
            }
            HomeCard {
                Layout.fillWidth: true; title: root.illustrative ? "Example jobs" : "Runtime jobs"; source: root.snapshot.source; freshness: root.staleLabel
                Item {
                    Layout.fillWidth: true; implicitHeight: jobRows.implicitHeight
                    HomeRows { id: jobRows; width: parent.width; rows: root.snapshot.jobs; kind: "jobs"; source: root.snapshot.source }
                }
                HomeText { visible: root.snapshot.jobs.length === 0; text: root.snapshot.confirmed && root.snapshot.jobsAvailable && root.snapshot.jobsCount === 0 ? "No jobs in the current snapshot." : root.snapshot.confirmed ? "Job data unavailable in this snapshot." : "Current job state unknown."; Layout.fillWidth: true }
                HomeText { visible: !root.illustrative; text: root.snapshot.jobsSummary + " · no ETA supplied"; font.pixelSize: 10; Layout.fillWidth: true }
                StudioButton { objectName: "homeJobs"; text: "View jobs"; Layout.fillWidth: true; onClicked: root.navigate("acquisition"); Accessible.description: "Open existing acquisition jobs; no cancellation or job selection" }
            }
            HomeCard {
                Layout.fillWidth: true; title: "Workflow guide"; source: root.snapshot.source
                HomeText { text: "01  Inspect device capabilities\n02  Acquire or replay source data\n03  Review jobs and immutable artifacts"; font.pixelSize: 12; Layout.fillWidth: true }
                HomeText { text: "Capture and calibration remain owned by mantisd. Tutorial browser and release updates are planned."; font.pixelSize: 11; Layout.fillWidth: true }
            }
        }
    }
}
