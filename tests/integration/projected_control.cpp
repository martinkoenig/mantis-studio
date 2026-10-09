#include "../../apps/cli/projected_commands.hpp"
#include "../../src/protocol/projected_adapter.hpp"
#include "../contract/projected-light/fixture.h"
#include <fstream>
#include <future>
#include <iostream>
#include <mantis/artifact_store.hpp>
#include <mantis/client.hpp>
#include <mantis/plugin_runtime.hpp>
#include <thread>
using namespace mantis;
using namespace std::chrono_literals;
namespace w = mantis::wire::v1;
namespace d = mantis::data;
namespace {
#define CHECK(...)                                                                                           \
    do {                                                                                                     \
        if (!(__VA_ARGS__))                                                                                  \
            throw std::runtime_error("Line " + std::to_string(__LINE__) + ": " #__VA_ARGS__);                \
    } while (false)
template <class F> void rejects(F f, Status expected) {
    try {
        f();
    } catch (const Failure &e) {
        CHECK(e.error.code == expected);
        return;
    }
    throw std::runtime_error("Expected failure");
}
d::AcquisitionProgram program() {
    d::AcquisitionProgram p;
    p.identity = {{{"service-program"}}, d::Unknown{}, d::Unavailable{}};
    p.participants.cameras = {{{{"camera-alpha"}}, {{"image-stream"}}, "imaging"}};
    p.participants.emitters = {{{"emitter-alpha"}}};
    p.participants.controllers = {{{"controller-alpha"}}};
    d::AcquisitionStep s;
    s.index = 17;
    s.label = "OFF";
    s.emitters = {{{{"emitter-alpha"}}, d::EmitterState::off}};
    s.capture.mode = d::CaptureMode::none;
    s.evidence_requirement = d::EvidenceRequirement::commanded_only;
    s.max_duration = 100ms;
    p.steps = {s};
    p.repetitions = 1;
    p.bounds = {2s, 1s, 100, 100, 100, 64 * 1024 * 1024, 1};
    CHECK(d::validate(p));
    return p;
}
w::ProjectedCaptureRequest request() {
    w::ProjectedCaptureRequest q;
    q.set_plugin_id("org.example.projected-contract");
    q.set_parent_id("parent-alpha");
    protocol::write_projected_program(q.mutable_program()->mutable_inline_program(), program());
    q.mutable_config()->set_queue_capacity(1);
    return q;
}
w::Response dispatch(services::Runtime &runtime, w::Request q, std::optional<Status> expected = {}) {
    q.set_protocol_version(1);
    if (q.request_id().empty())
        q.set_request_id(Id::random().value);
    w::Request parsed;
    CHECK(parsed.ParseFromString(q.SerializeAsString()));
    auto result = protocol::dispatch(runtime, parsed);
    w::Response response;
    CHECK(response.ParseFromString(result.SerializeAsString()));
    CHECK(response.request_id() == q.request_id());
    if (expected) {
        if (!response.has_error())
            throw std::runtime_error("Expected error for command " + std::to_string(q.command_case()) +
                                     " request " + q.request_id());
        CHECK(response.has_error());
        CHECK(response.error().code() == static_cast<uint32_t>(*expected));
    } else if (response.has_error())
        throw Failure({static_cast<Status>(response.error().code()), response.error().message(),
                       response.error().component()});
    CHECK(response.ByteSizeLong() < protocol::max_control_bytes);
    return response;
}
services::ProjectedCaptureInfo finish(services::Runtime &runtime, const std::string &id,
                                      bool finalized = true) {
    auto deadline = std::chrono::steady_clock::now() + 5s;
    do {
        auto status = runtime.projected_status({id});
        if (status.run.cleanup_resolved) {
            if (!finalized && status.recording_error &&
                status.storage_state == artifact::ArtifactState::recoverable)
                return status;
            if (finalized && status.storage_state == artifact::ArtifactState::finalized)
                for (const auto &job : runtime.jobs())
                    if (job.id == status.finalization_job && job.state == jobs::State::completed)
                        return status;
        }
        std::this_thread::sleep_for(2ms);
    } while (std::chrono::steady_clock::now() < deadline);
    throw std::runtime_error("Projected finish timeout");
}
w::ProjectedCapture start(services::Runtime &runtime, const std::string &id) {
    w::Request q;
    *q.mutable_projected_start() = request();
    q.set_request_id(id);
    return dispatch(runtime, q).projected_captures(0);
}
struct Probe {
    services::Runtime *runtime;
    bool durable{};
};
struct BlockedRead : memory::Fence {
    mutable std::mutex mutex;
    mutable std::condition_variable changed;
    mutable bool entered{}, released{};
    Result<void> wait(const CancellationToken &token) const override {
        std::unique_lock lock(mutex);
        entered = true;
        changed.notify_all();
        changed.wait(lock, [&] { return released; });
        token.check();
        return {};
    }
    bool waiting() const {
        std::unique_lock lock(mutex);
        return changed.wait_for(lock, 3s, [&] { return entered; });
    }
    void release() const {
        std::lock_guard lock(mutex);
        released = true;
        changed.notify_all();
    }
};
void header_probe(void *context) {
    auto &probe = *static_cast<Probe *>(context);
    auto artifacts = probe.runtime->artifacts();
    CHECK(!artifacts.empty());
    auto a = artifacts.back();
    auto store = probe.runtime->project_store();
    auto header = store->capture_header(a.id);
    CHECK(header.program.steps[0].index == 17);
    CHECK(std::filesystem::exists(store->root() / "objects" / a.id.value / "run.header"));
    CHECK(!std::filesystem::exists(store->root() / "objects" / a.id.value / "run.header.part"));
    probe.durable = true;
}
void tests(services::Runtime &runtime, const TestProjectedControl &control,
           std::atomic<int64_t> &lease_time) {
    auto roundtrip = program();
    roundtrip.identity.hash = Hash{"sha256", "00"};
    roundtrip.identity.content =
        d::ContentReference{{"service-program"}, schema::acquisition_program, d::Unavailable{}, 0};
    roundtrip.steps[0].capture.mode = d::CaptureMode::hardware_trigger;
    roundtrip.steps[0].capture.cameras = {{{"camera-alpha"}}};
    roundtrip.steps[0].capture.trigger =
        d::TriggerIntent{{{"controller-alpha"}}, {{"request-zero"}}, {{{"camera-alpha"}}}};
    roundtrip.steps[0].settle = 0ns;
    auto second = roundtrip.steps[0];
    second.index = 91;
    second.label = "second";
    second.capture.trigger->request = {{"request-one"}};
    roundtrip.steps.push_back(second);
    roundtrip.repetitions = 2;
    w::ProjectedAcquisitionProgram dto;
    protocol::write_projected_program(&dto, roundtrip);
    auto decoded = protocol::read_projected_program(dto);
    if (auto valid = d::validate(decoded); !valid)
        throw Failure(valid.error());
    std::ostringstream a, b;
    d::write_capture_header(a, {roundtrip, {{"r"}}, {{"g"}}, device::recorded_run_config({})});
    d::write_capture_header(b, {decoded, {{"r"}}, {{"g"}}, device::recorded_run_config({})});
    CHECK(a.str() == b.str());
    control.fault(TEST_SERVICE_COMPLETE);
    w::Request q;
    q.mutable_projected_devices_list();
    auto devices = dispatch(runtime, q);
    CHECK(devices.projected_devices_size() == 1);
    auto graph = devices.projected_devices(0);
    CHECK(graph.plugin_id() == "org.example.projected-contract");
    CHECK(graph.parent_id() == "parent-alpha");
    CHECK(graph.components_size() == 4);
    CHECK(graph.components(1).image_source().width() == 2);
    CHECK(graph.components(1).image_source().stream_id() == "image-stream");
    CHECK(graph.limits().max_pending_bundles() == 1);
    CHECK(graph.components(3).controls(0) == "emitter-alpha");
    CHECK(graph.limits().watchdog().presence() == w::PROJECTED_UNAVAILABLE);
    q.Clear();
    q.set_token(std::string(protocol::max_control_bytes, 'x'));
    q.mutable_projected_devices_list();
    dispatch(runtime, q, Status::invalid_argument);
    auto before = runtime.artifacts().size();
    auto prepares = control.prepares(), starts = control.starts(), validation_aborts = control.aborts();
    q.Clear();
    *q.mutable_projected_validate() = request();
    auto validation = dispatch(runtime, q);
    CHECK(validation.projected_validation().accepted());
    q.mutable_projected_validate()->clear_config();
    CHECK(dispatch(runtime, q).projected_validation().accepted());
    CHECK(control.prepares() == prepares);
    CHECK(control.starts() == starts);
    CHECK(control.aborts() == validation_aborts);
    CHECK(runtime.artifacts().size() == before);
    q.mutable_projected_validate()->mutable_program()->mutable_inline_program()->set_repetitions(0);
    CHECK(!dispatch(runtime, q).projected_validation().accepted());
    CHECK(runtime.artifacts().size() == before);
    *q.mutable_projected_validate() = request();
    q.mutable_projected_validate()
        ->mutable_program()
        ->mutable_inline_program()
        ->mutable_participants()
        ->mutable_cameras(0)
        ->set_stream("wrong");
    CHECK(dispatch(runtime, q).projected_validation().has_host_error());
    control.fault(TEST_SERVICE_VALIDATE_FAILURE);
    *q.mutable_projected_validate() = request();
    validation = dispatch(runtime, q);
    CHECK(!validation.projected_validation().accepted());
    CHECK(validation.projected_validation().has_executor_error());
    CHECK(control.aborts() == validation_aborts);
    control.fault(TEST_REJECT_PROGRAM);
    *q.mutable_projected_validate() = request();
    validation = dispatch(runtime, q);
    CHECK(!validation.projected_validation().executor_validation().accepted());
    CHECK(!validation.has_error());
    control.fault(TEST_SERVICE_COMPLETE);
    q.mutable_projected_validate()->set_parent_id("");
    dispatch(runtime, q, Status::invalid_argument);
    *q.mutable_projected_validate() = request();
    q.mutable_projected_validate()
        ->mutable_program()
        ->mutable_inline_program()
        ->mutable_steps(0)
        ->set_required_scope(static_cast<w::ProjectedEvidenceScope>(99));
    dispatch(runtime, q, Status::invalid_argument);
    *q.mutable_projected_validate() = request();
    q.mutable_projected_validate()->mutable_program()->mutable_inline_program()->mutable_steps(0)->set_label(
        std::string(512 * 1024, 'x'));
    dispatch(runtime, q, Status::invalid_argument);
    CHECK(runtime.artifacts().size() == before);
    Probe probe{&runtime};
    control.prepare_probe(header_probe, &probe);
    auto capture = start(runtime, "same-start");
    control.prepare_probe(nullptr, nullptr);
    CHECK(probe.durable);
    auto complete = finish(runtime, capture.id());
    CHECK(complete.run.state == device::ProjectedState::completed);
    CHECK(complete.committed == 2);
    CHECK(complete.run.queue.produced == complete.committed);
    CHECK(complete.evidence.evidence_only == 2);
    CHECK(complete.last_evidence_step->step_index == 17);
    std::vector<std::string> lifecycle;
    for (const auto &e : runtime.events(0))
        if (e.message == capture.id() && e.kind.starts_with("projected."))
            lifecycle.push_back(e.kind);
    CHECK(lifecycle ==
          std::vector<std::string>({"projected.created", "projected.ready", "projected.started",
                                    "projected.completed", "projected.finalizing", "projected.finalized"}));
    auto stored = runtime.project_store()->run_outcome(complete.raw_artifact);
    CHECK(stored->disposition == d::RecordedRunDisposition::completed);
    auto opened = control.opens();
    prepares = control.prepares();
    starts = control.starts();
    auto retry = start(runtime, "same-start");
    CHECK(retry.id() == capture.id());
    CHECK(retry.run_id() == capture.run_id());
    CHECK(control.opens() == opened && control.prepares() == prepares && control.starts() == starts);
    q.Clear();
    *q.mutable_projected_start() = request();
    q.set_request_id("same-start");
    q.mutable_projected_start()->mutable_config()->set_publication_timeout_ms(99);
    dispatch(runtime, q, Status::invalid_argument);
    // Both authoritative executor bundles remain inspectable separately from daemon cleanup.
    CHECK(runtime.project_store()->bundle_summary(complete.raw_artifact).records == 2);
    q.Clear();
    q.mutable_projected_bundle()->set_id(capture.id());
    auto ref = dispatch(runtime, q).data();
    CHECK(ref.format_version() == 3);
    CHECK(ref.transport() == "local-mapped-file");
    CHECK(ref.lease_seconds() == 60);
    CHECK(std::filesystem::exists(ref.locator()));
    runtime.release_preview({ref.lease_id()});
    std::vector<services::PreviewReference> leases;
    for (unsigned i = 0; i < 8; ++i)
        leases.push_back(runtime.projected_bundle({capture.id()}));
    rejects([&] { runtime.projected_bundle({capture.id()}); }, Status::busy);
    lease_time += 61;
    auto fresh = runtime.projected_bundle({capture.id()});
    for (const auto &lease : leases) {
        CHECK(!std::filesystem::exists(lease.path));
        runtime.release_preview(lease.lease);
    }
    runtime.release_preview(fresh.lease);
    opened = control.opens();
    for (bool paced : {false, true}) {
        auto job = runtime.replay_capture(complete.raw_artifact, paced, true);
        auto end = std::chrono::steady_clock::now() + 5s;
        bool success{};
        while (std::chrono::steady_clock::now() < end && !success) {
            for (const auto &j : runtime.jobs())
                if (j.id == job) {
                    CHECK(j.state != jobs::State::failed);
                    success = j.state == jobs::State::completed;
                    if (success)
                        CHECK(j.status.find("passes=2") != std::string::npos);
                }
            std::this_thread::sleep_for(2ms);
        }
        CHECK(success);
        auto latest = runtime.projected_bundle(job);
        runtime.release_preview(latest.lease);
    }
    CHECK(control.opens() == opened);
    // Exact immutable RawCapture program source, independent of active project calibration.
    q.Clear();
    *q.mutable_projected_validate() = request();
    q.mutable_projected_validate()->mutable_program()->set_raw_capture_artifact_id(
        complete.raw_artifact.value);
    CHECK(dispatch(runtime, q).projected_validation().accepted());
    q.Clear();
    *q.mutable_projected_start() = request();
    q.mutable_projected_start()->mutable_program()->set_raw_capture_artifact_id(complete.raw_artifact.value);
    q.set_request_id("raw-program-start");
    auto raw_started = dispatch(runtime, q).projected_captures(0);
    auto raw_capture = finish(runtime, raw_started.id());
    CHECK(runtime.project_store()->get(raw_capture.raw_artifact).provenance.inputs ==
          std::vector<Id>{complete.raw_artifact});
    a.str("");
    b.str("");
    d::write_capture_header(a, runtime.project_store()->capture_header(raw_capture.raw_artifact));
    auto expected_header = runtime.project_store()->capture_header(complete.raw_artifact);
    expected_header.run = raw_capture.run.identity.run;
    expected_header.generation = raw_capture.run.identity.generation;
    d::write_capture_header(b, expected_header);
    CHECK(a.str() == b.str());
    opened = control.opens();
    CHECK(dispatch(runtime, q).projected_captures(0).id() == raw_started.id());
    CHECK(control.opens() == opened);
    *q.mutable_projected_start() = request();
    dispatch(runtime, q, Status::invalid_argument); // Same program, changed source is a changed request.
    q.Clear();
    *q.mutable_projected_start() = request();
    q.set_request_id("host-validation-failure");
    q.mutable_projected_start()
        ->mutable_program()
        ->mutable_inline_program()
        ->mutable_participants()
        ->mutable_cameras(0)
        ->set_stream("wrong");
    auto host_failed = dispatch(runtime, q).projected_captures(0);
    auto host_failure = finish(runtime, host_failed.id());
    CHECK(host_failure.run.state == device::ProjectedState::failed);
    CHECK(host_failure.committed == 0 && host_failure.run.queue.produced == 0);
    CHECK(host_failure.run.terminal.initiating_error);
    for (auto fault : {TEST_REJECT_PROGRAM, TEST_FAILURE, TEST_SERVICE_START_FAILURE}) {
        control.fault(fault);
        auto failed = start(runtime, "failed-" + std::to_string(fault));
        auto result = finish(runtime, failed.id());
        CHECK(result.run.state == device::ProjectedState::failed);
        CHECK(result.committed == 0);
        CHECK(result.run.queue.produced == 0);
        CHECK(runtime.project_store()->run_outcome(result.raw_artifact)->disposition ==
              d::RecordedRunDisposition::failed);
    }
    control.fault(TEST_SERVICE_TRIGGER);
    q.Clear();
    *q.mutable_projected_start() = request();
    q.set_request_id("trigger-evidence");
    auto *step = q.mutable_projected_start()->mutable_program()->mutable_inline_program()->mutable_steps(0);
    step->mutable_capture()->set_mode(w::PROJECTED_HARDWARE_TRIGGER);
    step->mutable_capture()->add_cameras("camera-alpha");
    auto *intent = step->mutable_capture()->mutable_trigger();
    intent->set_controller("controller-alpha");
    intent->set_request("trigger-request");
    intent->add_endpoints("camera-alpha");
    auto trigger_capture = dispatch(runtime, q).projected_captures(0);
    auto triggers = finish(runtime, trigger_capture.id());
    CHECK(triggers.run.state == device::ProjectedState::failed);
    CHECK(triggers.committed == 2);
    CHECK(triggers.evidence.trigger_events == 1);
    CHECK(triggers.evidence.commanded_established == 1);
    CHECK(triggers.evidence.commanded_unknown == 1);
    CHECK(triggers.evidence.acknowledgement_unavailable == 2);
    CHECK(runtime.project_store()->bundle(triggers.raw_artifact).triggers.size() == 1);
    control.fault(TEST_SERVICE_PENDING);
    auto pending = start(runtime, "pending");
    q.Clear();
    q.mutable_projected_bundle()->set_id(pending.id());
    dispatch(runtime, q, Status::busy);
    opened = control.opens();
    q.Clear();
    *q.mutable_projected_start() = request();
    q.set_request_id("busy");
    dispatch(runtime, q, Status::busy);
    CHECK(control.opens() == opened);
    rejects([&] { runtime.start_capture({{"parent-alpha"}}); }, Status::busy);
    rejects([&] { runtime.enable_plugin("org.example.projected-contract", false); }, Status::busy);
    rejects([&] { runtime.open_project(runtime.project_store()->root() / "other", true); }, Status::busy);
    CHECK(!runtime.devices().empty());
    CHECK(runtime.projected_status({pending.id()}).active);
    q.Clear();
    auto *stop = q.mutable_projected_stop();
    stop->set_capture_id(pending.id());
    stop->set_expected_run_id("wrong");
    stop->set_expected_generation_id(pending.generation_id());
    dispatch(runtime, q, Status::invalid_argument);
    stop->set_expected_run_id(pending.run_id());
    stop->set_expected_generation_id("stale");
    dispatch(runtime, q, Status::invalid_argument);
    stop->set_expected_generation_id(pending.generation_id());
    stop->set_mode(static_cast<w::ProjectedStopMode>(99));
    dispatch(runtime, q, Status::invalid_argument);
    stop->set_mode(w::PROJECTED_CANCEL);
    auto aborted = control.aborts();
    dispatch(runtime, q);
    dispatch(runtime, q);
    auto cancelled = finish(runtime, pending.id());
    CHECK(cancelled.run.state == device::ProjectedState::cancelled);
    CHECK(cancelled.run.terminal.reason == d::AcquisitionReason::user_cancel);
    CHECK(control.aborts() == aborted + 1);
    dispatch(runtime, q);
    CHECK(control.aborts() == aborted + 1);
    // Camera-to-projected overlap must be rejected before executor open.
    control.fault(TEST_NORMAL);
    auto camera = runtime.start_capture({{"parent-alpha"}});
    opened = control.opens();
    q.Clear();
    *q.mutable_projected_start() = request();
    q.set_request_id("camera-busy");
    dispatch(runtime, q, Status::busy);
    CHECK(control.opens() == opened);
    runtime.stop_capture(camera.id);
    control.fault(TEST_SERVICE_ABORT_STATES);
    auto stopped = start(runtime, "stop");
    q.Clear();
    stop = q.mutable_projected_stop();
    stop->set_capture_id(stopped.id());
    stop->set_expected_run_id(stopped.run_id());
    stop->set_expected_generation_id(stopped.generation_id());
    dispatch(runtime, q);
    auto clean = finish(runtime, stopped.id());
    CHECK(clean.run.state == device::ProjectedState::completed);
    CHECK(clean.run.terminal.reason == d::AcquisitionReason::user_stop);
    q.Clear();
    q.mutable_projected_status()->set_id(stopped.id());
    auto status = dispatch(runtime, q).projected_captures(0);
    CHECK(status.abort_outcome().inhibited().presence() == w::PROJECTED_ESTABLISHED);
    CHECK(status.abort_outcome().inhibited().value());
    CHECK(status.abort_outcome().stale_work_fenced().presence() == w::PROJECTED_UNKNOWN);
    CHECK(!status.abort_outcome().stale_work_fenced().has_value());
    CHECK(status.abort_outcome().off_requested().presence() == w::PROJECTED_UNAVAILABLE);
    CHECK(!status.has_stop_error());
    control.fault(TEST_SERVICE_STOP_FAILURE);
    auto cleanup = start(runtime, "cleanup");
    auto failed = finish(runtime, cleanup.id());
    CHECK(failed.run.state == device::ProjectedState::failed);
    CHECK(failed.run.terminal.stop_error);
    CHECK(runtime.project_store()->bundle_summary(failed.raw_artifact).last_executor_disposition ==
          d::AcquisitionDisposition::completed);
    control.fault(TEST_SERVICE_PENDING);
    auto cancel_fault = start(runtime, "cancel-cleanup-fault");
    control.fault(TEST_SERVICE_STOP_FAILURE);
    runtime.stop_projected({{cancel_fault.id()},
                            {{cancel_fault.run_id()}},
                            {{cancel_fault.generation_id()}},
                            services::ProjectedStopMode::cancel});
    auto cancel_failed = finish(runtime, cancel_fault.id());
    CHECK(cancel_failed.run.state == device::ProjectedState::failed);
    CHECK(cancel_failed.run.terminal.reason == d::AcquisitionReason::user_cancel);
    CHECK(cancel_failed.run.terminal.stop_error);
    control.fault(TEST_SERVICE_ABORT_FALSE);
    auto false_abort = start(runtime, "abort-false");
    runtime.stop_projected({{false_abort.id()},
                            {{false_abort.run_id()}},
                            {{false_abort.generation_id()}},
                            services::ProjectedStopMode::cancel});
    auto false_final = finish(runtime, false_abort.id());
    CHECK(false_final.run.state == device::ProjectedState::failed);
    q.Clear();
    q.mutable_projected_status()->set_id(false_abort.id());
    status = dispatch(runtime, q).projected_captures(0);
    CHECK(status.abort_outcome().stale_work_fenced().presence() == w::PROJECTED_ESTABLISHED);
    CHECK(status.abort_outcome().stale_work_fenced().has_value() &&
          !status.abort_outcome().stale_work_fenced().value());
    // A pending run's next complete publication encounters an injected filesystem failure.
    control.fault(TEST_SERVICE_PENDING);
    auto recording = start(runtime, "storage-fault");
    auto directory = runtime.project_store()->root() / "objects" / recording.raw_artifact_id();
    std::filesystem::create_directory(directory / "0.segment.part"); // impossible record file open
    control.fault(TEST_SERVICE_RECORDING_FAILURE);
    auto broken = finish(runtime, recording.id(), false);
    CHECK(broken.run.state == device::ProjectedState::failed);
    CHECK(broken.run.terminal.reason != d::AcquisitionReason::user_stop &&
          broken.run.terminal.reason != d::AcquisitionReason::user_cancel);
    CHECK(broken.storage_state == artifact::ArtifactState::recoverable);
    CHECK(broken.committed < broken.run.queue.produced);
    CHECK(!std::filesystem::exists(directory / "run.outcome"));
    std::filesystem::remove(directory / "0.segment.part");
    control.fault(TEST_SERVICE_COMPLETE);
    // Strict typed CLI parser and request-id forwarding, no alternate semantic model.
    auto cli =
        projected_command(std::vector<std::string>{"start", "plugin", "parent", "--program-from-raw",
                                                   complete.raw_artifact.value, "--request-id", "cli-retry"});
    CHECK(cli.request_id() == "cli-retry");
    CHECK(cli.projected_start().program().raw_capture_artifact_id() == complete.raw_artifact.value);
    bool rejected{};
    try {
        projected_command(std::vector<std::string>{"cancel", "x"});
    } catch (...) {
        rejected = true;
    }
    CHECK(rejected);
}
} // namespace
int main(int argc, char **argv) {
    try {
        CHECK(argc == 4);
        plugins::Loaded plugin(argv[1]);
        auto *control =
            static_cast<const TestProjectedControl *>(plugin.api()->query_interface(TEST_PROJECTED_CONTROL));
        CHECK(control);
        auto root = std::filesystem::path(argv[3]) / ("projected-control-" + Id::random().value);
        {
            std::atomic<int64_t> lease_time{};
            services::Configuration config{argv[2], {}, root, {}, {"org.example.projected-contract"}, [&] {
                                               return std::chrono::steady_clock::time_point{} +
                                                      std::chrono::seconds(lease_time.load());
                                           }};
            services::Runtime runtime(config);
            tests(runtime, *control, lease_time);
        }
        control->fault(TEST_SERVICE_PENDING);
        auto begin = std::chrono::steady_clock::now();
        {
            services::Runtime runtime(
                {argv[2], {}, root / "shutdown", {}, {"org.example.projected-contract"}});
            auto run = start(runtime, "shutdown");
            CHECK(run.active());
        }
        CHECK(std::chrono::steady_clock::now() - begin < 3s);
        CHECK(control->live_instances() == 0);
        // Hold the real Store append lock on a mapped pixel read. Projected stop/status
        // must reach L3 independently, even while the recorder waits for that same Store.
        control->fault(TEST_SERVICE_CAPTURE);
        {
            services::Runtime runtime(
                {argv[2], {}, root / "blocked-storage", {}, {"org.example.projected-contract"}});
            w::Request q;
            *q.mutable_projected_start() = request();
            auto *capture = q.mutable_projected_start()
                                ->mutable_program()
                                ->mutable_inline_program()
                                ->mutable_steps(0)
                                ->mutable_capture();
            capture->set_mode(w::PROJECTED_FREE_RUNNING);
            capture->add_cameras("camera-alpha");
            auto source = finish(runtime, dispatch(runtime, q).projected_captures(0).id());
            auto store = runtime.project_store();
            auto original = std::make_shared<d::AcquisitionBundle>(store->bundle(source.raw_artifact));
            auto bundle = *original;
            auto frameset = std::make_shared<d::Packet>(*bundle.frameset);
            auto image = std::make_shared<d::Packet>(*frameset->frames[0]);
            auto pixels = image->attributes[0].buffer.map_read();
            CHECK(pixels);
            auto fence = std::make_shared<BlockedRead>();
            auto storage = std::make_shared<memory::Storage>();
            storage->owner = original;
            storage->host = pixels->data();
            storage->size = pixels->size();
            storage->alignment = image->attributes[0].buffer.alignment();
            storage->ready = fence;
            image->attributes[0].buffer = {storage, 0, storage->size};
            frameset->frames[0] = image;
            bundle.frameset = frameset;
            auto raw = store->begin_projected_capture(store->capture_header(source.raw_artifact));
            control->fault(TEST_SERVICE_PENDING);
            auto pending = start(runtime, "blocked-store-stop");
            auto append = std::async(std::launch::async, [&] { store->append_bundle(raw, bundle); });
            struct Release {
                std::shared_ptr<BlockedRead> fence;
                ~Release() { fence->release(); }
            } release{fence};
            CHECK(fence->waiting());
            auto before_stop = std::chrono::steady_clock::now();
            runtime.stop_projected({{pending.id()},
                                    {{pending.run_id()}},
                                    {{pending.generation_id()}},
                                    services::ProjectedStopMode::normal_stop});
            auto stopped = runtime.projected_status({pending.id()});
            CHECK(stopped.run.state == device::ProjectedState::stopping || stopped.run.cleanup_resolved);
            CHECK(std::chrono::steady_clock::now() - before_stop < 500ms);
            fence->release();
            append.get();
            store->abandon(raw);
            CHECK(finish(runtime, pending.id()).run.state == device::ProjectedState::completed);
        }
        CHECK(control->live_instances() == 0);
        // Unrelated camera and projected resources coexist, including finite teardown of both.
        const auto mixed_plugins = root / "mixed-plugins";
        std::filesystem::create_directories(mixed_plugins);
        std::filesystem::copy_file(std::filesystem::path(argv[3]) / "plugins" / "virtual-scanner.json",
                                   mixed_plugins / "virtual-scanner.json");
        std::filesystem::copy_file(std::filesystem::path(argv[3]) / "plugins" / "virtual-scanner.so",
                                   mixed_plugins / "virtual-scanner.so");
        std::filesystem::copy_file(std::filesystem::path(argv[2]) / "projected.json",
                                   mixed_plugins / "projected.json");
        std::filesystem::create_symlink(std::filesystem::absolute(argv[1]),
                                        mixed_plugins / std::filesystem::path(argv[1]).filename());
        auto aborts = control->aborts();
        begin = std::chrono::steady_clock::now();
        {
            services::Runtime runtime({mixed_plugins,
                                       {},
                                       root / "mixed-shutdown",
                                       {},
                                       {"org.example.projected-contract", "org.mantis.virtual-scanner"}});
            auto projected = start(runtime, "mixed");
            auto camera = runtime.start_capture({{"virtual-scanner"}});
            CHECK(camera.active);
            CHECK(runtime.projected_status({projected.id()}).active);
            CHECK(runtime.devices().size() == 2);
        }
        CHECK(control->aborts() == aborts + 1);
        CHECK(control->live_instances() == 0);
        CHECK(std::chrono::steady_clock::now() - begin < 3s);
        control->fault(TEST_SERVICE_CLOSE_FAILURE);
        {
            services::Runtime runtime(
                {argv[2], {}, root / "close-failure", {}, {"org.example.projected-contract"}});
            auto run = start(runtime, "close-failure");
            auto failed = finish(runtime, run.id());
            CHECK(failed.run.state == device::ProjectedState::failed);
            CHECK(failed.run.terminal.close_error);
            auto opened = control->opens();
            rejects([&] { start(runtime, "close-resources-owned"); }, Status::busy);
            CHECK(control->opens() == opened);
            rejects([&] { runtime.enable_plugin("org.example.projected-contract", false); }, Status::busy);
            control->fault(TEST_SERVICE_COMPLETE); // Let the fixture honor destruction on the final retry.
        }
        CHECK(control->live_instances() == 0);
        control->fault(TEST_SERVICE_CAPTURE);
        {
            services::Runtime runtime(
                {argv[2], {}, root / "queued-finalize", {}, {"org.example.projected-contract"}});
            w::Request q;
            *q.mutable_projected_start() = request();
            q.set_request_id("paced-source");
            auto *intent = q.mutable_projected_start()
                               ->mutable_program()
                               ->mutable_inline_program()
                               ->mutable_steps(0)
                               ->mutable_capture();
            intent->set_mode(w::PROJECTED_FREE_RUNNING);
            intent->add_cameras("camera-alpha");
            auto source = finish(runtime, dispatch(runtime, q).projected_captures(0).id());
            auto replay = runtime.replay_capture(source.raw_artifact, true, false);
            auto deadline = std::chrono::steady_clock::now() + 3s;
            bool published{};
            while (!published && std::chrono::steady_clock::now() < deadline) {
                try {
                    auto ref = runtime.projected_bundle(replay);
                    runtime.release_preview(ref.lease);
                    published = true;
                } catch (const Failure &e) {
                    CHECK(e.error.code == Status::busy);
                    std::this_thread::sleep_for(2ms);
                }
            }
            CHECK(published);
            control->fault(TEST_SERVICE_COMPLETE);
            auto pending = start(runtime, "queued-finalization");
            services::ProjectedCaptureInfo info;
            do {
                info = runtime.projected_status({pending.id()});
                if (!info.finalization_job.value.empty())
                    break;
                std::this_thread::sleep_for(2ms);
            } while (std::chrono::steady_clock::now() < deadline);
            CHECK(!info.finalization_job.value.empty());
            runtime.cancel_job(info.finalization_job);
            runtime.cancel_job(replay);
            auto cancelled = finish(runtime, pending.id(), false);
            CHECK(cancelled.run.state == device::ProjectedState::completed);
            CHECK(cancelled.recording_error->code == Status::cancelled);
            CHECK(runtime.project_store()->run_outcome(cancelled.raw_artifact)->disposition ==
                  d::RecordedRunDisposition::completed);
            CHECK(runtime.recover_artifact(cancelled.raw_artifact).state ==
                  artifact::ArtifactState::finalized);
            CHECK(runtime.projected_status(cancelled.id).storage_state == artifact::ArtifactState::finalized);
        }
        control->fault(TEST_REJECT_PROGRAM);
        {
            services::Runtime runtime(
                {argv[2], {}, root / "history", {}, {"org.example.projected-contract"}});
            for (unsigned i = 0; i < 64; ++i)
                finish(runtime, start(runtime, "history-" + std::to_string(i)).id());
            CHECK(runtime.projected_captures().size() == 64);
            auto opens = control->opens();
            rejects([&] { start(runtime, "history-overflow"); }, Status::busy);
            CHECK(control->opens() == opens);
            start(runtime, "history-0");
            CHECK(control->opens() == opens);
        }
        control->fault(TEST_NORMAL);
        std::filesystem::remove_all(root);
        std::cout << "Projected daemon service and actual protocol dispatch passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
