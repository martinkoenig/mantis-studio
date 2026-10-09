import QtQuick
import "../design"
Rectangle {
    id: root
    property url source
    property real safePadding: Math.max(12, Math.min(22, width * 0.065))
    property alias image: foreground
    radius: Theme.radius
    gradient: Gradient {
        GradientStop { position: 0; color: Theme.previewTop }
        GradientStop { position: 1; color: Theme.previewBottom }
    }
    // Bounded, static contact shadow. No floor pixels in the foreground PNG.
    Repeater {
        model: 8
        delegate: Rectangle {
            required property int index
            width: foreground.paintedWidth * (0.8 - index * 0.065)
            height: Math.max(3, foreground.paintedHeight * (0.13 - index * 0.012))
            radius: height / 2; color: "#08000000"
            x: (root.width - width) / 2
            y: (root.height + foreground.paintedHeight) / 2 - height / 2 - 3
        }
    }
    Image {
        id: foreground
        anchors.fill: parent; anchors.margins: root.safePadding
        source: visible ? root.source : ""
        // One decode/cache size per subject, sharp at the supported DPR 2.
        sourceSize.width: 520; sourceSize.height: 355
        fillMode: Image.PreserveAspectFit
        Accessible.ignored: true
    }
    Rectangle { height: 1; width: parent.width; anchors.bottom: parent.bottom; color: Theme.border; opacity: 0.5 }
    Accessible.ignored: true
}
