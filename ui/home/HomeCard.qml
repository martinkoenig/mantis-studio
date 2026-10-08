import QtQuick
import QtQuick.Layouts
import "../design"
import "../components"
Panel {
    id: root
    property string title
    property string source: "live"
    property string freshness: ""
    default property alias content: body.data
    implicitHeight: body.implicitHeight + 32
    ColumnLayout {
        id: body
        anchors.fill: parent; anchors.margins: 16; spacing: 10
        RowLayout {
            Layout.fillWidth: true
            HomeText { text: root.title; color: Theme.text; font.bold: true; Layout.fillWidth: true }
            SourceBadge { source: root.source }
        }
        HomeText { visible: root.freshness.length > 0; text: root.freshness; color: Theme.warning; font.pixelSize: 11; Layout.fillWidth: true }
    }
}
