import QtQuick
import QtQuick.Controls
import "../design"
import "../home"
Item {
    id: root
    property var rows: []
    property string selectedKey: ""
    property bool listMode: false
    property string prefix: "projectsMock"
    readonly property int columns: listMode ? 1 : Math.max(1, Math.min(6, rows.length || 1, Math.floor((width + 12) / 186)))
    readonly property real cardWidth: listMode ? width : Math.min(rows.length <= 2 ? 320 : 420, (width - (columns - 1) * 12) / columns)
    readonly property real previewHeight: Math.max(128, Math.min(230, cardWidth * .64))
    readonly property real cardHeight: listMode ? 100 : previewHeight + 110
    implicitHeight: Math.ceil(rows.length / columns) * (cardHeight + 12) - (rows.length ? 12 : 0)
    signal select(string key)
    signal revealFocusedCard(var item)
    // A fixed set of slots survives filtering, mode changes, and resizing. It is
    // outside Layout caches; only bounded view bindings and geometry change.
    Repeater {
        id: cards
        model: 12
        delegate: Button {
            id: card
            required property int index
            readonly property var row: root.rows[index] || ({key:"",name:"",image:"",type:"",tag:"",date:"",favorite:false})
            objectName: root.prefix + "Card" + index
            visible: index < root.rows.length
            x: (index % root.columns) * (root.cardWidth + 12)
            y: Math.floor(index / root.columns) * (root.cardHeight + 12)
            width: root.cardWidth; height: root.cardHeight
            padding: 0; hoverEnabled: true; activeFocusOnTab: true
            function scheduleReveal() {
                if (activeFocus) Qt.callLater(card.revealIfFocused)
            }
            function revealIfFocused() { if (card.activeFocus) root.revealFocusedCard(card) }
            onActiveFocusChanged: scheduleReveal()
            onYChanged: scheduleReveal()
            onHeightChanged: scheduleReveal()
            onClicked: root.select(row.key)
            Keys.onRightPressed: if (index + 1 < root.rows.length) cards.itemAt(index + 1).forceActiveFocus(Qt.TabFocusReason)
            Keys.onLeftPressed: if (index > 0) cards.itemAt(index - 1).forceActiveFocus(Qt.TabFocusReason)
            Keys.onDownPressed: if (index + root.columns < root.rows.length) cards.itemAt(index + root.columns).forceActiveFocus(Qt.TabFocusReason)
            Keys.onUpPressed: if (index >= root.columns) cards.itemAt(index - root.columns).forceActiveFocus(Qt.TabFocusReason)
            Accessible.role: Accessible.Button
            Accessible.name: row.name + " · illustrative sample"
            Accessible.description: row.type + " · " + row.tag + " · " + row.date + ". Illustrative metadata. Select read-only demo details. No runtime project will be opened."
            Accessible.onPressAction: if (enabled && visible) clicked()
            contentItem: Item {
                HomePreview {
                    objectName: "projectsPreview"
                    x: 1; y: 1
                    width: root.listMode ? 144 : parent.width - 2
                    height: root.listMode ? parent.height - 2 : root.previewHeight
                    source: Qt.resolvedUrl(card.row.image)
                    safePadding: Math.max(12, Math.min(24, width * .07))
                }
                ProjectsText {
                    x: root.listMode ? 158 : 12; y: root.listMode ? 12 : root.previewHeight + 10
                    width: parent.width - x - 10; text: card.row.name; font.bold: true; font.pixelSize: 13
                }
                Flow {
                    x: root.listMode ? 158 : 12; y: root.listMode ? 37 : root.previewHeight + 32
                    width: parent.width - x - 10; spacing: 5
                    ProjectsChip { text: card.row.type }
                    ProjectsChip { text: card.row.tag }
                }
                ProjectsText {
                    x: root.listMode ? 158 : 12; y: root.listMode ? 72 : root.previewHeight + 88
                    width: parent.width - x - 10; text: "Sample · " + card.row.date; color: Theme.secondary; font.pixelSize: 11
                }
                ProjectsText { x: 8; y: 7; text: card.row.favorite ? "★" : ""; color: Theme.warning; font.pixelSize: 18 }
            }
            background: Rectangle {
                radius: Theme.radius; color: card.hovered ? Theme.hover : Theme.panel
                border.width: card.visualFocus || root.selectedKey === card.row.key ? 2 : 1
                border.color: card.visualFocus ? Theme.focus : root.selectedKey === card.row.key ? Theme.accent : Theme.border
            }
        }
    }
}
