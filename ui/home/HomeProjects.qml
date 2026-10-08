import QtQuick
import QtQuick.Layouts
import "../design"
import "../components"
Column {
    id: root
    objectName: "homeProjectGallery"
    required property var projects
    spacing: 10
    Accessible.role: Accessible.Pane
    Accessible.name: "Illustrative recent projects, read-only"
    RowLayout {
        width: parent.width
        HomeText { text: "Recent Projects"; font.pixelSize: 16; color: Theme.text; font.bold: true; Layout.fillWidth: true }
        HomeText { text: "Illustrative · read-only"; font.pixelSize: 11; color: Theme.warning }
    }
    Grid {
        id: cards
        width: parent.width; columns: width < 760 ? 2 : 4; spacing: 12
        Repeater {
            model: root.projects
            delegate: Panel {
                required property var modelData
                required property int index
                objectName: "homeProjectCard" + index
                width: (cards.width - (cards.columns - 1) * cards.spacing) / cards.columns
                implicitHeight: 198
                clip: true
                border.color: root.activeFocus ? Theme.focus : Theme.border
                Column {
                    width: parent.width; spacing: 0
                    Image {
                        objectName: "homeProjectArt" + index
                        width: parent.width; height: 120
                        source: visible ? "assets/" + modelData.shape + ".jpg" : ""
                        sourceSize.width: 640; sourceSize.height: 380
                        fillMode: Image.PreserveAspectFit
                        Accessible.ignored: true
                    }
                    Column {
                        width: parent.width - 28; x: 14; topPadding: 10; spacing: 6
                        HomeText { width: parent.width; text: modelData.name; font.pixelSize: 14; font.bold: true; color: Theme.text; maximumLineCount: 1 }
                        HomeText { width: parent.width; text: modelData.description; font.pixelSize: 11; maximumLineCount: 1 }
                        HomeText { width: parent.width; text: modelData.stats; font.pixelSize: 11; color: Theme.secondary; maximumLineCount: 1 }
                    }
                }
            }
        }
    }
}
