import QtQuick
import "../design"

// Original stroke icons in a 24-unit coordinate space; no icon font or SVG plugin.
Canvas {
    id: root
    property string name: "home"
    property color color: Theme.secondary
    implicitWidth: 22; implicitHeight: 22
    onNameChanged: requestPaint()
    onColorChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    // Keep paint helpers outside the handler: Qt 6.4's ARM64 JIT can corrupt
    // captured call contexts (QTBUG-111935). Explicit arguments preserve the
    // exact strokes without per-paint closures or a global interpreter override.
    function line(c, points) {
        c.beginPath(); c.moveTo(points[0], points[1])
        for (let i = 2; i < points.length; i += 2) c.lineTo(points[i], points[i+1])
        c.stroke()
    }
    function circle(c, x, y, r) { c.beginPath(); c.arc(x,y,r,0,Math.PI*2); c.stroke() }
    onPaint: {
        const c = getContext("2d")
        c.reset(); c.scale(width / 24, height / 24)
        c.strokeStyle = color; c.lineWidth = 1.6; c.lineCap = "round"; c.lineJoin = "round"
        switch (name) {
        case "home": line(c, [3,11,12,3,21,11]); line(c, [5,10,5,21,10,21,10,15,14,15,14,21,19,21,19,10]); break
        case "scan": circle(c, 12,11,5); line(c, [3,8,3,3,8,3]); line(c, [16,3,21,3,21,8]); line(c, [3,16,3,21,8,21]); line(c, [16,21,21,21,21,16]); line(c, [12,8,12,14]); break
        case "process": line(c, [6,6,18,6,18,10]); line(c, [18,18,6,18,6,14]); line(c, [15,8,18,11,21,8]); line(c, [3,16,6,13,9,16]); circle(c, 12,12,2); break
        case "learn": line(c, [3,4,10,4,12,6,14,4,21,4,21,20,14,20,12,22,10,20,3,20,3,4]); line(c, [12,6,12,22]); break
        case "quickscan": line(c, [13,2,5,14,11,14,10,22,19,10,13,10,13,2]); break
        case "inspect": circle(c, 10,10,6); line(c, [15,15,21,21]); line(c, [10,7,10,13]); line(c, [7,10,13,10]); break
        case "reverse": circle(c, 6,6,3); circle(c, 18,6,3); circle(c, 6,18,3); circle(c, 18,18,3); break
        case "automate": case "plugins": circle(c, 12,5,3); circle(c, 5,18,3); circle(c, 19,18,3); line(c, [12,8,12,12,5,12,5,15]); line(c, [12,12,19,12,19,15]); break
        case "projects": line(c, [3,7,3,20,21,20,21,7,12,7,10,4,3,4,3,7]); line(c, [9,14,15,14]); line(c, [12,11,12,17]); break
        case "devices": line(c, [7,4,17,4,20,8,20,18,16,21,8,21,4,18,4,8,7,4]); circle(c, 12,10,3); line(c, [9,17,15,17]); break
        case "settings": circle(c, 12,12,4); circle(c, 12,12,8); line(c, [12,1,12,4]); line(c, [12,20,12,23]); line(c, [1,12,4,12]); line(c, [20,12,23,12]); line(c, [4,4,6,6]); line(c, [18,18,20,20]); line(c, [4,20,6,18]); line(c, [18,6,20,4]); break
        case "mantis": line(c, [12,21,4,15,2,6,4,2,10,8,12,16,14,8,20,2,22,6,20,15,12,21,12,10]); break
        default: circle(c, 12,12,8); line(c, [12,8,12,13]); circle(c, 12,17,0.5)
        }
    }
}
