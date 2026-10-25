import QtQuick
import QtQuick.Controls
import "../design"
Label {
    textFormat: Text.PlainText
    color: Theme.text
    font.pixelSize: 12
    elide: Text.ElideRight
    Accessible.name: text
}
