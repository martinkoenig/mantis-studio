import QtQuick
import QtQuick.Controls
import "../design"
Label {
    id: root
    property string source: "live"
    text: source === "mock" ? "Demo / Mock" : source === "hybrid" ? "Hybrid" : "Live source"
    color: source === "live" ? Theme.secondary : Theme.warning
    font.pixelSize: Theme.captionSize
    leftPadding: 9; rightPadding: 9; topPadding: 5; bottomPadding: 5
    background: Rectangle { radius: 4; color: Theme.raised; border.color: Theme.border }
    Accessible.name: text
}
