import QtQuick
import QtQuick.Layouts
import "../design"
import "../components"
Column {
    id: root
    property var rows: []
    property string kind: "artifacts"
    property string source: "live"
    spacing: root.kind === "artifacts" ? 2 : 8
    Repeater {
        model: root.rows
        delegate: Rectangle {
            required property var modelData
            required property int index
            objectName: "home_" + root.kind + "_" + index + "_" + root.source
            width: root.width
            implicitHeight: entry.implicitHeight + (root.kind === "artifacts" ? 8 : 12)
            color: index % 2 ? "transparent" : Theme.chrome
            radius: 4
            RowLayout {
                id: entry
                anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                anchors.margins: root.kind === "artifacts" ? 4 : 6; spacing: 10
                StudioIcon { name: root.kind === "devices" ? "devices" : root.kind === "jobs" ? "process" : root.kind === "events" ? "automate" : "projects"; color: root.source === "mock" ? Theme.warning : Theme.accent; Layout.alignment: Qt.AlignTop }
                ColumnLayout {
                    Layout.fillWidth: true; Layout.minimumWidth: 0; spacing: 3
                    RowLayout {
                        Layout.fillWidth: true
                        HomeText { objectName: "homeRowName"; text: root.kind === "artifacts" ? modelData.name.replace("org.mantis.", "") : modelData.name; color: Theme.text; font.bold: true; Layout.fillWidth: true; Layout.minimumWidth: 0; maximumLineCount: 2 }
                        HomeText { text: modelData.state; font.pixelSize: 11; color: modelData.state === "Failed" ? Theme.error : modelData.state === "Cancelled" || modelData.state === "RECOVERABLE" ? Theme.warning : Theme.secondary; maximumLineCount: 2; Layout.maximumWidth: root.width * 0.38; visible: root.kind !== "devices" }
                    }
                    HomeText { text: modelData.state; color: root.source === "mock" ? Theme.warning : Theme.secondary; font.pixelSize: 11; visible: root.kind === "devices"; Layout.fillWidth: true }
                    HomeText { text: (root.kind === "events" ? "#" + modelData.sequence + " · " : "") + modelData.detail; font.pixelSize: 11; visible: text.length > 0 && !(root.kind === "artifacts" && root.source === "mock"); Layout.fillWidth: true; maximumLineCount: root.kind === "devices" ? 3 : 2 }
                    RowLayout {
                        visible: root.kind === "jobs"
                        Layout.fillWidth: true
                        Rectangle {
                            Layout.fillWidth: true; implicitHeight: 4; radius: 2; color: Theme.border
                            visible: modelData.progressKnown === true
                            Rectangle { width: parent.width * (modelData.progress || 0); height: 4; radius: 2; color: modelData.state === "Failed" ? Theme.error : Theme.accent }
                            Accessible.role: Accessible.ProgressBar
                            Accessible.name: modelData.name + " " + modelData.progressText
                        }
                        HomeText { text: modelData.progressText || ""; font.pixelSize: 10 }
                    }
                }
            }
        }
    }
}
