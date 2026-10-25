import QtQuick
import "../design"
// Original procedural engineering illustration; no live scanner or measurement claim.
Canvas {
    id: root
    property int variant: 0
    property bool hero: false
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    onVariantChanged: requestPaint()
    onHeroChanged: requestPaint()
    onPaint: {
        const c = getContext("2d")
        c.reset()
        c.scale(width / 400, height / 240)
        if (hero) {
            c.strokeStyle = "#1d3938"; c.lineWidth = 0.7
            for (let i = 0; i < 9; ++i) {
                c.beginPath(); c.moveTo(i * 65 - 150, 240); c.lineTo(i * 65 + 140, 0); c.stroke()
                c.beginPath(); c.moveTo(0, i * 32); c.lineTo(400, i * 32); c.stroke()
            }
        }
        c.translate(200, 116); c.rotate(-0.2)
        c.scale(1, 0.78)
        const shape = variant % 3
        for (let layer = 5; layer >= 0; --layer) {
            c.save(); c.translate(layer * 7, -layer * 6)
            c.beginPath()
            const teeth = shape === 1 ? 12 : shape === 2 ? 6 : 18
            for (let i = 0; i <= teeth * 4; ++i) {
                const angle = i / (teeth * 4) * Math.PI * 2
                const r = 72 + (i % 4 < 2 ? 15 : 0)
                const x = Math.cos(angle) * r, y = Math.sin(angle) * r
                if (i === 0) c.moveTo(x, y); else c.lineTo(x, y)
            }
            c.closePath()
            const g = c.createLinearGradient(-80,-80,80,80)
            g.addColorStop(0, layer === 0 ? "#819c9d" : "#28423f")
            g.addColorStop(0.5, layer === 0 ? "#344f50" : "#152d2c")
            g.addColorStop(1, "#112727")
            c.fillStyle = g; c.fill(); c.strokeStyle = layer === 0 ? "#94b6b1" : "#365f59"; c.lineWidth = 1; c.stroke()
            c.beginPath(); c.arc(0,0,40,0,Math.PI*2); c.fillStyle = "#091919"; c.fill(); c.stroke()
            if (layer === 0) {
                c.strokeStyle = "#29bd91"; c.beginPath(); c.arc(0,0,48,0,Math.PI*1.6); c.stroke()
                for (let j = 0; j < 6; ++j) {
                    const a = j * Math.PI / 3
                    c.beginPath(); c.arc(Math.cos(a)*62,Math.sin(a)*62,4,0,Math.PI*2); c.fillStyle="#0b2221"; c.fill(); c.stroke()
                }
            }
            c.restore()
        }
        if (hero) {
            c.strokeStyle = "#28e0a5"; c.lineWidth = 1.2
            c.beginPath(); c.moveTo(-110,100); c.lineTo(85,100); c.lineTo(128,60); c.stroke()
            c.beginPath(); c.arc(128,60,3,0,Math.PI*2); c.stroke()
        }
    }
    Accessible.ignored: true
}
