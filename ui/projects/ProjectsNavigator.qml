import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../design"
import "../components"
import "ProjectsTags.js" as Tags
Panel {
    id: root
    property bool illustrative: true
    property string facet: "All"
    property int count: 12
    property var samples: []
    signal choose(string value)
    implicitHeight: column.implicitHeight + 24
    ProjectsViewport {
        objectName: "projectsNavigatorViewport"
        anchors.fill: parent
        contentHeight: column.implicitHeight + 24
        ColumnLayout {
            id: column
            x: 12; y: 12; width: parent.parent.contentWidth - 24; spacing: 6
            ProjectsText { text: root.illustrative ? "SAMPLE LIBRARY" : "RUNTIME PROJECT"; font.pixelSize: 10; color: Theme.muted; Layout.fillWidth: true }
            Item {
                Layout.fillWidth: true; implicitHeight: categories.implicitHeight
                Column {
                    id: categories; width: parent.width; spacing: 4
                    Repeater {
                        model: ["All", "Recent", "Favorites", "Shared", "Trash"]
                        delegate: StudioButton {
                            required property string modelData
                            required property int index
                            objectName: "projectsFacet" + modelData
                            width: parent.width; implicitHeight: 32; horizontalPadding: 6
                            text: modelData === "All" ? (root.illustrative ? "All samples   " + root.count : "Current project") : modelData
                            enabled: root.illustrative || modelData === "All"
                            primary: root.facet === modelData
                            onClicked: if (enabled) root.choose(modelData)
                            Accessible.description: enabled ? "Filter illustrative packaged samples; no runtime mutation" : "Catalog category unavailable; runtime exposes only its current project"
                            Accessible.onPressAction: if (enabled && visible) clicked()
                        }
                    }
                }
            }
            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border; Layout.topMargin: 10 }
            ProjectsText { text: "Tags"; font.bold: true; Layout.topMargin: 8 }
            Item {
                visible: root.illustrative
                Layout.fillWidth: true; implicitHeight: tags.implicitHeight
                Column {
                    id: tags; width: parent.width; spacing: 4
                    Repeater {
                        model: Tags.names
                        delegate: StudioButton {
                            required property string modelData
                            required property int index
                            objectName: "projectsTag" + index
                            width: parent.width; implicitHeight: 32; horizontalPadding: 6
                            text: modelData; enabled: root.illustrative
                            contentItem: RowLayout {
                                spacing: 8
                                Rectangle { objectName: "projectsTagDot" + index; implicitWidth: 9; implicitHeight: 9; radius: 4.5; color: Tags.color(modelData) }
                                ProjectsText { text: modelData; Layout.fillWidth: true; font.pixelSize: 12; color: parent.parent.primary ? Theme.canvas : Theme.text }
                                ProjectsText { objectName: "projectsTagCount" + index; text: root.illustrative ? Tags.count(root.samples, modelData) : ""; font.pixelSize: 11; color: parent.parent.primary ? Theme.canvas : Theme.secondary }
                            }
                            primary: root.facet === modelData
                            onClicked: if (enabled) root.choose(modelData)
                            Accessible.name: modelData + (root.illustrative ? " · " + Tags.count(root.samples, modelData) + " illustrative samples" : "")
                            Accessible.description: root.illustrative ? "Illustrative tag filter, no persistence" : "Runtime does not supply project tags"
                            Accessible.onPressAction: if (enabled && visible) clicked()
                        }
                    }
                }
            }
            ProjectsText { visible: !root.illustrative; text: "Tags unavailable"; color: Theme.muted; Layout.fillWidth: true }
            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border; Layout.topMargin: 10 }
            ProjectsText { text: "Locations"; font.bold: true; Layout.topMargin: 8 }
            StudioButton {
                objectName: "projectsPackaged"
                Layout.fillWidth: true; implicitHeight: 32
                text: root.illustrative ? "Packaged samples" : "Runtime host"
                onClicked: root.choose("All")
                Accessible.description: "Reset facet; a runtime path does not establish laptop or cloud storage"
            }
            UnavailableAction { objectName: "projectsNetwork"; text: "Network catalog"; implicitHeight: 32; Layout.fillWidth: true; reason: "No project catalog API is published." }
            UnavailableAction { objectName: "projectsCloud"; text: "Cloud catalog"; implicitHeight: 32; Layout.fillWidth: true; reason: "No cloud project service is published." }
            ProjectsText { text: root.illustrative ? "Illustrative metadata\nRead-only · no persistence" : "One shared runtime project\nCatalog unavailable"; color: Theme.warning; wrapMode: Text.Wrap; Layout.fillWidth: true; font.pixelSize: 10; Layout.topMargin: 10 }
        }
    }
}
