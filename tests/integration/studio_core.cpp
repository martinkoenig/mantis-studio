// CUT-0B: production StudioBridge against the real Core daemon, using the
// accepted isolated X1 fixture. No Store/Runtime implementation linkage.
#include "bridge.hpp"
#include "scan_model.hpp"
#include <QCoreApplication>
#include <QSignalSpy>
#include <QTest>
#include <iostream>
namespace w = mantis::wire::v1;
using namespace mantis;
using namespace std::chrono_literals;
#define CHECK(...)                                                                                           \
    do {                                                                                                     \
        if (!(__VA_ARGS__))                                                                                  \
            throw std::runtime_error("Line " + std::to_string(__LINE__) + ": " #__VA_ARGS__);                \
    } while (false)
class Observed : public StudioBridge {
  public:
    explicit Observed(bool runtime = true) : StudioBridge(nullptr, runtime) {}
    using StudioBridge::applyResult;
    using StudioBridge::collectResult;
};
namespace {
void idle(StudioBridge &b) {
    QSignalSpy changed(&b, &StudioBridge::changed);
    auto deadline = std::chrono::steady_clock::now() + 4s;
    while (b.busy() && std::chrono::steady_clock::now() < deadline)
        changed.wait(50);
    CHECK(!b.busy());
}
w::ProjectedCaptureRequest program(const w::ProjectedDevice &g) {
    w::ProjectedCaptureRequest q;
    q.set_plugin_id(g.plugin_id());
    q.set_parent_id(g.parent_id());
    auto &p = *q.mutable_program()->mutable_inline_program();
    p.set_type("org.mantis.AcquisitionProgram");
    p.set_schema_version(1);
    p.mutable_identity()->set_id("cut-0b-camera-ownership");
    p.set_repetitions(1);
    auto *step = p.add_steps();
    step->set_index(0);
    step->set_label("Synthetic OFF / blocked capture");
    step->set_evidence_requirement(w::PROJECTED_COMMANDED_ONLY);
    step->set_max_duration_ns(2'000'000'000);
    step->mutable_capture()->set_mode(w::PROJECTED_FREE_RUNNING);
    for (const auto &c : g.components()) {
        if (c.kind() == w::PROJECTED_IMAGE) {
            auto *camera = p.mutable_participants()->add_cameras();
            camera->set_component(c.id());
            camera->set_stream(c.image_source().stream_id());
            camera->set_role(c.role());
            step->mutable_capture()->add_cameras(c.id());
        } else if (c.kind() == w::PROJECTED_EMITTER) {
            p.mutable_participants()->add_emitters(c.id());
            auto *intent = step->add_emitters();
            intent->set_emitter(c.id());
            intent->set_state(w::PROJECTED_OFF);
        } else if (c.kind() == w::PROJECTED_CONTROLLER)
            p.mutable_participants()->add_controllers(c.id());
    }
    auto *b = p.mutable_bounds();
    b->set_max_duration_ns(5'000'000'000);
    b->set_max_on_duration_ns(2'000'000'000);
    b->set_max_step_instances(1);
    b->set_max_commands(32);
    b->set_max_events(128);
    b->set_max_bytes(64 * 1024 * 1024);
    b->set_max_in_flight_captures(1);
    return q;
}
void artifacts(const w::Response &snapshot, const std::string &raw) {
    // Explicit DTO coverage for new Core types and partial/unknown states. These
    // are presentation inputs, not claims that synthetic metadata was persisted.
    Observed b(false);
    StudioResult result;
    result.snapshot = snapshot;
    for (const auto *state : {"OPEN", "RECOVERABLE", "FUTURE_STATE"}) {
        auto *a = result.snapshot->add_artifacts();
        a->set_id(std::string("partial-") + state);
        a->set_type("org.mantis.RawCapture");
        a->set_schema_version(3);
        a->set_state(state);
    }
    auto *semantic = result.snapshot->add_artifacts();
    semantic->set_id("synthetic-semantic-metadata");
    semantic->set_type("org.mantis.LaserObservation");
    semantic->set_schema_version(1);
    semantic->set_state("FINALIZED");
    b.applyResult(result);
    CHECK(b.connected() && b.selectedArtifact().isEmpty());
    CHECK(!result.cloud && !result.artifactAttempted);
    bool found = false;
    for (const auto &value : b.artifacts()) {
        auto a = value.toMap();
        if (a["id"] == QString::fromStdString(raw))
            found = a["type"] == "org.mantis.RawCapture" && a["state"] == "FINALIZED";
        if (a["id"] == "partial-FUTURE_STATE")
            CHECK(a["state"] == "FUTURE_STATE");
        CHECK(!a.contains("canExport") && !a.contains("canReplay"));
    }
    CHECK(found);
    ScanModel scan;
    scan.setBridge(&b);
    CHECK(scan.data()["processingStatus"] == "Not reported");
    CHECK(scan.data()["latestPointCloud"] == "None / unavailable");
    CHECK(scan.data()["artifacts"].toList().size() <= b.artifacts().size() - 1);
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        client::Client client;
        auto devices = client.projected_devices();
        CHECK(devices.size() == 1);
        const auto parent = QString::fromStdString(devices[0].parent_id());
        auto q = program(devices[0]);
        CHECK(client.validate_projected(q).accepted());
        auto owner = client.start_projected(q, "cut-0b-owner");
        CHECK(owner.active());
        Observed bridge;
        bridge.applyResult(Observed::collectResult(client));
        CHECK(bridge.connected() && bridge.captureStateKnown() && !bridge.capturing());
        CHECK(!bridge.canStopCapture());       // Projected runs are not camera capture IDs.
        CHECK(bridge.canStartCapture(parent)); // Capability permits an intent; daemon owns reservation.
        bridge.startCapture(parent);
        idle(bridge);
        CHECK(bridge.connected() && !bridge.capturing() && !bridge.canStopCapture());
        bool busy = false;
        for (const auto &v : bridge.errorDetails()) {
            auto e = v.toMap();
            busy |= e["phase"] == "operation" && e["code"].toUInt() == static_cast<unsigned>(Status::busy);
        }
        CHECK(busy);
        bridge.stopCapture(); // Cannot stop or impersonate the projected owner.
        auto unchanged = client.projected_status(owner.id());
        CHECK(unchanged.active() && unchanged.run_id() == owner.run_id() &&
              unchanged.generation_id() == owner.generation_id() && unchanged.committed_bundles() == 0);
        client.cancel_projected(owner.id(), owner.run_id(), owner.generation_id());
        auto deadline = std::chrono::steady_clock::now() + 4s;
        do {
            owner = client.projected_status(owner.id());
            if (owner.cleanup_resolved() && owner.storage_state() == w::PROJECTED_STORAGE_FINALIZED)
                break;
            QTest::qWait(2);
        } while (std::chrono::steady_clock::now() < deadline);
        CHECK(owner.state() == w::PROJECTED_CANCELLED && owner.reason() == w::PROJECTED_USER_CANCEL &&
              owner.storage_state() == w::PROJECTED_STORAGE_FINALIZED);
        // Selecting schema 3 never reinterprets a bundle as a PointCloud or grants export authority.
        auto selected = Observed::collectResult(client, {}, owner.raw_artifact_id());
        CHECK(selected.snapshot && !selected.cloud && selected.artifactAttempted);
        CHECK(selected.issues.size() == 1 && selected.issues[0].cause->code == Status::invalid_argument);
        artifacts(*selected.snapshot, owner.raw_artifact_id());
        client.wait(client.replay(owner.raw_artifact_id(), false, true));
        // After owner cleanup, the unchanged camera-only workflow succeeds through the real bridge.
        idle(bridge);
        bridge.refresh();
        idle(bridge);
        CHECK(bridge.canStartCapture(parent));
        bridge.startCapture(parent);
        idle(bridge);
        CHECK(bridge.capturing() && bridge.canStopCapture() && !bridge.canStartCapture(parent));
        bridge.stopCapture();
        idle(bridge);
        CHECK(!bridge.capturing() && !bridge.canStopCapture());
        w::Request shutdown;
        shutdown.mutable_shutdown();
        client.call(shutdown);
        std::cout << "CUT-0B real Core/Bridge projected reservation, cancellation, schema-3 replay and "
                     "camera lifecycle PASS\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
