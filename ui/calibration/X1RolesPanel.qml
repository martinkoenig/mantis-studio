import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

Frame {
    id: root
    objectName: "x1RolesPanel"
    property var controller: null
    readonly property var data: controller ? controller.state : ({})
    property string confirmationKey: ""
    property string confirmationText: ""
    signal reviewActiveRig()
    Layout.fillWidth: true
    Layout.minimumWidth: 0
    function mapping(rows) {
        let lines = []
        for (let i = 0; i < rows.length; ++i) {
            const c = rows[i]
            lines.push(c.role + "\nPhysical identity: " + c.identity + "\nSensor: " + c.sensor
                + "\nBus: " + c.bus + "\nRoute: " + c.route.join(" → ") + "\nResolved node (not identity): " + c.node)
        }
        return lines.join("\n\n") || "No physical assignment confirmed"
    }
    function revealFocus() {
        const item = root.Window.window ? root.Window.window.activeFocusItem : null
        if (!root.visible || !item) return
        let p = item
        while (p && p !== root) p = p.parent
        if (p !== root) return
        p = root.parent
        while (p) {
            if (typeof p.contentY === "number" && p.contentItem) {
                const y = item.mapToItem(p.contentItem, 0, 0).y
                const limit = Math.max(0, p.contentHeight - p.height)
                if (y < p.contentY) p.contentY = Math.max(0, Math.min(y, limit))
                else if (y + item.height > p.contentY + p.height)
                    p.contentY = Math.max(0, Math.min(limit, y + Math.min(item.height, p.height) - p.height))
            }
            p = p.parent
        }
    }
    Connections { target: root.Window.window; function onActiveFocusItemChanged() { Qt.callLater(root.revealFocus) } }
    onWidthChanged: Qt.callLater(root.revealFocus)
    function review() {
        if (!root.controller || !root.data.canApply) return
        root.confirmationKey = root.data.confirmation
        root.confirmationText = "Parent: " + root.data.parent + "\nGeneration: " + root.data.generation
            + "\nDiscovery: " + root.data.discovery + "\nBase: " + root.data.base
            + "\n\nCURRENT\n" + root.mapping(root.data.current || [])
            + "\n\nPROPOSED\n" + root.mapping(root.data.proposed || [])
        acknowledged.checked = false
        Qt.callLater(function() { acknowledged.forceActiveFocus(Qt.TabFocusReason) })
    }
    onVisibleChanged: if (!visible) confirmationKey = ""
    Connections {
        target: root.controller
        function onChanged() {
            if (root.confirmationKey !== (root.data.confirmation || "")) root.confirmationKey = ""
        }
    }
    ColumnLayout {
        width: parent.width
        spacing: 12
        Label { text: "Physical X1 camera roles"; font.pixelSize: 20; font.bold: true; Layout.fillWidth: true; wrapMode: Text.Wrap }
        Label {
            Layout.fillWidth: true; wrapMode: Text.Wrap; textFormat: Text.PlainText
            text: "Assign the two complete physical sensor / bus / route tuples to LEFT and RIGHT. Preview direction never identifies a sensor. Software configuration does not verify physical LEFT / RIGHT accuracy."
        }
        Label {
            objectName: "x1RolesAvailability"
            Layout.fillWidth: true; wrapMode: Text.Wrap; textFormat: Text.PlainText
            text: "Capability v1: " + (!root.data.fresh ? "unconfirmed" : root.data.capability ? "advertised" : "not advertised")
                + " · Daemon opt-in / support: " + (!root.data.fresh ? "unconfirmed" : root.data.supported ? "enabled" : "absent or unsupported")
                + "\nState: " + (!root.data.fresh ? "unknown / stale" : root.data.blocked ? "blocked" : root.data.busy || root.data.jobId ? "busy / Job retiring" : "queried")
                + "\n" + (root.data.reason || "Ready for an explicit proposal")
                + (root.data.diagnostic ? "\nDaemon: " + root.data.diagnostic : "")
        }
        TextArea {
            objectName: "x1RolesProvenance"
            Layout.fillWidth: true; Layout.minimumWidth: 0; readOnly: true; selectByMouse: true
            wrapMode: TextEdit.WrapAnywhere; textFormat: TextEdit.PlainText
            Accessible.name: "Exact X1 parent, generation, discovery and base profile provenance"
            text: "Parent: " + (root.data.parent || "unknown") + "\nGeneration: " + (root.data.generation || "unknown")
                + "\nDiscovery: " + (root.data.discovery || "unknown") + "\nBase: " + (root.data.base || "unknown")
        }
        Label { text: root.data.fresh ? "Current daemon assignment" : "Last-known assignment · unconfirmed"; font.bold: true }
        TextArea {
            objectName: "x1RolesCurrent"
            Layout.fillWidth: true; Layout.minimumWidth: 0; readOnly: true; selectByMouse: true
            wrapMode: TextEdit.WrapAnywhere; textFormat: TextEdit.PlainText
            Accessible.name: "Current physical LEFT and RIGHT identities, sensors, buses, routes and node resolutions"
            text: root.mapping(root.data.current || [])
        }
        Label { text: "Proposed assignment"; font.bold: true; visible: !!root.data.hasChange }
        TextArea {
            objectName: "x1RolesProposed"
            visible: !!root.data.hasChange
            Layout.fillWidth: true; Layout.minimumWidth: 0; readOnly: true; selectByMouse: true
            wrapMode: TextEdit.WrapAnywhere; textFormat: TextEdit.PlainText
            Accessible.name: "Proposed complete physical LEFT and RIGHT assignment"
            text: root.mapping(root.data.proposed || [])
        }
        Label {
            Layout.fillWidth: true; wrapMode: Text.Wrap; textFormat: Text.PlainText
            visible: !!root.data.activeRig
            text: "Active rig: " + (root.data.activeRig || "") + ". Clearing is a separate confirmed action in Review & Activate. It changes only the active binding; historical captures and immutable revisions retain their recorded mapping."
        }
        Button {
            objectName: "x1RolesReviewRig"
            visible: !!root.data.activeRig
            text: "Review active rig / explicit clear"
            Accessible.description: "Navigate to Review and Activate. Does not clear the active rig."
            onClicked: root.reviewActiveRig()
        }
        // A column remains usable in the compact calibration content pane.
        Button {
            objectName: "x1RolesRefresh"; text: "Refresh role state"
            enabled: !!root.controller && !!root.data.connected && !root.data.pending
            onClicked: if (enabled) root.controller.refresh()
        }
        Button {
            objectName: "x1RolesSwap"; text: root.data.hasChange ? "Reset proposal" : "Propose LEFT ↔ RIGHT swap"
            enabled: !!root.controller && !!root.data.canPropose
            Accessible.description: "Exchange the complete two discovered physical tuples; no configuration is applied."
            onClicked: if (enabled) root.controller.proposeSwap()
        }
        Button {
            objectName: "x1RolesValidate"; text: "Validate proposal"
            enabled: !!root.controller && !!root.data.canValidate
            Accessible.description: "Start an asynchronous daemon validation Job; wait for confirmed completion before Apply."
            onClicked: if (enabled) root.controller.validate()
        }
        Label {
            objectName: "x1RolesJob"
            Layout.fillWidth: true; wrapMode: Text.Wrap; textFormat: Text.PlainText
            visible: !!(root.data.job || {}).state
            text: (root.data.jobPhase || "") + " · " + ((root.data.job || {}).state || "")
                + "\n" + ((root.data.job || {}).status || "") + "\n" + ((root.data.job || {}).diagnostics || "")
        }
        Label {
            objectName: "x1RolesDaemonJob"
            Layout.fillWidth: true; wrapMode: Text.Wrap; textFormat: Text.PlainText
            visible: !!(root.data.daemonJob || {}).id
            text: "Daemon configuration Job (observed): " + ((root.data.daemonJob || {}).id || "")
                + " · " + ((root.data.daemonJob || {}).state || "unconfirmed")
                + "\n" + ((root.data.daemonJob || {}).status || "") + "\n" + ((root.data.daemonJob || {}).diagnostics || "")
        }
        Button {
            objectName: "x1RolesCancel"; text: root.data.cancelRequested ? "Cancellation requested" : "Request Job cancellation"
            visible: !!root.data.jobId; enabled: !!root.controller && !!root.data.canCancel
            Accessible.description: "Request cancellation. A running commit may still complete; reservation remains busy until the daemon retires ownership."
            onClicked: if (enabled) root.controller.cancelJob()
        }
        Label {
            objectName: "x1RolesStatus"
            Layout.fillWidth: true; wrapMode: Text.Wrap; textFormat: Text.PlainText
            text: (root.data.message || "") + ((root.data.error || {}).message ? "\n[" + root.data.error.component + " / " + root.data.error.code + "] " + root.data.error.message : "")
            visible: text.length > 0
            Accessible.name: "Role configuration status: " + text
        }
        Button {
            objectName: "x1RolesReview"; text: "Review validated change…"
            enabled: !!root.data.canApply
            onClicked: root.review()
        }
        Frame {
            objectName: "x1RolesConfirmation"
            visible: root.confirmationKey.length > 0
            Layout.fillWidth: true; Layout.minimumWidth: 0
            ColumnLayout {
                width: parent.width; spacing: 12
                Label { text: "Confirm this exact assignment"; font.bold: true; Layout.fillWidth: true; wrapMode: Text.Wrap }
                TextArea {
                    objectName: "x1RolesConfirmationEvidence"
                    Layout.fillWidth: true; Layout.minimumWidth: 0; readOnly: true; selectByMouse: true
                    wrapMode: TextEdit.WrapAnywhere; textFormat: TextEdit.PlainText
                    text: root.confirmationText
                    Accessible.name: "Frozen confirmation: exact discovery, generation, parent, current and proposed physical mapping"
                }
                CheckBox {
                    id: acknowledged; objectName: "x1RolesAcknowledge"
                    Layout.fillWidth: true
                    text: "I confirm the exact physical assignments above"
                    contentItem: Label { text: acknowledged.text; wrapMode: Text.Wrap; leftPadding: acknowledged.indicator.width + acknowledged.spacing; verticalAlignment: Text.AlignVCenter }
                }
                Button {
                    objectName: "x1RolesApply"; text: "Confirm and apply roles"
                    enabled: acknowledged.checked && !!root.data.canApply && root.confirmationKey === root.data.confirmation
                    onClicked: {
                        if (!enabled) return
                        const key = root.confirmationKey
                        root.confirmationKey = ""
                        root.controller.apply(key)
                    }
                }
                Button { objectName: "x1RolesDismiss"; text: "Dismiss confirmation"; onClicked: root.confirmationKey = "" }
            }
        }
    }
}
