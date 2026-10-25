import QtQuick

// Original, deterministic technical studies. Decorative art, never a scan or hardware feed.
Canvas {
    id: root
    property string shape: "housing"
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    onShapeChanged: requestPaint()
    Accessible.ignored: true
    onPaint: {
        const c = getContext("2d")
        c.reset()
        const scale = Math.min(width / 300, height / 140)
        c.translate((width - 300 * scale) / 2, (height - 140 * scale) / 2)
        c.scale(scale, scale)

        // One orthographic projection and light direction keep the four materials coherent.
        function project(u, v, z) { return [150 + 0.85 * u - 0.65 * v, 97 + 0.28 * u + 0.22 * v - 0.85 * z] }
        function path(points) {
            c.beginPath()
            c.moveTo(points[0][0], points[0][1])
            for (let i = 1; i < points.length; ++i) c.lineTo(points[i][0], points[i][1])
            c.closePath()
        }
        function face(points, fill, stroke) {
            path(points)
            c.fillStyle = fill; c.fill()
            c.strokeStyle = stroke || "#698885"; c.lineWidth = 1.2; c.stroke()
        }
        function metal(light, dark) {
            const gradient = c.createLinearGradient(40, 5, 245, 135)
            gradient.addColorStop(0, light); gradient.addColorStop(1, dark)
            return gradient
        }
        function plane(outline, z) { return outline.map(p => project(p[0], p[1], z)) }
        function extrude(outline, bottom, top) {
            const low = plane(outline, bottom), high = plane(outline, top)
            for (let i = 0; i < outline.length; ++i) {
                const next = (i + 1) % outline.length
                face([low[i], low[next], high[next], high[i]], i % 2 ? "#29423f" : "#3c5854", "#466661")
            }
            face(high, metal("#a2b9b3", "#4d6a65"))
        }
        function circle(u, v, radius, z) {
            const points = []
            for (let i = 0; i < 32; ++i) {
                const a = i * Math.PI / 16
                points.push(project(u + radius * Math.cos(a), v + radius * Math.sin(a), z))
            }
            return points
        }
        function hole(u, v, radius, z) {
            face(circle(u, v, radius + 2, z), "#b5c9c1", "#819e93")
            face(circle(u, v, radius, z), "#0a1a18", "#31514b")
        }

        // Soft grounding shadow is static and local; there is no timer or random input.
        c.save(); c.translate(150, 126); c.scale(1, 0.09)
        const shadow = c.createRadialGradient(0, 0, 8, 0, 0, 125)
        shadow.addColorStop(0, "rgba(3,12,10,0.8)"); shadow.addColorStop(1, "rgba(3,12,10,0)")
        c.beginPath(); c.arc(0,0,125,0,Math.PI*2); c.fillStyle = shadow; c.fill(); c.restore()

        if (shape === "housing") {
            // Deep chamfered casting: mounting flange, thick walls and a recessed open cavity.
            const flange = [[-90,-55], [75,-55], [90,-40], [90,40], [75,55], [-75,55], [-90,40]]
            extrude(flange, 0, 10)
            const rim = [[-70,-40], [62,-40], [72,-30], [72,30], [62,40], [-62,40], [-72,30], [-72,-30]]
            extrude(rim, 10, 48)
            const cavity = [[-53,-27], [48,-27], [56,-19], [56,19], [48,27], [-48,27], [-56,19], [-56,-19]]
            face(plane(cavity, 48), "#10221e", "#bad1c5")
            c.save(); path(plane(cavity, 48)); c.clip()
            const floor = [[-37,-16], [35,-16], [42,-9], [42,9], [35,16], [-35,16], [-42,9], [-42,-9]]
            face(plane(floor, 23), metal("#3f5b51", "#182d25"), "#4a6c58")
            hole(0, 0, 15, 23)
            c.restore()
            for (const p of [[-80,-43], [78,-40], [78,40], [-78,42]]) hole(p[0], p[1], 5, 10)
            // Visible stiffening ribs connect the casting wall to its flange.
            for (const u of [-45, 0, 45])
                face([project(u,40,43), project(u,51,10), project(u+7,51,10), project(u+7,40,43)], "#486b5b", "#7a9d84")
        } else if (shape === "rotor") {
            // Impeller: circular foot, swept radial blades and a tall central hub.
            const disc = []
            for (let i = 0; i < 40; ++i) disc.push([78 * Math.cos(i * Math.PI / 20), 78 * Math.sin(i * Math.PI / 20)])
            extrude(disc, 0, 7)
            const blades = []
            for (let i = 0; i < 11; ++i) blades.push(i * Math.PI * 2 / 11)
            blades.sort((a, b) => project(65*Math.cos(a),65*Math.sin(a),0)[1] - project(65*Math.cos(b),65*Math.sin(b),0)[1])
            for (const a of blades) {
                const outer = project(77*Math.cos(a),77*Math.sin(a),8)
                const tip = project(24*Math.cos(a-0.55),24*Math.sin(a-0.55),57)
                const back = project(25*Math.cos(a-0.15),25*Math.sin(a-0.15),54)
                const heel = project(70*Math.cos(a+0.32),70*Math.sin(a+0.32),7)
                const sweep = project(66*Math.cos(a-0.30),66*Math.sin(a-0.30),38)
                const returnSweep = project(56*Math.cos(a+0.12),56*Math.sin(a+0.12),30)
                c.beginPath(); c.moveTo(outer[0],outer[1])
                c.quadraticCurveTo(sweep[0],sweep[1],tip[0],tip[1])
                c.lineTo(back[0],back[1])
                c.quadraticCurveTo(returnSweep[0],returnSweep[1],heel[0],heel[1])
                c.closePath(); c.fillStyle = metal("#bfd0cb", "#405e58"); c.fill()
                c.strokeStyle = "#91b5a7"; c.lineWidth = 1; c.stroke()
            }
            const hub = []
            for (let i = 0; i < 32; ++i) hub.push([19*Math.cos(i*Math.PI/16),19*Math.sin(i*Math.PI/16)])
            extrude(hub, 10, 66)
            hole(0, 0, 9, 66)
        } else if (shape === "bracket") {
            // Angular L mounting bracket: open footprint, upright flange and triangular gusset.
            extrude([[-95,-38], [85,-38], [95,-28], [95,12], [0,12], [0,62], [-85,62], [-95,52]], 0, 9)
            face([project(-95,-38,9), project(-15,-38,9), project(-15,-38,66), project(-95,-38,66)], metal("#afc5be", "#506e65"))
            face([project(-95,-38,66), project(-15,-38,66), project(-15,-29,66), project(-95,-29,66)], "#b9ccc1")
            face([project(-15,-38,9), project(-15,22,9), project(-15,-38,59)], "#38594c", "#91b7a1")
            for (const p of [[70,-12], [-58,36]]) hole(p[0], p[1], 10, 9)
            // Flange hole is in its upright X/Z plane rather than the horizontal mounting foot.
            const bore = []
            for (let i = 0; i < 32; ++i) {
                const a = i * Math.PI / 16
                bore.push(project(-56+11*Math.cos(a), -38, 42+11*Math.sin(a)))
            }
            face(bore, "#0b1d17", "#d0ded3")
        } else if (shape === "cover") {
            // Shallow closed inspection cover: broad plate, bevel, parallel stiffening ribs.
            const plate = [[-100,-49], [87,-49], [100,-36], [100,36], [87,49], [-87,49], [-100,36], [-100,-36]]
            extrude(plate, 0, 9)
            for (const v of [-22, 0, 22])
                extrude([[-76,v-4], [76,v-4], [82,v], [76,v+4], [-76,v+4], [-82,v]], 9, 18)
            for (const p of [[-86,-35], [86,-35], [-86,35], [86,35]]) hole(p[0], p[1], 5, 9)
        }
    }
}
