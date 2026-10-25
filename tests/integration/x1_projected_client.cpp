#include "../../src/protocol/projected_adapter.hpp"
#include "../fixtures/x1-projected/program.hpp"
#include <fstream>
#include <iostream>
#include <mantis/artifact_store.hpp>
#include <mantis/client.hpp>
#include <thread>
using namespace mantis;
using namespace std::chrono_literals;
namespace w = mantis::wire::v1;
namespace d = mantis::data;
#define CHECK(...)                                                                                           \
    do {                                                                                                     \
        if (!(__VA_ARGS__))                                                                                  \
            throw std::runtime_error("Line " + std::to_string(__LINE__) + ": " #__VA_ARGS__);                \
    } while (false)
template <class F> void rejects(F f, Status status) {
    try {
        f();
    } catch (const Failure &e) {
        CHECK(e.error.code == status);
        return;
    }
    throw std::runtime_error("Expected rejection");
}
w::ProjectedCapture finish(client::Client &client, std::string id, bool finalized = true) {
    auto until = std::chrono::steady_clock::now() + 5s;
    do {
        auto s = client.projected_status(id);
        if (s.cleanup_resolved() && ((!finalized && s.has_recording_error() &&
                                      s.storage_state() == w::PROJECTED_STORAGE_RECOVERABLE) ||
                                     s.storage_state() == w::PROJECTED_STORAGE_FINALIZED))
            return s;
        std::this_thread::sleep_for(2ms);
    } while (std::chrono::steady_clock::now() < until);
    throw std::runtime_error("X1 daemon completion timeout");
}
void verify(const std::filesystem::path &root, const std::string &mode) {
    artifact::Store store(root);
    unsigned captures{};
    for (const auto &a : store.list()) {
        if (a.type.name != "org.mantis.RawCapture" || a.type.schema_version != 3)
            continue;
        ++captures;
        if (mode == "recorder") {
            CHECK(a.state == artifact::ArtifactState::recoverable);
            CHECK(!store.run_outcome(a.id));
            continue;
        }
        if (mode == "shutdown") {
            CHECK(a.state == artifact::ArtifactState::recoverable ||
                  a.state == artifact::ArtifactState::finalized);
            auto before = store.run_outcome(a.id);
            const uint64_t count = 0; // blocked fixture has not published any bundle
            CHECK(before && before->reason == d::AcquisitionReason::user_stop && before->abort_outcome);
            CHECK(before->abort_outcome->off_requested.get() && *before->abort_outcome->off_requested.get());
            CHECK(count == 0);
            std::ostringstream original;
            d::write_run_outcome(original, {count, *before});
            if (a.state == artifact::ArtifactState::recoverable)
                CHECK(store.recover(a.id).state == artifact::ArtifactState::finalized);
            std::ostringstream recovered;
            d::write_run_outcome(recovered, {count, *store.run_outcome(a.id)});
            CHECK(original.str() == recovered.str());
            CHECK(store.bundle_summary(a.id).records == 0);
        } else
            CHECK(a.state == artifact::ArtifactState::finalized);
        auto header = store.capture_header(a.id);
        CHECK(header.program.steps.size() == 5);
        auto outcome = store.run_outcome(a.id);
        CHECK(outcome && outcome->run == header.run && outcome->generation == header.generation);
        auto summary = store.bundle_summary(a.id);
        if (mode == "prepare" || mode == "start") {
            CHECK(summary.records == 0 && outcome->disposition == d::RecordedRunDisposition::failed);
            continue;
        }
        std::vector<std::string> bytes;
        uint64_t n{};
        store.replay_bundles(a.id, [&](d::AcquisitionBundle b) {
            CHECK(b.key.run_id == header.run && b.key.sequence.value == n++);
            CHECK(b.evidence.program.id == header.program.identity.id &&
                  b.evidence.program.hash == header.program.identity.hash);
            CHECK(b.evidence.program.content.presence() == header.program.identity.content.presence());
            CHECK(b.published.clock.generation == header.generation);
            CHECK(d::validate(b));
            if (b.frameset) {
                CHECK(b.frameset->frames.size() == 2 && b.evidence.frames.size() == 2);
                auto step = b.evidence.step.get();
                CHECK(step && step->run_id == header.run);
                auto index = (step->step_index - 10) / 3;
                CHECK(index < 5);
                for (size_t i = 0; i < 2; ++i) {
                    const auto &f = b.evidence.frames[i];
                    auto image = b.frameset->frames[i];
                    CHECK(f.frame.camera == header.program.participants.cameras[i].component);
                    CHECK(f.frame.stream.id == header.program.participants.cameras[i].stream);
                    CHECK(f.frame.native_sequence == image->header.sequence.value);
                    CHECK(f.source_timestamp.get()->nanoseconds == image->header.timestamp.nanoseconds);
                    CHECK(f.host_received.get()->time.nanoseconds == image->header.received.nanoseconds);
                    CHECK(!f.exposure.get() && !f.sync.get()->hardware_association.get());
                    auto mapped = image->attributes[0].buffer.map_read();
                    CHECK(mapped && mapped->size() == 3840);
                    for (size_t k = 0; k < mapped->size(); ++k)
                        CHECK((*mapped)[k] ==
                              std::byte((k + f.frame.native_sequence * 7 + (i ? 97 : 0)) & 255));
                }
                for (size_t i = 0; i < 2; ++i) {
                    const auto &e = b.evidence.emitters[i];
                    CHECK(e.commanded.get()->state == header.program.steps[index].emitters[i].state);
                    CHECK(e.acknowledged.presence() == d::Presence::unavailable &&
                          e.observed.presence() == d::Presence::unavailable);
                    for (const auto &v : e.exposure_effective)
                        CHECK(v.state.presence() == d::Presence::unavailable);
                }
                CHECK(b.triggers.empty());
            }
            std::ostringstream encoded;
            d::write_bundle(encoded, b);
            bytes.push_back(encoded.str());
        });
        CHECK(n == summary.records);
        n = 0;
        store.replay_bundles(a.id, [&](d::AcquisitionBundle b) {
            std::ostringstream encoded;
            d::write_bundle(encoded, b);
            CHECK(encoded.str() == bytes.at(n++));
        });
        CHECK(n == bytes.size());
        if (mode == "normal" && outcome->disposition == d::RecordedRunDisposition::completed)
            CHECK(summary.records == 21);
        if (mode == "timeout")
            CHECK(outcome->reason == d::AcquisitionReason::timeout);
        if (mode == "disconnect")
            CHECK(outcome->disposition == d::RecordedRunDisposition::failed);
    }
    CHECK(captures > 0);
    std::cout << "X1 RawCapture-3 exact header/outcome/images/timing/evidence and two-pass replay verified ("
              << mode << ")\n";
}
int main(int argc, char **argv) {
    try {
        CHECK(argc == 3 || argc == 4);
        if (argc == 4) {
            verify(argv[2], argv[3]);
            return 0;
        }
        const std::string mode = argv[1];
        std::filesystem::path project = argv[2];
        client::Client client;
        auto devices = client.projected_devices();
        CHECK(devices.size() == 1);
        const auto &discovered = devices[0];
        CHECK(discovered.plugin_id() == "org.mantis.x1" && discovered.components_size() == 6);
        device::ProjectedGraph graph;
        graph.parent = {discovered.parent_id()};
        for (const auto &v : discovered.components()) {
            device::ProjectedComponent c;
            c.descriptor.id = {v.id()};
            c.role = v.role();
            c.kind = static_cast<device::ParticipantKind>(v.kind());
            if (v.has_image_source())
                c.image_source = device::ProjectedImageSource{{{v.image_source().stream_id()}},
                                                              v.image_source().physical_identity(),
                                                              v.image_source().width(),
                                                              v.image_source().height()};
            graph.components.push_back(std::move(c));
        }
        auto p = x1_fixture::program(graph, 4);
        w::ProjectedCaptureRequest q;
        q.set_plugin_id(discovered.plugin_id());
        q.set_parent_id(discovered.parent_id());
        auto write = [&](const d::AcquisitionProgram &value) {
            protocol::write_projected_program(q.mutable_program()->mutable_inline_program(), value);
        };
        write(p);
        CHECK(client.validate_projected(q).accepted());
        // Pure validation: actual graph selections and unsupported evidence/mode fail.
        for (unsigned invalid = 0; invalid < 5; ++invalid) {
            auto bad = p;
            if (invalid == 0)
                bad.participants.cameras[0].component = {{"foreign-camera"}};
            if (invalid == 1) {
                bad.steps[0].capture.mode = d::CaptureMode::hardware_trigger;
                bad.steps[0].capture.trigger = d::TriggerIntent{
                    p.participants.controllers[0], {{"no-trigger"}}, bad.steps[0].capture.cameras};
            }
            if (invalid == 2)
                bad.participants.controllers[0] = {{"foreign-controller"}};
            if (invalid == 3)
                bad.repetitions = 0;
            if (invalid == 4)
                bad.steps[0].settle = 1ms;
            write(bad);
            CHECK(!client.validate_projected(q).accepted());
        }
        write(p);
        if (mode == "normal") {
            auto camera = client.start_capture({discovered.parent_id()});
            rejects([&] { client.start_projected(q, "camera-busy"); }, Status::busy);
            CHECK(client.capture_status(camera).active());
            client.stop_capture(camera);
        }
        if (mode == "timeout") {
            p.bounds.max_duration = 200ms;
            write(p);
        }
        auto begun = std::chrono::steady_clock::now();
        auto capture = client.start_projected(q, "x1-start");
        CHECK(client.start_projected(q, "x1-start").id() == capture.id());
        // The header exists and is immutable before the first executor publication.
        std::ifstream input(project / "objects" / capture.raw_artifact_id() / "run.header", std::ios::binary);
        char magic[8]{};
        input.read(magic, 8);
        CHECK(std::string(magic, 8) == "MRUNHDR3");
        if (mode == "normal" || mode == "stop" || mode == "abort" || mode == "recorder") {
            rejects([&] { client.start_projected(q, "second-projected"); }, Status::busy);
            rejects([&] { client.start_capture({discovered.parent_id()}); }, Status::busy);
            rejects([&] { client.stop_projected(capture.id(), "stale-run", capture.generation_id()); },
                    Status::invalid_argument);
            rejects([&] { client.stop_projected(capture.id(), capture.run_id(), "stale-generation"); },
                    Status::invalid_argument);
        }
        if (mode == "recorder") {
            std::filesystem::create_directory(project / "objects" / capture.raw_artifact_id() /
                                              "0.segment.part");
            std::ofstream release(std::getenv("MANTIS_X1_PROJECTED_RELEASE_FILE"));
            release << "release";
            CHECK(release.good());
        }
        if (mode == "stop")
            client.stop_projected(capture.id(), capture.run_id(), capture.generation_id());
        if (mode == "abort") {
            auto at = std::chrono::steady_clock::now();
            client.cancel_projected(capture.id(), capture.run_id(), capture.generation_id());
            CHECK(std::chrono::steady_clock::now() - at < 500ms);
        }
        if (mode == "shutdown") {
            rejects([&] { client.start_capture({discovered.parent_id()}); }, Status::busy);
            w::Request shutdown;
            shutdown.mutable_shutdown();
            client.call(shutdown);
            return 0;
        }
        auto result = finish(client, capture.id(), mode != "recorder");
        if (mode == "normal") {
            CHECK(result.state() == w::PROJECTED_COMPLETED && result.reason() == w::PROJECTED_REASON_NONE &&
                  result.committed_bundles() == 21);
            auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - begun).count();
            auto replay = client.replay(result.raw_artifact_id(), false, true);
            CHECK(client.wait(replay).status().find("passes=2") != std::string::npos);
            CHECK(client.projected_bundle(replay)->key.run_id.id.value == result.run_id());
            std::cout << "X1 daemon: 20 captured FrameSets + terminal bundle in " << elapsed << "s; "
                      << 20 / elapsed << " FrameSets/s; replay passes=2\n";
            auto camera = client.start_capture({discovered.parent_id()});
            client.stop_capture(camera);
        } else if (mode == "recorder") {
            CHECK(result.state() == w::PROJECTED_FAILED &&
                  result.storage_state() == w::PROJECTED_STORAGE_RECOVERABLE);
            CHECK(result.reason() == w::PROJECTED_RESOURCE_LIMIT &&
                  result.reason() != w::PROJECTED_USER_STOP);
            CHECK(result.abort_outcome().off_requested().value());
        } else if (mode == "stop")
            CHECK(result.reason() == w::PROJECTED_USER_STOP);
        else if (mode == "abort")
            CHECK(result.state() == w::PROJECTED_CANCELLED && result.reason() == w::PROJECTED_USER_CANCEL);
        else {
            CHECK(result.state() == w::PROJECTED_FAILED);
            if (mode == "prepare" || mode == "start")
                CHECK(result.committed_bundles() == 0);
        }
        w::Request shutdown;
        shutdown.mutable_shutdown();
        client.call(shutdown);
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
