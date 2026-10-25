import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../design"
import "../components"
Popup {
    id: root
    objectName: "homeGuide"
    signal navigate(string route)
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(560, parent.width - 48)
    padding: 24
    modal: true; focus: true
    onOpened: closeButton.forceActiveFocus(Qt.TabFocusReason)
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Panel { border.color: Theme.accent }
    contentItem: ColumnLayout {
        spacing: 16
        HomeText { text: "From object to traceable result"; font.pixelSize: 22; font.bold: true; color: Theme.text; Layout.fillWidth: true }
        HomeText { text: "01  Discover your source"; font.bold: true; color: Theme.accent; Layout.fillWidth: true }
        HomeText { text: "Use Devices to inspect advertised capabilities. Logical discovery does not establish physical readiness. Calibration is a separate guided workflow."; Layout.fillWidth: true; maximumLineCount: 5 }
        HomeText { text: "02  Acquire or replay"; font.bold: true; color: Theme.accent; Layout.fillWidth: true }
        HomeText { text: "Open Scan to review capture controls and recorded sources. Home navigation never starts capture; commands remain owned by the runtime."; Layout.fillWidth: true; maximumLineCount: 5 }
        HomeText { text: "03  Review and export"; font.bold: true; color: Theme.accent; Layout.fillWidth: true }
        HomeText { text: "Review job diagnostics and immutable artifacts in Acquisition. Select real artifacts there to replay, process or export supported data. Illustrative Home projects are read-only."; Layout.fillWidth: true; maximumLineCount: 5 }
        StudioButton { objectName: "homeGuideDevices"; text: "Open Devices"; Layout.fillWidth: true; onClicked: { root.close(); root.navigate("devices") } Accessible.description: "Open Devices without selecting or calibrating hardware" }
        StudioButton { id: closeButton; objectName: "homeGuideClose"; text: "Back to Home"; primary: true; Layout.fillWidth: true; onClicked: root.close() }
    }
}
