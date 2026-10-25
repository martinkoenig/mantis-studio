import QtQuick
// Local illustrative descriptors. Never attached to a runtime model or controller.
QtObject {
    readonly property var nodes: [
        {id:"demo-device",name:"Studio scanner",group:"Scanners",depth:0,selectable:true,source:"mock",plugin:"Illustrative adapter",displayId:"demo-device",parent:"",children:["demo-left","demo-right"],capabilities:[],metadata:[],art:"scanner",description:"Original mechanical study · no physical device or live telemetry"},
        {id:"demo-left",name:"Left imaging module",group:"Scanners",depth:1,selectable:true,source:"mock",plugin:"Illustrative adapter",displayId:"demo-left",parent:"demo-device",children:[],capabilities:[],metadata:[],art:"camera",description:"Illustrative component · not a calibration parent"},
        {id:"demo-right",name:"Right imaging module",group:"Scanners",depth:1,selectable:true,source:"mock",plugin:"Illustrative adapter",displayId:"demo-right",parent:"demo-device",children:[],capabilities:[],metadata:[],art:"camera",description:"Illustrative component · not a calibration parent"},
        {id:"demo-rotary",name:"Rotary table",group:"Motion systems",depth:0,selectable:true,source:"mock",plugin:"Illustrative motion adapter",displayId:"demo-rotary",parent:"",children:[],capabilities:[],metadata:[],art:"rotor",description:"Demo motion study · no position or control contract"},
        {id:"demo-stage",name:"Linear stage",group:"Motion systems",depth:0,selectable:true,source:"mock",plugin:"Unavailable example",displayId:"demo-stage",parent:"",children:[],capabilities:[],metadata:[],art:"stage",description:"Unavailable example · illustrative only"},
        {id:"demo-trigger",name:"Trigger box",group:"I/O & triggers",depth:0,selectable:true,source:"mock",plugin:"Illustrative timing adapter",displayId:"demo-trigger",parent:"",children:[],capabilities:[],metadata:[],art:"trigger",description:"Illustrative timing device · no outputs connected"},
        {id:"demo-aux",name:"Auxiliary camera",group:"Other devices",depth:0,selectable:true,source:"mock",plugin:"Illustrative image adapter",displayId:"demo-aux",parent:"",children:[],capabilities:[],metadata:[],art:"camera",description:"Illustrative camera · no stream opened"}
    ]
}
