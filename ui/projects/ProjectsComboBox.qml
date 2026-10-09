import QtQuick
import QtQuick.Controls
import "../design"
ComboBox {
    id: root
    implicitHeight: 32
    activeFocusOnTab: true
    contentItem: ProjectsText {
        text: root.displayText; color: root.enabled ? Theme.text : Theme.muted
        leftPadding: 10; rightPadding: 26; verticalAlignment: Text.AlignVCenter
    }
    indicator: ProjectsText {
        x: root.width - 24; y: (root.height - height) / 2
        width: 20; text: "⌄"; horizontalAlignment: Text.AlignHCenter; color: Theme.secondary
        Accessible.ignored: true
    }
    background: Rectangle {
        color: Theme.raised; radius: Theme.radius
        border.color: root.visualFocus ? Theme.focus : Theme.border; border.width: root.visualFocus ? 2 : 1
    }
}
