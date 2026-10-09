import QtQuick
QtObject {
    readonly property var projects: [
        {key:"housing", name:"X1 Housing", image:"../home/assets/housing.png", type:"Scan + CAD", tag:"Housing", favorite:true, date:"2026-10-05 14:32", description:"Illustrative flanged casting and bearing seat study."},
        {key:"rotor", name:"Impeller v2", image:"../home/assets/rotor.png", type:"Scan", tag:"R&D", favorite:true, date:"2026-10-04 16:21", description:"Original swept-blade impeller study."},
        {key:"engine", name:"Engine Block", image:"assets/engine.png", type:"Scan", tag:"Prototype", favorite:false, date:"2026-10-03 11:18", description:"Four-bore casting, port bosses and mounting features."},
        {key:"bracket-qc", name:"Bracket QC", image:"assets/bracket-qc.png", type:"Inspection", tag:"Quality Control", favorite:false, date:"2026-10-02 09:47", description:"Illustrative color palette only; no measurement or tolerance evidence."},
        {key:"pipe", name:"Intake Pipe", image:"assets/pipe.png", type:"Scan", tag:"Prototype", favorite:false, date:"2026-10-01 13:05", description:"Hollow intake and original mounting flange study."},
        {key:"bracket", name:"Mounting Bracket", image:"../home/assets/bracket.png", type:"Reverse", tag:"Housing", favorite:false, date:"2026-09-29 15:44", description:"Gusseted mount with bored foot and upright flange."},
        {key:"gear", name:"Gear Housing", image:"assets/gear.png", type:"Scan", tag:"Quality Control", favorite:true, date:"2026-09-28 10:17", description:"Splined bearing housing and machined annular lip."},
        {key:"flange", name:"Bearing Flange", image:"assets/flange.png", type:"Reverse", tag:"R&D", favorite:false, date:"2026-09-25 13:06", description:"Eight-hole flange and long bearing sleeve study."},
        {key:"enclosure", name:"Electronics Enclosure", image:"assets/enclosure.png", type:"Scan", tag:"Prototype", favorite:false, date:"2026-09-22 12:11", description:"Pocketed enclosure with an illustrative controller board."},
        {key:"cover", name:"Gearbox Cover", image:"../home/assets/cover.png", type:"CAD", tag:"Housing", favorite:false, date:"2026-09-20 09:16", description:"Ribbed inspection cover and original fastening pattern."},
        {key:"cover-qc", name:"Cover Inspection", image:"assets/cover-qc.png", type:"Inspection", tag:"Quality Control", favorite:false, date:"2026-09-18 14:22", description:"Illustrative color-map study; no real QC result."},
        {key:"manifold", name:"Manifold", image:"assets/manifold.png", type:"Scan", tag:"R&D", favorite:false, date:"2026-09-15 11:37", description:"Three-port manifold, through bores and mounting holes."}
    ]
}
