import QtQuick
import QtQuick.Controls
import "../design"
Label {
    color: Theme.secondary
    textFormat: Text.PlainText
    font.pixelSize: Theme.bodySize
    wrapMode: Text.Wrap
    maximumLineCount: 3
    elide: Text.ElideRight
    Accessible.role: Accessible.StaticText
    Accessible.name: text
}
