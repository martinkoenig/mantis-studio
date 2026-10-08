import QtQuick
import QtQuick.Layouts
import "../design"
import "../components"
ColumnLayout {
    id: root
    required property var snapshot
    required property var demoProjects
    readonly property alias projectGallery: projects
    property bool hasExamples: illustrative
    property bool stacked: width < 1100
    readonly property string staleLabel: illustrative || snapshot.confirmed ? "" : snapshot.hasSnapshot ? "Last known · current state unconfirmed" : "Runtime state unconfirmed"
    readonly property bool illustrative: snapshot.source === "mock"
    signal navigate(string route)
    signal revealExamples()
    spacing: 12
    HomeGuide { id: guide; onNavigate: function(route) { root.navigate(route) } }
    GridLayout {
        Layout.fillWidth: true
        columns: root.stacked ? 1 : 2
        columnSpacing: 16; rowSpacing: 16
        ColumnLayout {
            Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.alignment: Qt.AlignTop
            spacing: 12
            Panel {
                objectName: "homeHero"
                Layout.fillWidth: true; implicitHeight: 280
                clip: true
                Image {
                    anchors.fill: parent
                    source: visible ? "assets/hero.jpg" : ""; sourceSize.width: 1600; sourceSize.height: 480
                    fillMode: Image.PreserveAspectFit; horizontalAlignment: Image.AlignRight
                    Accessible.ignored: true
                }
                Rectangle {
                    anchors.fill: parent
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0; color: "#ef061917" }
                        GradientStop { position: 0.5; color: "#ad081918" }
                        GradientStop { position: 0.75; color: "#00081918" }
                    }
                }
                ColumnLayout {
                    anchors.left: parent.left; anchors.top: parent.top; anchors.right: parent.right
                    anchors.leftMargin: 26; anchors.rightMargin: 20; anchors.topMargin: 32
                    spacing: 16
                    HomeText { text: "MANTIS STUDIO"; color: Theme.secondary; font.pixelSize: 11; font.letterSpacing: 1.2; Layout.fillWidth: true }
                    HomeText { text: "Scan. Process. Inspect. Engineer."; font.pixelSize: root.stacked ? 28 : 29; font.bold: true; color: Theme.text; Layout.fillWidth: true; maximumLineCount: 2 }
                    HomeText { text: "Bring real-world data into your engineering workflow.\nFrom acquisition to traceable results."; font.pixelSize: 14; Layout.fillWidth: true; Layout.rightMargin: parent.width * 0.40; maximumLineCount: 3 }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 12
                        StudioButton { objectName: "homeGoScan"; text: "Go to Scan"; primary: true; implicitHeight: 44; implicitWidth: 138; onClicked: root.navigate("acquisition"); Accessible.description: "Open acquisition without starting capture" }
                        StudioButton { objectName: "homeProjects"; text: "Browse Projects"; implicitHeight: 44; onClicked: root.navigate("projects"); Accessible.description: "Open the planned project browser foundation" }
                        StudioButton { objectName: "homePlanned"; text: "Import · planned"; implicitHeight: 44; enabled: false; Accessible.description: "Project creation, opening and import are unavailable until UI-M2" }
                        Item { Layout.fillWidth: true }
                    }
                    HomeText { text: "Original illustrative artwork · project creation / import coming in UI-M2"; font.pixelSize: 10; color: Theme.muted; Layout.fillWidth: true }
                }
            }
            HomeText { objectName: "homeFreshness"; text: root.snapshot.freshness; font.pixelSize: 11; color: root.snapshot.confirmed ? Theme.secondary : Theme.warning; Layout.fillWidth: true; maximumLineCount: 2 }
            // Dynamic content remains behind stable Items, outside Qt 6.4 Layout caches.
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
                objectName: "homeCurrentProjectCard"
                visible: !root.illustrative; Layout.fillWidth: true; implicitHeight: Math.max(228, body.implicitHeight + 64)
                title: "Current Project"; freshness: root.staleLabel
                RowLayout {
                    id: body
                    Layout.fillWidth: true; spacing: 20
                    Rectangle {
                        Layout.preferredWidth: 100; Layout.preferredHeight: 116; radius: 8; color: Theme.raised
                        StudioIcon { name: "projects"; width: 52; height: 52; anchors.centerIn: parent; color: Theme.accent }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true; Layout.minimumWidth: 0; spacing: 10
                        HomeText { objectName: "homeProjectName"; text: root.snapshot.projectName; color: Theme.text; font.pixelSize: 21; font.bold: true; Layout.fillWidth: true; maximumLineCount: 1 }
                        HomeText { objectName: "homeProjectPath"; text: root.snapshot.project; Layout.fillWidth: true; maximumLineCount: 1; elide: Text.ElideMiddle }
                        HomeText { text: "Project history coming in UI-M2\nThe runtime supplies the current path, without last-opened dates or file sizes."; font.pixelSize: 12; Layout.fillWidth: true }
                        StudioButton { objectName: "homeCurrentProject"; text: "View current runtime"; onClicked: root.navigate("acquisition"); Accessible.description: "Inspect current project in existing acquisition; no project selection" }
                    }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true; spacing: 10
                HomeText { text: "Quick Actions"; font.pixelSize: 16; font.bold: true; color: Theme.text }
                Item {
                    Layout.fillWidth: true; implicitHeight: actions.implicitHeight
                    Grid {
                        id: actions; objectName: "homeQuickActions"; width: parent.width; columns: width < 760 ? 2 : 4; spacing: 12
                        Repeater {
                            // Keep controls and nested layouts alive across mode changes.
                            // Bind source-dependent labels in the delegate, not in this model.
                            model: [
                                {title: "Scanner & Devices", icon: "devices", detail: "Inspect sources and\nadvertised capabilities", button: "View devices", name: "homeDevices"},
                                {title: "Quick Scan", icon: "quickscan", detail: "Open acquisition.\nYou control when to capture.", button: "Go to Scan", name: "homeAcquisition"},
                                {title: "Learn Mantis Studio", icon: "learn", detail: "A short guide to sources,\nartifacts and traceability.", button: "Read workflow guide", name: "homeLearn"},
                                {title: "Example Projects", icon: "projects", detail: "", button: "", name: "homeExamples"}
                            ]
                            delegate: Panel {
                                required property var modelData
                                required property int index
                                objectName: "homeActionCard" + index
                                width: (actions.width - (actions.columns - 1) * actions.spacing) / actions.columns
                                implicitHeight: 150; clip: true
                                gradient: Gradient {
                                    GradientStop { position: 0; color: index === 1 ? "#173b32" : index === 2 ? "#1b3035" : "#17272b" }
                                    GradientStop { position: 1; color: Theme.panel }
                                }
                                Image { visible: index === 0 || index === 3; source: visible ? (index === 0 ? "assets/scanner.jpg" : "assets/housing.jpg") : ""; sourceSize.width: 240; sourceSize.height: 160; width: 90; height: 85; anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.bottomMargin: 40; opacity: 0.55; fillMode: Image.PreserveAspectFit; Accessible.ignored: true }
                                ColumnLayout {
                                    anchors.fill: parent; anchors.margins: 14; spacing: 8
                                    RowLayout {
                                        Layout.fillWidth: true; spacing: 9
                                        StudioIcon { name: modelData.icon; color: Theme.accent; Layout.preferredWidth: 22; Layout.preferredHeight: 22 }
                                        HomeText { text: modelData.title; font.pixelSize: 12; font.bold: true; color: Theme.text; Layout.fillWidth: true; maximumLineCount: 1 }
                                    }
                                    HomeText { text: index === 3 ? root.hasExamples ? "Explore four illustrative\nmechanical studies." : "Samples available in\nMock / Hybrid mode." : modelData.detail; font.pixelSize: 11; Layout.fillWidth: true; Layout.rightMargin: index === 0 || index === 3 ? 35 : 0; maximumLineCount: 3 }
                                    Item { Layout.fillHeight: true }
                                    StudioButton {
                                        objectName: modelData.name; Layout.fillWidth: true; implicitHeight: 30; text: index === 3 ? root.hasExamples ? "Browse examples" : "Samples unavailable" : modelData.button; primary: index === 1; enabled: index !== 3 || root.hasExamples
                                        onClicked: {
                                            if (index === 0) root.navigate("devices")
                                            else if (index === 1) root.navigate("acquisition")
                                            else if (index === 2) guide.open()
                                            else if (root.hasExamples) root.revealExamples()
                                        }
                                        Accessible.description: index === 3 ? root.hasExamples ? "Focus illustrative read-only projects, without importing data" : "Examples unavailable in live mode; use Mock or Hybrid" : index === 2 ? "Open local read-only workflow guidance" : "Open existing workspace; no automatic operation"
                                    }
                                }
                            }
                        }
                    }
                }
            }
            Item {
                Layout.fillWidth: true; implicitHeight: activity.implicitHeight
                HomeActivity { id: activity; objectName: "homeActivity"; width: parent.width; snapshot: root.snapshot; freshness: root.staleLabel }
            }
        }
        ColumnLayout {
            Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.preferredWidth: root.stacked ? -1 : 332; Layout.maximumWidth: root.stacked ? Infinity : 348
            Layout.alignment: Qt.AlignTop; spacing: 12
            HomeCard {
                Layout.fillWidth: true; title: "Scanner"; freshness: root.staleLabel
                RowLayout {
                    visible: root.illustrative; Layout.fillWidth: true; spacing: 12
                    Image { source: visible ? "assets/scanner.jpg" : ""; sourceSize.width: 480; sourceSize.height: 400; Layout.preferredWidth: 102; Layout.preferredHeight: 90; fillMode: Image.PreserveAspectFit; Accessible.ignored: true }
                    ColumnLayout {
                        Layout.fillWidth: true
                        HomeText { text: "Scanner study"; color: Theme.text; font.bold: true; Layout.fillWidth: true }
                        HomeText { text: "Illustrative · no hardware\nconnected"; color: Theme.warning; font.pixelSize: 11; Layout.fillWidth: true }
                    }
                }
                Item {
                    visible: !root.illustrative; Layout.fillWidth: true; implicitHeight: deviceRows.implicitHeight
                    HomeRows { id: deviceRows; width: parent.width; rows: root.illustrative ? [] : root.snapshot.devices; kind: "devices"; source: root.snapshot.source }
                }
                HomeText { visible: !root.illustrative && root.snapshot.devices.length === 0; text: root.snapshot.confirmed && root.snapshot.devicesAvailable && root.snapshot.devicesCount === 0 ? "No logical devices discovered." : root.snapshot.confirmed ? "No valid logical descriptors available." : "Device state unconfirmed. Check runtime and authentication."; Layout.fillWidth: true }
                HomeText { text: "Firmware  —     Serial  —\nCalibration / temperature unknown"; font.pixelSize: 11; Layout.fillWidth: true }
                HomeText { visible: !root.illustrative; text: root.snapshot.devicesSummary; font.pixelSize: 10; Layout.fillWidth: true }
                RowLayout {
                    Layout.fillWidth: true
                    StudioButton { objectName: "homeDeviceDetails"; text: "Devices"; Layout.fillWidth: true; implicitHeight: 30; onClicked: root.navigate("devices") }
                    StudioButton { objectName: "homeCalibration"; text: "Calibration"; Layout.fillWidth: true; implicitHeight: 30; onClicked: root.navigate("calibration"); Accessible.description: "Open guided calibration, without selecting a device or activating a revision" }
                }
            }
            HomeCard {
                Layout.fillWidth: true; title: "System Status"
                Item {
                    Layout.fillWidth: true; implicitHeight: gauges.implicitHeight
                    Column {
                        id: gauges; width: parent.width; spacing: 10
                        Repeater {
                            model: [{name: "GPU", value: 0.42, label: "42 %"}, {name: "CPU", value: 0.18, label: "18 %"}, {name: "Memory", value: 0.35, label: "5.6 / 16 GB"}, {name: "Storage", value: 0.45, label: "420 / 931 GB"}]
                            delegate: Row {
                                required property var modelData
                                width: gauges.width; spacing: 10; height: 15
                                HomeText { width: 58; text: modelData.name; font.pixelSize: 11; maximumLineCount: 1 }
                                Rectangle { width: Math.max(40, gauges.width - 158); height: 5; y: 5; radius: 3; color: Theme.border
                                    Rectangle { visible: root.illustrative; width: parent.width * modelData.value; height: 5; radius: 3; color: Theme.accent }
                                }
                                HomeText { width: 80; text: root.illustrative ? modelData.label : "—"; font.pixelSize: 10; horizontalAlignment: Text.AlignRight; maximumLineCount: 1 }
                            }
                        }
                    }
                }
                HomeText { objectName: "homeMetrics"; text: root.illustrative ? "Illustrative gauges · Demo / Mock" : "Metrics not available from this runtime"; font.pixelSize: 10; Layout.fillWidth: true; color: root.illustrative ? Theme.warning : Theme.muted }
            }
            HomeCard {
                Layout.fillWidth: true; title: "Running Jobs"; freshness: root.staleLabel
                Item {
                    Layout.fillWidth: true; implicitHeight: jobRows.implicitHeight
                    HomeRows { id: jobRows; width: parent.width; rows: root.snapshot.jobs; kind: "jobs"; source: root.snapshot.source }
                }
                HomeText { visible: root.snapshot.jobs.length === 0; text: root.snapshot.confirmed && root.snapshot.jobsAvailable && root.snapshot.jobsCount === 0 ? "No jobs in the current snapshot." : root.snapshot.confirmed ? "Job data unavailable." : "Current job state unknown."; Layout.fillWidth: true }
                HomeText { visible: !root.illustrative; text: root.snapshot.jobsSummary; font.pixelSize: 10; Layout.fillWidth: true }
                StudioButton { objectName: "homeJobs"; text: "View jobs"; implicitHeight: 28; Layout.fillWidth: true; enabled: !root.illustrative; onClicked: if (!root.illustrative) root.navigate("acquisition"); Accessible.description: root.illustrative ? "Demo jobs are illustrative; details are unavailable" : "Open existing acquisition jobs; no cancellation or selection" }
                HomeText { objectName: "homeJobsUnavailable"; visible: root.illustrative; text: "Illustrative jobs · details unavailable"; font.pixelSize: 10; Layout.fillWidth: true }
            }
            HomeCard {
                Layout.fillWidth: true; title: "Tips & Updates"
                RowLayout {
                    Layout.fillWidth: true; spacing: 12
                    Image { source: visible ? "assets/housing.jpg" : ""; sourceSize.width: 240; sourceSize.height: 140; Layout.preferredWidth: 100; Layout.preferredHeight: 72; fillMode: Image.PreserveAspectCrop; Accessible.ignored: true }
                    HomeText { text: "Getting started\nwith Mantis Studio"; color: Theme.text; font.bold: true; Layout.fillWidth: true }
                }
                HomeText { text: "Discover sources  ·  Acquire or replay\nReview immutable artifacts and diagnostics"; font.pixelSize: 11; Layout.fillWidth: true }
                StudioButton { objectName: "homeTips"; text: "Read workflow guide"; implicitHeight: 28; Layout.fillWidth: true; onClicked: guide.open(); Accessible.description: "Open static local guidance; no video or release feed" }
            }
        }
    }
}
