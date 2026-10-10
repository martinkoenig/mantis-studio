import QtQuick
QtObject {
    readonly property var data: ({
        source: "mock", confirmed: false, hasSnapshot: false, projectEpoch: 0,
        freshness: "Illustrative study · no hardware or runtime data", project: "Local demo / housing study",
        captureStatus: "Demo only", captureStatusText: "Illustrative content · capture is not running", captureActive: false,
        readiness: "Unknown · illustrative descriptor", previewStatus: "Illustrations only · no camera stream",
        selectedArtifact: "None · illustrative study", latestPointCloud: "No runtime artifact", runtimeError: "", issues: [], diagnostics: [],
        devices: [{id: "illustrative-scanner", name: "Stereo scanner study", source: "mock", actionable: false, state: "Illustrative · hardware unavailable", detail: "Local presentation sample"}],
        artifacts: [{id: "illustrative-housing", label: "Mechanical housing study", name: "Study", type: "Illustration", state: "Demo / Mock", source: "mock", actionable: false, detail: "Unsequenced illustration · not a recorded capture"}],
        jobs: [], commandsAllowed: false, canOpenClassicAcquisition: false
    })
}
