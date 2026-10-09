import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../design"
import "../components"
import "../state"
import "../workspaces"

ApplicationWindow {
    id: window
    property string workspace: "acquisition"
    property string uiMode: "live"
    property var studioBridge: studio
    readonly property alias appUiState: state
    readonly property string activeRoute: workspace === "acquisition" ? "scan" : workspace === "calibration" ? "devices" : workspace
    readonly property var currentPage: state.routeInfo(workspace)
    readonly property bool legacyVisible: workspace === "acquisition" || (workspace === "scan" && uiMode !== "mock")
    readonly property bool compact: width < 1250
    visible: true
    width: 1536; height: 1024
    minimumWidth: 1080; minimumHeight: 720
    title: "Mantis Studio · " + currentPage.title
    color: Theme.canvas
    font.family: Theme.fontFamily
    font.pixelSize: Theme.bodySize
    palette.window: Theme.canvas
    palette.windowText: Theme.text
    palette.base: Theme.panel
    palette.text: Theme.text
    palette.button: Theme.raised
    palette.buttonText: Theme.text
    palette.highlight: Theme.accent
    palette.highlightedText: Theme.canvas
    palette.disabled.text: Theme.muted
    palette.disabled.buttonText: Theme.muted

    AppUiState { id: state; bridge: window.studioBridge; calibrationProvider: calibration; mode: window.uiMode }
    function updateCalibrationVisibility() {
        calibration.visible = uiMode !== "mock" && (workspace === "devices" || workspace === "calibration")
    }
    function openCalibration(deviceId) {
        if (uiMode !== "mock") calibration.selectDevice(deviceId)
        calibration.stage = 0
        workspace = "calibration"
    }
    onWorkspaceChanged: updateCalibrationVisibility()
    onUiModeChanged: updateCalibrationVisibility()
    Component.onCompleted: updateCalibrationVisibility()

    RowLayout {
        anchors.fill: parent; spacing: 0
        Rectangle {
            Layout.preferredWidth: window.compact ? 152 : 176
            Layout.fillHeight: true
            color: Theme.chrome
            Rectangle { width: 1; anchors.right: parent.right; height: parent.height; color: Theme.border }
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 8; spacing: 4
                RowLayout {
                    Layout.preferredHeight: 48; Layout.leftMargin: 10; spacing: 10
                    StudioIcon { name: "mantis"; color: Theme.accent; Layout.preferredWidth: 26; Layout.preferredHeight: 26 }
                    Label { text: "MANTIS"; font.pixelSize: 18; font.letterSpacing: 1.5; font.bold: true }
                }
                Repeater {
                    id: navigation
                    model: state.routes
                    delegate: ColumnLayout {
                        required property var modelData
                        required property int index
                        property alias navigationButton: navButton
                        Layout.fillWidth: true; spacing: 8
                        Rectangle { visible: index === 6; Layout.fillWidth: true; Layout.margins: 12; implicitHeight: 1; color: Theme.border }
                        NavigationItem {
                            id: navButton
                            objectName: "nav_" + modelData.route
                            Layout.fillWidth: true
                            route: modelData.route; text: modelData.title; selected: window.activeRoute === route
                            onClicked: window.workspace = route
                            Keys.onDownPressed: navigation.itemAt((index + 1) % navigation.count).navigationButton.forceActiveFocus()
                            Keys.onUpPressed: navigation.itemAt((index + navigation.count - 1) % navigation.count).navigationButton.forceActiveFocus()
                        }
                    }
                }
                Item { Layout.fillHeight: true }
                Label { text: "MANTIS STUDIO"; font.pixelSize: 9; color: Theme.muted; Layout.leftMargin: 10 }
                Label { text: "Workspace"; font.pixelSize: 12; color: Theme.secondary; Layout.leftMargin: 10; Layout.bottomMargin: 12 }
            }
        }
        ColumnLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 0
            Rectangle {
                Layout.fillWidth: true; implicitHeight: 56; color: Theme.chrome
                Rectangle { height: 1; width: parent.width; anchors.bottom: parent.bottom; color: Theme.border }
                RowLayout {
                    anchors.fill: parent; anchors.leftMargin: Theme.padding; anchors.rightMargin: Theme.padding; spacing: 14
                    Label { text: "Mantis Studio"; font.pixelSize: 14 }
                    Rectangle { implicitWidth: 1; implicitHeight: 18; color: Theme.border }
                    Label { text: window.currentPage.title; color: Theme.muted; font.pixelSize: 12 }
                    Item { Layout.fillWidth: true }
                    SourceBadge { objectName: "modeBadge"; source: window.uiMode }
                    StatusIndicator { status: state.runtime }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true; Layout.fillHeight: true
                Layout.margins: window.workspace === "home" || window.workspace === "projects" ? 12 : window.compact ? 16 : Theme.padding
                spacing: Theme.gap
                RowLayout {
                    visible: window.workspace !== "home" && window.workspace !== "projects"
                    Layout.fillWidth: true
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: 6
                        Label { objectName: "workspaceTitle"; text: window.currentPage.title; font.pixelSize: Theme.titleSize; font.bold: true }
                        Label { visible: window.workspace !== "home"; text: window.currentPage.description; color: Theme.secondary; Layout.fillWidth: true; wrapMode: Text.Wrap }
                    }
                    SourceBadge { visible: window.legacyVisible || window.workspace === "calibration"; source: window.uiMode === "mock" ? "mock" : "live" }
                }
                Label { visible: uiMode === "mock" && (window.legacyVisible || window.workspace === "calibration"); text: "Offline Mock preview · runtime controls disabled"; color: Theme.warning }
                AcquisitionWorkspace {
                    objectName: "acquisitionWorkspace"
                    Layout.fillWidth: true; Layout.fillHeight: true; visible: window.legacyVisible
                    studio: window.studioBridge; enabled: window.uiMode !== "mock"
                }
                CalibrationWorkspace {
                    Layout.fillWidth: true; Layout.fillHeight: true; visible: window.workspace === "calibration"
                    controller: calibration; studio: window.studioBridge; enabled: window.uiMode !== "mock"
                }
                HomeWorkspace {
                    objectName: "homeWorkspace"
                    Layout.fillWidth: true; Layout.fillHeight: true
                    visible: window.workspace === "home"
                    bridge: window.studioBridge; mode: window.uiMode
                    onNavigate: function(route) {
                        if (route === "calibration") window.openCalibration("")
                        else window.workspace = route
                    }
                }
                ProjectsWorkspace {
                    objectName: "projectsWorkspace"
                    Layout.fillWidth: true; Layout.fillHeight: true
                    visible: window.workspace === "projects"
                    bridge: window.studioBridge; mode: window.uiMode
                    onNavigate: function(route) { window.workspace = route }
                }
                FoundationWorkspace {
                    objectName: "foundationWorkspace"
                    Layout.fillWidth: true; Layout.fillHeight: true
                    visible: !window.legacyVisible && window.workspace !== "calibration" && window.workspace !== "home" && window.workspace !== "projects"
                    page: window.currentPage; state: window.appUiState
                    onOpenAcquisition: window.workspace = "acquisition"
                    onOpenDevices: window.workspace = "devices"
                    onOpenCalibration: function(deviceId) { window.openCalibration(deviceId) }
                }
            }
            Rectangle {
                Layout.fillWidth: true; implicitHeight: 30; color: Theme.chrome
                Rectangle { implicitHeight: 1; width: parent.width; color: Theme.border }
                RowLayout {
                    anchors.fill: parent; anchors.leftMargin: 20; anchors.rightMargin: 20
                    Label {
                        objectName: "runtimeMessage"
                        textFormat: Text.PlainText
                        text: window.uiMode === "mock" ? "Demo / Mock · sample content only" : window.studioBridge.error.length > 0 ? (window.studioBridge.connected ? "Operation failed · " : "Runtime state unconfirmed · ") + window.studioBridge.error : window.studioBridge.project.length > 0 ? window.studioBridge.project : "Live source · awaiting runtime"
                        elide: Text.ElideMiddle; Layout.fillWidth: true; color: window.studioBridge.error.length > 0 && window.uiMode !== "mock" ? Theme.warning : Theme.muted; font.pixelSize: Theme.captionSize
                    }
                    Label { text: "mm  ·  X right / Y forward / Z up"; font.pixelSize: Theme.captionSize; color: Theme.muted }
                }
            }
        }
    }
}
