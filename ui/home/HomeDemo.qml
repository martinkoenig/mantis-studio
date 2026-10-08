import QtQuick
QtObject {
    objectName: "homeDemoFixture"
    // Illustrative presentation only. No command IDs, client or runtime capabilities.
    readonly property var projects: [
        {name: "Housing study", description: "Illustrative project", variant: 0},
        {name: "Rotor study", description: "Illustrative project", variant: 1},
        {name: "Bracket study", description: "Illustrative project", variant: 2},
        {name: "Cover study", description: "Illustrative project", variant: 3}
    ]
    readonly property var data: ({
        source: "mock", confirmed: false, hasSnapshot: false,
        freshness: "Demo / Mock · illustrative content · runtime not used",
        projectName: "Housing study", project: "Demo project · no runtime path",
        devicesCount: 1, devicesSampled: false, devicesAvailable: true, devicesSummary: "Demo / Mock · illustrative only",
        devices: [{id: "", name: "Example composite scanner", plugin: "Illustrative device", state: "Demo / Mock · no hardware connected", detail: "No runtime capabilities · readiness unknown", source: "mock"}],
        jobsCount: 4, jobsSampled: false, jobsAvailable: true, jobsSummary: "Demo / Mock · illustrative only",
        jobs: [
            {id: "", name: "Surface processing · example", state: "Running", progressKnown: true, progress: 0.64, progressText: "64%", detail: "Illustrative progress", source: "mock"},
            {id: "", name: "Texture preparation · example", state: "Queued", progressKnown: false, progress: 0, progressText: "Progress unavailable", detail: "", source: "mock"},
            {id: "", name: "Export · example", state: "Failed", progressKnown: false, progress: 0, progressText: "Progress unavailable", detail: "Illustrative failure · destination unavailable", source: "mock"},
            {id: "", name: "Replay · example", state: "Cancelled", progressKnown: false, progress: 0, progressText: "Progress unavailable", detail: "Illustrative cancellation", source: "mock"}
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
