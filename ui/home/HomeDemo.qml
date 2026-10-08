import QtQuick
QtObject {
    objectName: "homeDemoFixture"
    // Illustrative presentation only. No command IDs, client or runtime capabilities.
    readonly property var projects: [
        {name: "X1 Housing", description: "Sample · last opened 2 hours ago", stats: "3 scans  ·  4.2 M points", shape: "housing"},
        {name: "Turbine Rotor", description: "Sample · last opened 1 day ago", stats: "5 scans  ·  8.1 M points", shape: "rotor"},
        {name: "Mounting Bracket", description: "Sample · last opened 3 days ago", stats: "2 scans  ·  1.8 M points", shape: "bracket"},
        {name: "Gearbox Cover", description: "Sample · last opened 1 week ago", stats: "6 scans  ·  12.4 M points", shape: "cover"}
    ]
    readonly property var data: ({
        source: "mock", confirmed: false, hasSnapshot: false,
        freshness: "Illustrative workspace · sample projects and activity · runtime not used",
        projectName: "Housing study", project: "Demo project · no runtime path",
        devicesCount: 1, devicesSampled: false, devicesAvailable: true, devicesSummary: "Demo / Mock · illustrative only",
        devices: [{id: "", name: "Example composite scanner", plugin: "Illustrative device", state: "Demo / Mock · no hardware connected", detail: "No runtime capabilities · readiness unknown", source: "mock"}],
        jobsCount: 4, jobsSampled: false, jobsAvailable: true, jobsSummary: "Demo / Mock · illustrative only",
        jobs: [
            {id: "", name: "Meshing · Housing", state: "Running", progressKnown: true, progress: 0.64, progressText: "64%", detail: "Illustrative progress", source: "mock"},
            {id: "", name: "Texture preparation", state: "Queued", progressKnown: false, progress: 0, progressText: "Progress unavailable", detail: "", source: "mock"},
            {id: "", name: "Export · housing.ply", state: "Failed", progressKnown: false, progress: 0, progressText: "Progress unavailable", detail: "Destination unavailable", source: "mock"},
            {id: "", name: "Replay · Bracket", state: "Cancelled", progressKnown: false, progress: 0, progressText: "Progress unavailable", detail: "Illustrative cancellation", source: "mock"}
        ],
        activity: [
            {id: "", source: "mock", name: "High Quality Scan · Housing", type: "Scan", state: "Completed", date: "2026-10-05 14:23", size: "2.1 GB"},
            {id: "", source: "mock", name: "Mesh · Housing (High Detail)", type: "Mesh", state: "Completed", date: "2026-10-05 14:45", size: "480 MB"},
            {id: "", source: "mock", name: "Texture · Housing", type: "Texture", state: "Completed", date: "2026-10-05 15:02", size: "320 MB"},
            {id: "", source: "mock", name: "Export · housing.ply", type: "Export", state: "Completed", date: "2026-10-05 15:10", size: "12 MB"},
            {id: "", source: "mock", name: "Scan · Bracket", type: "Scan", state: "Processing", date: "2026-10-05 15:28", size: "1.4 GB"}
        ],
        artifactsCount: 4, artifactsSampled: false, artifactsAvailable: true, artifactsSummary: "Demo / Mock · illustrative only",
        artifacts: [
            {id: "", name: "org.mantis.RawCapture", state: "FINALIZED", detail: "Demo / Mock · illustrative source", source: "mock"},
            {id: "", name: "org.mantis.PointCloud", state: "FINALIZED", detail: "Demo / Mock · illustrative result", source: "mock"},
            {id: "", name: "org.mantis.Mesh", state: "FINALIZED", detail: "Demo / Mock · illustrative result", source: "mock"},
            {id: "", name: "org.mantis.RawCapture", state: "RECOVERABLE", detail: "Demo / Mock · illustrative recovery", source: "mock"}
        ],
        eventsCount: 2, eventsSampled: false, eventsAvailable: true, eventsSummary: "Demo / Mock · illustrative only",
        events: [
            {id: "", sequence: "—", name: "Demo / Mock", state: "Information", detail: "Example processing result available for review", source: "mock"},
            {id: "", sequence: "—", name: "Demo / Mock", state: "Information", detail: "Example project opened · illustrative activity", source: "mock"}
        ], issues: []
    })
}
