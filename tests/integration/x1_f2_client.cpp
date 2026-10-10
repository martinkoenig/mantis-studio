#include "../../src/protocol/projected_adapter.hpp"
#include "../fixtures/x1-f2/program.hpp"
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
    throw std::runtime_error("Missing expected rejection");
}
w::ProjectedCapture finish(client::Client &client, const std::string &id, bool recorder = false) {
    auto deadline = std::chrono::steady_clock::now() + 5s;
    do {
        auto s = client.projected_status(id);
        if (s.cleanup_resolved() &&
            (recorder ? s.has_recording_error() && s.storage_state() == w::PROJECTED_STORAGE_RECOVERABLE
                      : s.storage_state() == w::PROJECTED_STORAGE_FINALIZED))
            return s;
        std::this_thread::sleep_for(2ms);
    } while (std::chrono::steady_clock::now() < deadline);
    throw std::runtime_error("F2 daemon cleanup/finalization timeout");
}
void verify(const std::filesystem::path &project, const std::string &mode) {
    artifact::Store store(project);
    unsigned count{};
    for (auto &a : store.list()) {
        if (a.type.name != "org.mantis.RawCapture" || a.type.schema_version != 3)
            continue;
        ++count;
        auto header = store.capture_header(a.id);
        CHECK(header.program.steps.size() == 1 && header.program.participants.cameras.empty());
        if (mode == "recorder") {
            CHECK(a.state == artifact::ArtifactState::recoverable && !store.run_outcome(a.id));
            continue;
        }
        CHECK(a.state == artifact::ArtifactState::finalized);
        auto outcome = store.run_outcome(a.id);
        CHECK(outcome && outcome->run == header.run && outcome->generation == header.generation);
        auto summary = store.bundle_summary(a.id);
        std::vector<std::string> encoded;
        store.replay_bundles(a.id, [&](d::AcquisitionBundle b) {
            CHECK(!b.frameset && b.triggers.empty() && b.evidence.frames.empty());
            CHECK(d::validate(b));
            CHECK(b.key.run_id == header.run && b.published.clock.generation == header.generation);
            CHECK(b.evidence.program.id == header.program.identity.id &&
                  b.evidence.program.hash == header.program.identity.hash &&
                  b.evidence.program.content.presence() == header.program.identity.content.presence());
            CHECK(b.evidence.participants.cameras.empty() && b.evidence.emitters.size() == 1);
            for (auto &e : b.evidence.emitters)
                CHECK(!e.observed.get() && !e.acknowledged.get() && e.exposure_effective.empty());
            std::ostringstream bytes;
            d::write_bundle(bytes, b);
            encoded.push_back(bytes.str());
        });
        CHECK(encoded.size() == summary.records);
        size_t index{};
        store.replay_bundles(a.id, [&](d::AcquisitionBundle b) {
            std::ostringstream bytes;
            d::write_bundle(bytes, b);
            CHECK(bytes.str() == encoded.at(index++));
        });
        CHECK(index == encoded.size());
        std::ostringstream before, after;
        d::write_run_outcome(before, {summary.records, *outcome});
        d::write_run_outcome(after, {summary.records, *store.run_outcome(a.id)});
        CHECK(before.str() == after.str());
        if (mode == "normal" || mode == "off" || mode == "lost-ack" || mode == "lost-event")
            CHECK(outcome->disposition == d::RecordedRunDisposition::completed && summary.records == 2);
        if (mode == "prepare" || mode == "configure" || mode == "arm")
            CHECK(outcome->disposition == d::RecordedRunDisposition::failed && summary.records == 0);
        if (mode == "cleanup") {
            CHECK(outcome->disposition == d::RecordedRunDisposition::failed);
            CHECK(encoded.size() == 2);
            CHECK(encoded.back().find("F2 v1 synthetic terminal record hex=") != std::string::npos);
        }
    }
    CHECK(count);
    std::cout << "F2 RawCapture-3 zero-camera canonical two-pass replay after controller teardown (" << mode
              << ")\n";
}
int main(int argc, char **argv) {
    try {
        CHECK(argc == 3 || argc == 4);
        if (argc == 4) {
            verify(argv[2], argv[3]);
            return 0;
        }
        std::string mode = argv[1];
        std::filesystem::path project = argv[2];
        client::Client client;
        auto devices = client.projected_devices();
        CHECK(devices.size() == 1 && devices[0].components_size() == 5);
        auto selected = devices[0];
        device::ProjectedGraph graph;
        graph.parent = {selected.parent_id()};
        for (auto &v : selected.components()) {
            device::ProjectedComponent c;
            c.descriptor.id = {v.id()};
            c.kind = static_cast<device::ParticipantKind>(v.kind());
            graph.components.push_back(c);
        }
        auto p = x1_f2_fixture::program(graph, mode != "off");
        w::ProjectedCaptureRequest q;
        q.set_plugin_id(selected.plugin_id());
        q.set_parent_id(selected.parent_id());
        auto write = [&](const d::AcquisitionProgram &program) {
            protocol::write_projected_program(q.mutable_program()->mutable_inline_program(), program);
        };
        write(p);
        CHECK(client.validate_projected(q).accepted());
        for (unsigned bad = 0; bad < 6; ++bad) {
            auto v = p;
            if (bad == 0)
                v.repetitions = 2;
            if (bad == 1)
                v.steps[0].settle = 1ms;
            if (bad == 2)
                v.steps[0].evidence_requirement = d::EvidenceRequirement::controller_acknowledged;
            if (bad == 3)
                v.participants.controllers[0] = {{"foreign-controller"}};
            if (bad == 4)
                v.steps[0].capture.mode = d::CaptureMode::free_running;
            if (bad == 5) {
                v.bounds.max_on_duration = 1ms;
                v.steps[0].emitters[0].state = d::EmitterState::on;
            }
            write(v);
            CHECK(!client.validate_projected(q).accepted());
        }
        write(p);
        if (mode == "normal") {
            auto camera = client.start_capture({selected.parent_id()});
            rejects([&] { client.start_projected(q, "busy-camera"); }, Status::busy);
            CHECK(client.capture_status(camera).active());
            client.stop_capture(camera);
        }
        if (mode == "timeout") {
            p.bounds.max_duration = 400ms;
            write(p);
        }
        auto capture = client.start_projected(q, "f2-start");
        CHECK(client.start_projected(q, "f2-start").id() == capture.id());
        std::ifstream input(project / "objects" / capture.raw_artifact_id() / "run.header", std::ios::binary);
        char magic[8]{};
        input.read(magic, 8);
        CHECK(std::string(magic, 8) == "MRUNHDR3");
        if (mode == "normal" || mode == "cancel" || mode == "stop" || mode == "recorder") {
            rejects([&] { client.start_capture({selected.parent_id()}); }, Status::busy);
            rejects([&] { client.start_projected(q, "second"); }, Status::busy);
            rejects([&] { client.stop_projected(capture.id(), "stale-run", capture.generation_id()); },
                    Status::invalid_argument);
            rejects([&] { client.cancel_projected(capture.id(), capture.run_id(), "stale-generation"); },
                    Status::invalid_argument);
        }
        if (mode == "recorder")
            std::filesystem::create_directory(project / "objects" / capture.raw_artifact_id() /
                                              "0.segment.part");
        if (mode == "normal" || mode == "recorder") {
            std::ofstream release(std::getenv("MANTIS_X1_F2_RELEASE_FILE"));
            release << "release";
            CHECK(release.good());
        }
        if (mode == "cancel")
            client.cancel_projected(capture.id(), capture.run_id(), capture.generation_id());
        if (mode == "stop")
            client.stop_projected(capture.id(), capture.run_id(), capture.generation_id());
        auto result = finish(client, capture.id(), mode == "recorder");
        if ((mode == "normal" || mode == "off" || mode == "lost-ack" || mode == "lost-event") &&
            result.state() != w::PROJECTED_COMPLETED)
            std::cerr << "F2 mode=" << mode << " outcome=" << result.DebugString() << "\n";
        if (mode == "normal" || mode == "off" || mode == "lost-ack" || mode == "lost-event") {
            CHECK(result.state() == w::PROJECTED_COMPLETED && result.reason() == w::PROJECTED_REASON_NONE &&
                  result.committed_bundles() == 2);
            auto replay = client.replay(result.raw_artifact_id(), false, true);
            CHECK(client.wait(replay).status().find("passes=2") != std::string::npos);
            CHECK(client.projected_bundle(replay)->key.run_id.id.value == result.run_id());
        } else if (mode == "recorder")
            CHECK(result.state() == w::PROJECTED_FAILED && result.reason() == w::PROJECTED_RESOURCE_LIMIT &&
                  result.storage_state() == w::PROJECTED_STORAGE_RECOVERABLE);
        else if (mode == "cancel")
            CHECK(result.state() == w::PROJECTED_CANCELLED && result.reason() == w::PROJECTED_USER_CANCEL);
        else if (mode == "stop")
            CHECK(result.reason() == w::PROJECTED_USER_STOP);
        else {
            CHECK(result.state() == w::PROJECTED_FAILED);
            if (mode == "prepare" || mode == "configure" || mode == "arm")
                CHECK(result.committed_bundles() == 0);
            if (mode == "timeout")
                CHECK(result.reason() == w::PROJECTED_TIMEOUT);
        }
        if (mode != "stop-ack" && mode != "prepare" && mode != "cleanup")
            CHECK(result.abort_outcome().off_requested().value());
        CHECK(!client.projected_captures().empty());
        auto camera = client.start_capture({selected.parent_id()});
        client.stop_capture(camera);
        w::Request shutdown;
        shutdown.mutable_shutdown();
        client.call(shutdown);
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
