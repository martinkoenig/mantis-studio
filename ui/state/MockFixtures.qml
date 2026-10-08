import QtQuick
QtObject {
    // Presentation-only fixtures. IDs never enter a client command or live list.
    readonly property var devices: [{
        id: "demo-device", name: "Example scanner", source: "mock", synthetic: true,
        connected: false, actionable: false, status: "Demo / Mock · not connected",
        capabilities: [], capabilityLabel: "Illustrative device · no runtime capabilities"
    }]
}
