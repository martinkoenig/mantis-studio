import QtQuick
import QtQuick.Layouts
import "../design"
import "../components"
Column {
    id: root
    required property var projects
    spacing: 10
    RowLayout {
        width: parent.width
        HomeText { text: "Example projects"; color: Theme.text; font.bold: true; Layout.fillWidth: true }
        SourceBadge { source: "mock" }
    }
    Grid {
        id: cards
        width: parent.width; columns: width < 760 ? 2 : 4; spacing: 12
        Repeater {
            model: root.projects
            delegate: Panel {
                required property var modelData
                width: (cards.width - (cards.columns - 1) * cards.spacing) / cards.columns
                implicitHeight: 142
                clip: true
                Column {
                    width: parent.width; spacing: 6
                    Rectangle {
                        width: parent.width; height: 78
                        color: Theme.raised
                        GeometryArt { anchors.fill: parent; variant: modelData.variant }
                    }
                    HomeText { width: parent.width - 24; x: 12; text: modelData.name; font.bold: true; color: Theme.text; maximumLineCount: 1 }
                    HomeText { width: parent.width - 24; x: 12; text: "Demo / Mock · read-only"; font.pixelSize: 10; color: Theme.warning; maximumLineCount: 1 }
                }
            }
        }
    }
}
