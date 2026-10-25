#include "../../src/services/projected_calibration.hpp"
#include "../contract/projected-light/fixture.h"
#include "../fixtures/calibration_api.hpp"
#include <fstream>
#include <iostream>
#include <mantis/plugin_runtime.hpp>
#include <mantis/replay.hpp>
#include <mantis/service_adapter.hpp>
#include <thread>
using namespace mantis;
using namespace std::chrono_literals;
namespace d = data;
namespace ca = calibration::artifacts;
#define CHECK(...)                                                                                           \
    do {                                                                                                     \
        if (!(__VA_ARGS__))                                                                                  \
            throw std::runtime_error("Line " + std::to_string(__LINE__) + ": " #__VA_ARGS__);                \
    } while (false)
template <class T> T get(Result<T> result) {
    if (!result)
        throw Failure(result.error());
    return std::move(*result);
}
services::ProjectedCaptureRequest request(std::string role = "left") {
    services::ProjectedCaptureRequest q;
    q.plugin_id = "org.example.projected-contract";
    q.parent = {"parent-alpha"};
    d::AcquisitionProgram p;
    p.identity = {{{"calibration-program"}}, d::Unknown{}, d::Unavailable{}};
    p.participants.cameras = {{{{"camera-alpha"}}, {{"image-stream"}}, std::move(role)}};
    p.participants.emitters = {{{"emitter-alpha"}}};
    p.participants.controllers = {{{"controller-alpha"}}};
    d::AcquisitionStep s;
    s.index = 17;
    s.label = "OFF frame";
    s.emitters = {{{{"emitter-alpha"}}, d::EmitterState::off}};
    s.capture.mode = d::CaptureMode::free_running;
    s.capture.cameras = {{{"camera-alpha"}}};
    s.evidence_requirement = d::EvidenceRequirement::commanded_only;
    s.max_duration = 100ms;
    p.steps = {s};
    p.repetitions = 1;
    p.bounds = {2s, 1s, 100, 100, 100, 64 * 1024 * 1024, 1};
    q.program = p;
    return q;
}
services::ProjectedCaptureInfo finish(services::Runtime &r, Id id) {
    auto end = std::chrono::steady_clock::now() + 8s;
    do {
        auto s = r.projected_status(id);
        if (s.storage_state == artifact::ArtifactState::finalized) {
            CHECK(s.run.cleanup_resolved);
            CHECK(s.committed == s.run.queue.produced);
            return s;
        }
        std::this_thread::sleep_for(2ms);
    } while (std::chrono::steady_clock::now() < end);
    throw std::runtime_error("Projected calibration finish timeout");
}
bool same(const calibration::Reference &a, const calibration::Reference &b) {
    return a.id == b.id && a.schema_version == b.schema_version && a.revision == b.revision;
}
void exact(const d::Evidence<d::ExactCalibrationReference> &e, const artifact::ActiveCalibration &s) {
    CHECK(e.get());
    CHECK(same(e.get()->calibration, s.reference));
    auto c = e.get()->content.get();
    CHECK(c);
    CHECK(c->id == s.artifact.id && c->revision == s.reference.revision);
    CHECK(c->type == schema::DataTypeId("org.mantis.RigCalibration", 1));
    CHECK(c->hash.get() && *c->hash.get() == s.artifact.hash);
}
std::string canonical(const d::AcquisitionBundle &b) {
    std::ostringstream out;
    d::write_bundle(out, b);
    return out.str();
}
void rejected(services::Runtime &runtime, const TestProjectedControl &control,
              const services::ProjectedCaptureRequest &q, std::string_view diagnostic) {
    auto prepares = control.prepares(), starts = control.starts(), opens = control.opens();
    auto artifacts = runtime.artifacts().size();
    auto validation = runtime.validate_projected(q);
    CHECK(!validation.accepted && validation.host_error);
    CHECK(!validation.executor_error && !validation.executor_validation);
    CHECK(validation.host_error->code == Status::incompatible);
    CHECK(validation.host_error->message.find(diagnostic) != std::string::npos);
    CHECK(control.prepares() == prepares && control.starts() == starts && control.opens() == opens);
    CHECK(runtime.artifacts().size() == artifacts);
    try {
        runtime.start_projected(q, Id::random().value);
        throw std::runtime_error("Expected calibration rejection");
    } catch (const Failure &e) {
        CHECK(e.error.code == Status::incompatible);
        CHECK(e.error.message == validation.host_error->message);
    }
    CHECK(control.prepares() == prepares && control.starts() == starts && control.opens() == opens);
    CHECK(runtime.artifacts().size() == artifacts);
}
device::ProjectedValidation validate(services::Runtime &runtime, const TestProjectedControl &control,
                                     const services::ProjectedCaptureRequest &q) {
    const auto prepares = control.prepares(), starts = control.starts(), aborts = control.aborts();
    const auto artifacts = runtime.artifacts().size();
    auto result = runtime.validate_projected(q);
    CHECK(control.prepares() == prepares && control.starts() == starts && control.aborts() == aborts);
    CHECK(runtime.artifacts().size() == artifacts);
    return result;
}
void source_consistency(artifact::Store &store, const device::ProjectedGraph &graph,
                        const d::AcquisitionProgram &program, const d::AcquisitionBundle &source) {
    const d::ExactCalibrationReference rig{
        {{"source.rig"}, 1, 7},
        d::ContentReference{
            {"source-artifact-A"}, {"org.mantis.RigCalibration", 1}, Hash{"sha256", "AAAA"}, 7}};
    auto exact_source = source;
    exact_source.evidence.rig_calibration = rig;
    exact_source.evidence.frames[0].rig_calibration = rig;
    CHECK(d::validate(exact_source));
    {
        services::ProjectedCalibrationBinding binding(store, graph, program);
        binding.stamp(exact_source);
    }
    for (int difference : {0, 1, 2, 3, 4, 5}) {
        auto conflict = exact_source;
        auto changed = rig;
        auto content = *rig.content.get();
        switch (difference) {
        case 0:
            content.id = {"source-artifact-B"};
            break;
        case 1:
            content.hash = Hash{"sha256", "BBBB"};
            break;
        case 2:
            content.revision = 8;
            break;
        case 3:
            content.type = {"org.mantis.RigCalibration", 2};
            break;
        case 4:
            content.hash = d::Unknown{};
            break;
        case 5:
            content.hash = d::Unavailable{};
            break;
        }
        changed.content = content;
        conflict.evidence.frames[0].rig_calibration = changed;
        services::ProjectedCalibrationBinding binding(store, graph, program);
        bool rejected{};
        try {
            binding.stamp(conflict);
        } catch (const Failure &e) {
            rejected = e.error.code == Status::incompatible;
        }
        CHECK(rejected);
    }
    // Missing exact content does not assert a different immutable identity.
    for (auto presence : {d::Presence::unknown, d::Presence::unavailable}) {
        auto missing = exact_source;
        auto reference = rig;
        reference.content = presence == d::Presence::unknown
                                ? d::Evidence<d::ContentReference>(d::Unknown{})
                                : d::Evidence<d::ContentReference>(d::Unavailable{});
        missing.evidence.frames[0].rig_calibration = reference;
        CHECK(d::validate(missing));
        services::ProjectedCalibrationBinding binding(store, graph, program);
        CHECK(canonical(binding.stamp(missing)) == canonical(missing));
        auto fields = binding.source_provenance(missing.key.sequence);
        CHECK(fields.at("source_camera_0_rig_calibration_content_presence") ==
              (presence == d::Presence::unknown ? "unknown" : "unavailable"));
    }
    auto two = program;
    two.participants.cameras.push_back({{{"camera-beta"}}, {{"second-stream"}}, "right"});
    auto cameras = source;
    cameras.evidence.participants = two.participants;
    cameras.frameset.reset();
    cameras.evidence.rig_calibration = d::Unknown{};
    cameras.evidence.frames[0].rig_calibration = rig;
    auto second = cameras.evidence.frames[0];
    second.frame.camera = {{"camera-beta"}};
    second.frame.stream.id = {{"second-stream"}};
    second.camera_role = "right";
    auto other = rig;
    auto content = *rig.content.get();
    content.id = {"source-artifact-B"};
    other.content = content;
    second.rig_calibration = other;
    cameras.evidence.frames.push_back(second);
    for (auto &emitter : cameras.evidence.emitters) {
        auto effective = emitter.exposure_effective[0];
        effective.frame = second.frame;
        emitter.exposure_effective.push_back(effective);
    }
    CHECK(d::validate(cameras));
    services::ProjectedCalibrationBinding binding(store, graph, two);
    bool rejected{};
    try {
        binding.stamp(cameras);
    } catch (const Failure &e) {
        rejected = e.error.code == Status::incompatible;
    }
    CHECK(rejected);
    cameras.evidence.frames[1].rig_calibration = rig;
    for (size_t i = 0; i < cameras.evidence.frames.size(); ++i) {
        const auto id = "intrinsics-" + std::to_string(i);
        cameras.evidence.frames[i].camera_calibration = d::ExactCalibrationReference{
            {{id}, 1, 1},
            d::ContentReference{
                {id + "-artifact"}, {"org.mantis.CameraCalibration", 1}, Hash{"sha256", "abcd"}, 1}};
    }
    CHECK(d::validate(cameras));
    services::ProjectedCalibrationBinding matching(store, graph, two);
    CHECK(canonical(matching.stamp(cameras)) == canonical(cameras));
}
int main(int argc, char **argv) {
    try {
        CHECK(argc == 4);
        plugins::Loaded plugin(argv[1]);
        auto control =
            static_cast<const TestProjectedControl *>(plugin.api()->query_interface(TEST_PROJECTED_CONTROL));
        CHECK(control);
        const auto root = std::filesystem::path(argv[3]) / ("projected-calibration-" + Id::random().value);
        control->image_geometry("left", "camera.left", 1280, 960);
        control->source_calibration("source.rig", 7);
        control->fault(TEST_SERVICE_CAPTURE);
        {
            services::Runtime runtime({argv[2], {}, root, {}, {"org.example.projected-contract"}});
            auto store = runtime.project_store();
            CHECK(validate(runtime, *control, request()).accepted);
            auto no_active = finish(runtime, runtime.start_projected(request(), "unbound").id);
            auto source = store->bundle(no_active.raw_artifact);
            CHECK(source.frameset);
            CHECK(source.frameset->header.calibration.id.value == "source.rig");
            CHECK(source.frameset->frames[0]->header.calibration.revision == 7);
            CHECK(source.evidence.rig_calibration.presence() == d::Presence::unknown);
            CHECK(source.evidence.frames[0].rig_calibration.presence() == d::Presence::unknown);
            CHECK(source.evidence.frames[0].original_calibration.presence() == d::Presence::unavailable);
            CHECK(store->get(no_active.raw_artifact).provenance.calibration.id.value.empty());
            CHECK(
                !store->get(no_active.raw_artifact).provenance.parameters.contains("active_calibration_id"));
            auto seed = m6fixture::seed(*store);
            calibration::MonoSolveConfig mono;
            mono.heldout_per_camera = 3;
            auto left = get(calibration::solve_camera_intrinsics(seed.dataset.value.dataset, "left", mono));
            auto right = get(calibration::solve_camera_intrinsics(seed.dataset.value.dataset, "right", mono));
            ca::SolverImplementation implementation{std::string(calibration::solver_opencv_version())};
            auto l = get(ca::create_camera_calibration(*store, left, seed.dataset.reference(),
                                                       seed.charuco.reference(), implementation));
            auto r = get(ca::create_camera_calibration(*store, right, seed.dataset.reference(),
                                                       seed.charuco.reference(), implementation));
            calibration::StereoSolveConfig stereo;
            stereo.left_role = "left";
            stereo.right_role = "right";
            stereo.heldout_pairs = 3;
            stereo.rig_frame = {{"rig.fixture"}, "millimeters"};
            auto solved = get(calibration::solve_stereo_rig(seed.dataset.value.dataset, left, right, stereo));
            auto rev1 = get(ca::create_rig_calibration(*store, solved, seed.dataset.reference(),
                                                       seed.charuco.reference(), l.reference(), r.reference(),
                                                       implementation));
            CHECK(ca::activate_rig_calibration(*store, {"parent-alpha"}, rev1.descriptor.id));
            const auto selected = *store->active_calibration({"parent-alpha"});
            CHECK(validate(runtime, *control, request()).accepted);
            auto malformed = request();
            std::get<d::AcquisitionProgram>(malformed.program).steps.clear();
            auto invalid = validate(runtime, *control, malformed);
            CHECK(!invalid.accepted && invalid.host_error && !invalid.executor_error);
            control->fault(TEST_SERVICE_VALIDATE_FAILURE);
            auto executor_failure = validate(runtime, *control, request());
            CHECK(!executor_failure.accepted && !executor_failure.host_error &&
                  executor_failure.executor_error);
            control->fault(TEST_SERVICE_CAPTURE);
            // Corrupt our test artifact bytes, then restore exactly; pure validation must fail closed.
            const auto path = store->object_path(selected.artifact.id);
            std::ifstream input(path, std::ios::binary);
            std::string bytes((std::istreambuf_iterator<char>(input)), {});
            input.close();
            auto corrupt = bytes;
            corrupt.back() ^= 1;
            {
                std::ofstream out(path, std::ios::binary | std::ios::trunc);
                out.write(corrupt.data(), corrupt.size());
            }
            const auto opens = control->opens();
            auto bad_artifact = validate(runtime, *control, request());
            CHECK(!bad_artifact.accepted && bad_artifact.host_error && !bad_artifact.executor_error);
            CHECK(bad_artifact.host_error->code == Status::corrupt && control->opens() == opens);
            {
                std::ofstream out(path, std::ios::binary | std::ios::trunc);
                out.write(bytes.data(), bytes.size());
            }
            CHECK(store->active_calibration({"parent-alpha"})->artifact.hash == selected.artifact.hash);

            // One participating camera from a two-camera rig; emitter/controller children are excluded.
            services::ProjectedCalibrationBinding binding(*store, runtime.projected_devices()[0].graph,
                                                          std::get<d::AcquisitionProgram>(request().program));
            auto bound = binding.stamp(source);
            exact(bound.evidence.rig_calibration, selected);
            exact(bound.evidence.frames[0].rig_calibration, selected);
            CHECK(same(bound.frameset->header.calibration, selected.reference));
            CHECK(same(bound.frameset->frames[0]->header.calibration, selected.reference));
            const auto &old_pixels = source.frameset->frames[0]->attributes[0].buffer;
            const auto &new_pixels = bound.frameset->frames[0]->attributes[0].buffer;
            CHECK(old_pixels.identity() == new_pixels.identity());
            CHECK(get(old_pixels.map_read()).data() == get(new_pixels.map_read()).data());
            // Restore only the deliberate binding fields: every native fact must then encode identically.
            auto native = bound;
            native.frameset = source.frameset;
            native.evidence.rig_calibration = source.evidence.rig_calibration;
            native.evidence.frames[0].rig_calibration = source.evidence.frames[0].rig_calibration;
            CHECK(canonical(native) == canonical(source));
            // Late frame evidence without a FrameSet retains its original exact source identity.
            services::ProjectedCalibrationBinding late_binding(
                *store, runtime.projected_devices()[0].graph,
                std::get<d::AcquisitionProgram>(request().program));
            auto late = source;
            late.frameset.reset();
            CHECK(d::validate(late));
            late_binding.stamp(late);
            auto initial_audit = late_binding.source_provenance(late.key.sequence);
            CHECK(initial_audit.at("source_camera_0_rig_calibration_presence") == "unknown");
            late.key.sequence.value++;
            const d::ExactCalibrationReference late_reference{
                {{"source.rig"}, 1, 7},
                d::ContentReference{
                    {"late-source-artifact"}, {"org.mantis.RigCalibration", 1}, Hash{"sha256", "abcd"}, 7}};
            late.evidence.frames[0].rig_calibration = late_reference;
            auto late_bound = late_binding.stamp(late);
            exact(late_bound.evidence.frames[0].rig_calibration, selected);
            auto late_audit = late_binding.source_provenance(late.key.sequence);
            CHECK(late_audit.at("source_camera_0_established_rig_calibration_artifact_id") ==
                  "late-source-artifact");
            late.evidence.frames[0].rig_calibration =
                d::ExactCalibrationReference{{{"source.rig"}, 1, 8}, d::Unknown{}};
            bool changed{};
            try {
                late_binding.stamp(late);
            } catch (const Failure &e) {
                changed = e.error.code == Status::incompatible;
            }
            CHECK(changed);
            auto captured = finish(runtime, runtime.start_projected(request(), "bound").id);
            CHECK(captured.run.terminal.executor_terminal);
            exact(captured.run.terminal.executor_terminal->evidence.rig_calibration, selected);
            auto recorded = store->bundle(captured.raw_artifact);
            exact(recorded.evidence.rig_calibration, selected);
            exact(recorded.evidence.frames[0].rig_calibration, selected);
            CHECK(same(recorded.frameset->header.calibration, selected.reference));
            CHECK(same(recorded.frameset->frames[0]->header.calibration, selected.reference));
            auto a = store->get(captured.raw_artifact);
            CHECK(a.provenance.calibration.id == selected.reference.id);
            CHECK(std::find(a.provenance.inputs.begin(), a.provenance.inputs.end(), selected.artifact.id) !=
                  a.provenance.inputs.end());
            CHECK(a.provenance.parameters.at("active_rig_artifact_hash") == selected.artifact.hash.hex);
            CHECK(a.provenance.parameters.at("source_device_calibration_id") == "source.rig");
            CHECK(a.provenance.parameters.at("source_device_calibration_revision") == "7");
            CHECK(a.provenance.parameters.at("source_frameset_rig_calibration_presence") == "unknown");
            control->image_geometry("left", "wrong-camera", 1280, 960);
            rejected(runtime, *control, request(), "identity");
            control->image_geometry("wrong-role", "camera.left", 1280, 960);
            rejected(runtime, *control, request("wrong-role"), "role");
            control->image_geometry("left", "camera.left", 1279, 960);
            rejected(runtime, *control, request(), "geometry");
            control->image_geometry("left", "camera.left", 1280, 959);
            rejected(runtime, *control, request(), "geometry");
            control->image_geometry("left", "camera.left", 1280, 960);
            for (auto mode : {TEST_SERVICE_CALIBRATION_MISMATCH, TEST_SERVICE_CALIBRATION_CHANGE}) {
                control->fault(mode);
                auto failed = finish(runtime, runtime.start_projected(request(), Id::random().value).id);
                CHECK(failed.run.state == device::ProjectedState::failed &&
                      failed.run.terminal.initiating_error);
                CHECK(failed.run.terminal.initiating_error->message.find(
                          mode == TEST_SERVICE_CALIBRATION_MISMATCH
                              ? "references disagree"
                              : "calibration changed") != std::string::npos);
                CHECK(failed.committed == (mode == TEST_SERVICE_CALIBRATION_MISMATCH ? 0 : 1));
            }
            control->fault(TEST_SERVICE_CALIBRATION_EXACT);
            auto exact_source = finish(runtime, runtime.start_projected(request(), "source-content").id);
            control->fault(TEST_SERVICE_CALIBRATION_CONTENT_MISMATCH);
            auto inconsistent = finish(runtime, runtime.start_projected(request(), "source-conflict").id);
            CHECK(inconsistent.run.state == device::ProjectedState::failed);
            CHECK(inconsistent.committed == 0 && inconsistent.run.queue.produced == 0);
            CHECK(inconsistent.run.terminal.initiating_error);
            CHECK(inconsistent.run.terminal.initiating_error->message.find("rig calibration disagree") !=
                  std::string::npos);
            control->fault(TEST_SERVICE_CALIBRATION_EXACT);
            auto original = store->get(exact_source.raw_artifact).provenance.parameters;
            CHECK(original.at("source_established_rig_calibration_artifact_id") == "source-rig-artifact");
            CHECK(original.at("source_established_rig_calibration_hash") == "abcd");
            CHECK(original.at("source_camera_0_rig_calibration_hash") == "abcd");
            exact(store->bundle(exact_source.raw_artifact).evidence.rig_calibration, selected);
            auto rev2 = get(ca::create_rig_calibration(*store, solved, seed.dataset.reference(),
                                                       seed.charuco.reference(), l.reference(), r.reference(),
                                                       implementation, rev1.value.revision.id));
            CHECK(ca::activate_rig_calibration(*store, {"parent-alpha"}, rev2.descriptor.id));
            auto opens_before_retry = control->opens();
            CHECK(runtime.start_projected(request(), "bound").raw_artifact == captured.raw_artifact);
            CHECK(control->opens() == opens_before_retry);
            for (bool clear : {false, true}) {
                if (clear)
                    store->clear_active_calibration({"parent-alpha"});
                device::BundleReplay replay(store, captured.raw_artifact, false);
                auto replayed = get(replay.next(100));
                CHECK(replayed);
                CHECK(canonical(*replayed) == canonical(recorded));
                exact(replayed->evidence.rig_calibration, selected);
                auto unbound = store->bundle(no_active.raw_artifact);
                CHECK(canonical(unbound) == canonical(source));
            }
            source_consistency(*store, runtime.projected_devices()[0].graph,
                               std::get<d::AcquisitionProgram>(request().program), source);
            // An explicit unavailable source stays unavailable when no active project binding exists.
            auto unavailable = source;
            unavailable.evidence.rig_calibration = d::Unavailable{};
            services::ProjectedCalibrationBinding absent(*store, runtime.projected_devices()[0].graph,
                                                         std::get<d::AcquisitionProgram>(request().program));
            CHECK(canonical(absent.stamp(unavailable)) == canonical(unavailable));
            // Later source audit initialization is allowed after evidence-only records; overwrites aren't.
            auto header = store->capture_header(captured.raw_artifact);
            auto raw = store->begin_projected_capture(header);
            auto control_bundle = recorded;
            control_bundle.frameset.reset();
            control_bundle.evidence.frames.clear();
            for (auto &emitter : control_bundle.evidence.emitters)
                emitter.exposure_effective.clear();
            control_bundle.evidence.frameset = d::Unavailable{};
            control_bundle.evidence.disposition = d::AcquisitionDisposition::control_only;
            store->append_bundle(raw, control_bundle);
            store->initialize_projected_source_provenance(raw,
                                                          {{"source_device_calibration_id", "source.rig"}});
            bool duplicate{};
            try {
                store->initialize_projected_source_provenance(raw,
                                                              {{"source_device_calibration_id", "other"}});
            } catch (const Failure &e) {
                duplicate = e.error.code == Status::invalid_argument;
            }
            CHECK(duplicate);
            store->abandon(raw);
        }
        control->image_geometry("imaging", "physical-camera-alpha", 2, 2);
        control->source_calibration("", 0);
        control->fault(TEST_NORMAL);
        std::filesystem::remove_all(root);
        std::cout << "Projected calibration snapshot, source provenance, pre-publication zero-copy binding "
                     "and historical replay passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
