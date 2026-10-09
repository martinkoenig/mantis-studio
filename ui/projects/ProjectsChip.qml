import QtQuick
import "ProjectsTags.js" as Tags
Rectangle {
    id: root
    property string text: ""
    property color ink: Tags.color(text)
    property real maximumWidth: parent ? parent.width : 320
    width: Math.min(implicitWidth, maximumWidth)
    implicitWidth: label.implicitWidth + 12
    implicitHeight: 22
    radius: 4
    color: Qt.rgba(ink.r, ink.g, ink.b, .17)
    border.color: Qt.rgba(ink.r, ink.g, ink.b, .4)
    ProjectsText { id: label; anchors.centerIn: parent; width: Math.max(0, root.width - 12); text: root.text; color: root.ink; font.pixelSize: 11 }
    Accessible.role: Accessible.StaticText
    Accessible.name: text
}
