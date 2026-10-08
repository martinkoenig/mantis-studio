import QtQuick
QtObject {
    id: root
    required property var bridge
    property var calibrationProvider: null
    property string mode: "live"
    readonly property MockFixtures fixtures: MockFixtures {}
    readonly property bool runtimeConnected: mode !== "mock" && !!bridge && bridge.connected === true
    readonly property var runtime: ({
        source: mode === "mock" ? "mock" : "live", connected: runtimeConnected,
        actionable: runtimeConnected,
        label: mode === "mock" ? "Mock · runtime not used" : runtimeConnected ? "Runtime connected" : "Runtime disconnected"
    })
    // Qt 6.4 exposes typed sequences (notably QStringList) as array-like objects.
    // Accept bounded sequences explicitly; strings and malformed data are not lists.
    function sequence(value) {
        if (!value || typeof value !== "object" || !Number.isInteger(value.length) || value.length < 0 || value.length > 65536) return []
        return Array.prototype.slice.call(value)
    }
    readonly property var devices: {
        if (mode === "mock") return fixtures.devices
        if (!runtimeConnected) return []
        const eligible = calibrationProvider ? sequence(calibrationProvider.devices) : []
        return sequence(bridge.devices).filter(function(device) {
            return device && typeof device.id === "string" && device.id.length > 0 && typeof device.name === "string"
        }).map(function(device) {
            const capabilities = sequence(device.capabilities).filter(function(capability) {
                return typeof capability === "string"
            })
            // Eligibility is validated by CalibrationController against the complete Device Graph.
            // Opening that workflow is permission to inspect/attempt, never proof of readiness.
            const calibrate = eligible.some(function(candidate) { return candidate && candidate.id === device.id })
            return { id: device.id, name: device.name, source: "live", synthetic: false,
                discovered: true, connected: null, availability: "unknown", readiness: "unknown",
                actionable: calibrate, actions: { calibration: calibrate },
                status: "Discovered by runtime · availability and readiness unknown",
                capabilities: capabilities,
                capabilityLabel: capabilities.join(" · ") || "No capabilities advertised" }
        })
    }
    // Hybrid fixtures stay separate even when real devices are available.
    readonly property var demoDevices: mode === "hybrid" ? fixtures.devices : []
    readonly property var routes: [
        {route: "home", title: "Home", milestone: "UI-M1", description: "Your starting point for acquisition and engineering.", planned: "Welcome, recent projects and activity", detail: "The full Home dashboard arrives in UI-M1."},
        {route: "scan", title: "Scan", milestone: "UI-M3", description: "Acquire and replay data from runtime-owned captures.", planned: "Acquisition workspace and capture review", detail: "Camera previews, scan setup and timeline will adopt the new design in UI-M3."},
        {route: "process", title: "Process", milestone: "UI-M4", description: "Build reproducible processing workflows.", planned: "Recipes, artifact lineage and processing stages", detail: "Registration, fusion, mesh and texture tools depend on supported runtime capabilities."},
        {route: "inspect", title: "Inspect", milestone: "UI-M5", description: "Explore geometry and its measurement evidence.", planned: "Selection, measurements and evidence review", detail: "Measurement results and uncertainty require validated runtime data."},
        {route: "reverse", title: "Reverse", milestone: "UI-M5", description: "Take geometry into an engineering workflow.", planned: "Geometry fitting, sections and CAD handoff", detail: "Fitting and CAD tools are planned; no derived geometry is produced here."},
        {route: "automate", title: "Automate", milestone: "UI-M6", description: "Compose repeatable sequences and batch workflows.", planned: "Sequence editor and execution history", detail: "Motion devices, scheduling and triggers require explicit backend capabilities."},
        {route: "projects", title: "Projects", milestone: "UI-M2", description: "Organize captures, artifacts and their history.", planned: "Project browser and immutable revisions", detail: "The current project and artifacts remain available in the existing acquisition view."},
        {route: "devices", title: "Devices", milestone: "UI-M2", description: "Understand device capabilities and calibration.", planned: "Device details and capability-driven configuration", detail: "The existing seven-stage geometric calibration workflow remains available."},
        {route: "plugins", title: "Plugins", milestone: "UI-M6", description: "Extend your workflow through runtime plugins.", planned: "Plugin inventory and permissions", detail: "Existing plugin status and recovery controls remain in the acquisition view."},
        {route: "settings", title: "Settings", milestone: "UI-M7", description: "Make Studio fit your workflow.", planned: "Preferences, appearance and diagnostics", detail: "M0 uses millimeters and right-handed X right / Y forward / Z up coordinates."}
    ]
    function routeInfo(route) {
        const key = route === "acquisition" ? "scan" : route
        for (let i = 0; i < routes.length; ++i) if (routes[i].route === key) return routes[i]
        return {route: "calibration", title: "Calibration", description: "Geometric calibration · existing workflow", milestone: "Existing", planned: "", detail: ""}
    }
}
