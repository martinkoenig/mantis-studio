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
    onPaint: {
        const c = getContext("2d")
        c.reset(); c.scale(width / 24, height / 24)
        c.strokeStyle = color; c.lineWidth = 1.6; c.lineCap = "round"; c.lineJoin = "round"
        function line(points) {
            c.beginPath(); c.moveTo(points[0], points[1])
            for (let i = 2; i < points.length; i += 2) c.lineTo(points[i], points[i+1])
            c.stroke()
        }
        function circle(x,y,r) { c.beginPath(); c.arc(x,y,r,0,Math.PI*2); c.stroke() }
        switch (name) {
        case "home": line([3,11,12,3,21,11]); line([5,10,5,21,10,21,10,15,14,15,14,21,19,21,19,10]); break
        case "scan": circle(12,11,5); line([3,8,3,3,8,3]); line([16,3,21,3,21,8]); line([3,16,3,21,8,21]); line([16,21,21,21,21,16]); line([12,8,12,14]); break
        case "process": line([6,6,18,6,18,10]); line([18,18,6,18,6,14]); line([15,8,18,11,21,8]); line([3,16,6,13,9,16]); circle(12,12,2); break
        case "learn": line([3,4,10,4,12,6,14,4,21,4,21,20,14,20,12,22,10,20,3,20,3,4]); line([12,6,12,22]); break
        case "quickscan": line([13,2,5,14,11,14,10,22,19,10,13,10,13,2]); break
        case "inspect": circle(10,10,6); line([15,15,21,21]); line([10,7,10,13]); line([7,10,13,10]); break
        case "reverse": circle(6,6,3); circle(18,6,3); circle(6,18,3); circle(18,18,3); break
        case "automate": case "plugins": circle(12,5,3); circle(5,18,3); circle(19,18,3); line([12,8,12,12,5,12,5,15]); line([12,12,19,12,19,15]); break
        case "projects": line([3,7,3,20,21,20,21,7,12,7,10,4,3,4,3,7]); line([9,14,15,14]); line([12,11,12,17]); break
        case "devices": line([7,4,17,4,20,8,20,18,16,21,8,21,4,18,4,8,7,4]); circle(12,10,3); line([9,17,15,17]); break
        case "settings": circle(12,12,4); circle(12,12,8); line([12,1,12,4]); line([12,20,12,23]); line([1,12,4,12]); line([20,12,23,12]); line([4,4,6,6]); line([18,18,20,20]); line([4,20,6,18]); line([18,6,20,4]); break
        case "mantis": line([12,21,4,15,2,6,4,2,10,8,12,16,14,8,20,2,22,6,20,15,12,21,12,10]); break
        default: circle(12,12,8); line([12,8,12,13]); circle(12,17,0.5)
        }
    }
}
