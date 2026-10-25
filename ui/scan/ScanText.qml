import QtQuick
import QtQuick.Controls
import "../design"
Label {
    textFormat: Text.PlainText
    color: Theme.secondary
    font.pixelSize: 12
    wrapMode: Text.Wrap
    Accessible.role: Accessible.StaticText
    Accessible.name: text
}
