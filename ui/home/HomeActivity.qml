import QtQuick
import QtQuick.Layouts
import "../design"
import "../components"
Column {
    id: root
    required property var snapshot
    property string freshness: ""
    readonly property bool illustrative: snapshot.source === "mock"
    readonly property var groups: illustrative ? [{title: "Illustrative activity · no runtime operations", kind: "activity", rows: snapshot.activity}] : [
        {title: "Runtime events · descending sequence", kind: "events", rows: snapshot.events},
        {title: "Project artifacts · deterministic ID order", kind: "artifacts", rows: snapshot.artifacts}]
    spacing: 10
    RowLayout {
        width: parent.width
        HomeText { objectName: "homeActivityHeading"; text: "Recent Activity"; color: Theme.text; font.pixelSize: 16; font.bold: true; Layout.fillWidth: true }
        HomeText { text: root.illustrative ? "Demo / Mock" : "Live snapshot"; color: root.illustrative ? Theme.warning : Theme.muted; font.pixelSize: 11 }
    }
    Panel {
        width: parent.width
        implicitHeight: table.implicitHeight + 2
        clip: true
        Column {
            id: table
            x: 1; y: 1; width: parent.width - 2; spacing: 0
            HomeText { visible: root.freshness.length > 0; width: parent.width - 24; x: 12; padding: 6; text: root.freshness; color: Theme.warning; font.pixelSize: 11 }
            Row {
                width: parent.width; height: 30
                Repeater {
                    model: [{label: "Name", fraction: 0.42}, {label: "Type", fraction: 0.18}, {label: "Status", fraction: 0.16}, {label: "Date", fraction: 0.16}, {label: "Size", fraction: 0.08}]
                    delegate: HomeText {
                        required property var modelData
                        width: table.width * modelData.fraction; height: 30; leftPadding: 12; verticalAlignment: Text.AlignVCenter
                        text: modelData.label; font.pixelSize: 11; color: Theme.muted; maximumLineCount: 1
                    }
                }
            }
            Repeater {
                model: root.groups
                delegate: Column {
                    id: group
                    required property var modelData
                    width: table.width; spacing: 0
                    HomeText {
                        visible: !root.illustrative
                        width: parent.width; height: 26; leftPadding: 12; verticalAlignment: Text.AlignVCenter
                        text: group.modelData.title; font.pixelSize: 10; color: root.illustrative ? Theme.muted : Theme.secondary; maximumLineCount: 1
                    }
                    Repeater {
                        model: group.modelData.rows
                        delegate: Rectangle {
                            id: entry
                            required property var modelData
                            required property int index
                            objectName: "home_" + group.modelData.kind + "_" + index + "_" + root.snapshot.source
                            width: group.width; height: root.illustrative ? 30 : 52
                            color: index % 2 ? "transparent" : Theme.chrome
                            Rectangle { width: parent.width; height: 1; color: Theme.border; opacity: 0.45 }
                            Row {
                                anchors.fill: parent
                                Column {
                                    width: parent.width * 0.42; y: root.illustrative ? 7 : 8; spacing: 3
                                    HomeText {
                                        objectName: "homeRowName"; width: parent.width - 24; x: 12
                                        text: group.modelData.kind === "artifacts" ? entry.modelData.name.replace("org.mantis.", "") : entry.modelData.name
                                        font.pixelSize: 12; color: Theme.text; maximumLineCount: 1
                                    }
                                    HomeText {
                                        objectName: "homeActivityDetail"; visible: !root.illustrative; width: parent.width - 24; x: 12
                                        text: (group.modelData.kind === "events" ? "#" + entry.modelData.sequence + " · " : "") + (entry.modelData.detail || "")
                                        font.pixelSize: 10; maximumLineCount: 1
                                    }
                                }
                                HomeText { objectName: "homeActivityType"; width: parent.width * 0.18; height: parent.height; leftPadding: 12; verticalAlignment: Text.AlignVCenter; text: root.illustrative ? entry.modelData.type : group.modelData.kind === "events" ? "Runtime event" : "Project artifact"; font.pixelSize: 11; maximumLineCount: 1 }
                                HomeText { objectName: "homeActivityStatus"; width: parent.width * 0.16; height: parent.height; leftPadding: 12; verticalAlignment: Text.AlignVCenter; text: entry.modelData.state; color: entry.modelData.state === "Failed" || entry.modelData.state === "error" ? Theme.error : entry.modelData.state === "RECOVERABLE" ? Theme.warning : Theme.secondary; font.pixelSize: 11; maximumLineCount: 1 }
                                HomeText { objectName: "homeActivityDate"; width: parent.width * 0.16; height: parent.height; leftPadding: 12; verticalAlignment: Text.AlignVCenter; text: root.illustrative ? entry.modelData.date : "—"; font.pixelSize: 11; maximumLineCount: 1; Accessible.description: root.illustrative ? "Illustrative date" : "Runtime supplies no trusted timestamp" }
                                HomeText { objectName: "homeActivitySize"; width: parent.width * 0.08; height: parent.height; leftPadding: 12; verticalAlignment: Text.AlignVCenter; text: root.illustrative ? entry.modelData.size : "—"; font.pixelSize: 11; maximumLineCount: 1; Accessible.description: root.illustrative ? "Illustrative size" : "Runtime supplies no file size" }
                            }
                        }
                    }
                    HomeText {
                        visible: group.modelData.rows.length === 0
                        width: parent.width - 24; x: 12; padding: 8; font.pixelSize: 11
                        text: root.snapshot.confirmed ? (group.modelData.kind === "events" ? root.snapshot.eventsAvailable && root.snapshot.eventsCount === 0 ? "No events in this snapshot." : "Event data unavailable." : root.snapshot.artifactsAvailable && root.snapshot.artifactsCount === 0 ? "No artifacts in this snapshot." : "Artifact data unavailable.") : "Current state unconfirmed · no activity inferred."
                    }
                }
            }
            HomeText { visible: !root.illustrative; width: parent.width - 24; x: 12; padding: 6; font.pixelSize: 10; text: "Date / size unavailable · groups do not imply a shared chronology.\n" + root.snapshot.eventsSummary + " · " + root.snapshot.artifactsSummary }
        }
    }
}
