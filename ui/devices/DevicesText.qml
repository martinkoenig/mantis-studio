import QtQuick
import QtQuick.Controls
import "../design"
Label {
    textFormat: Text.PlainText
    color: Theme.secondary
    wrapMode: Text.Wrap
    font.pixelSize: Theme.bodySize
    Accessible.role: Accessible.StaticText
    Accessible.name: text
}
