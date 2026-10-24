#include "../../src/protocol/projected_adapter.hpp"
#include "../fixtures/x1-f2/program.hpp"
#include <fstream>
#include <iostream>
#include <mantis/artifact_store.hpp>
#include <mantis/client.hpp>
#include <sstream>
#include <thread>
using namespace mantis;
using namespace std::chrono_literals;
namespace d = mantis::data;
namespace w = mantis::wire::v1;
#define CHECK(...)                                                                                           \
    do {                                                                                                     \
        if (!(__VA_ARGS__))                                                                                  \
            throw std::runtime_error("Line " + std::to_string(__LINE__) + ": " #__VA_ARGS__);                \
    } while (false)
namespace {
void verify(const std::filesystem::path &root) {
    std::ifstream identities(root / "expected-identities");
    std::string expected_run, expected_generation, expected_artifact;
    CHECK(std::getline(identities, expected_run) && std::getline(identities, expected_generation) &&
          std::getline(identities, expected_artifact));
    std::ifstream terminal_file(root / "expected-terminal", std::ios::binary);
    CHECK(terminal_file.good());
    std::string expected_terminal{std::istreambuf_iterator<char>(terminal_file), {}};
    artifact::Store store(root);
    unsigned captures = 0;
    for (const auto &a : store.list())
        if (a.type.name == "org.mantis.RawCapture" && a.type.schema_version == 3) {
            ++captures;
            CHECK(a.id.value == expected_artifact);
            CHECK(a.state == artifact::ArtifactState::recoverable);
            auto header = store.capture_header(a.id);
            CHECK(header.run.id.value == expected_run && header.generation.id.value == expected_generation);
            CHECK(header.program.identity.id.id.value == "x1-f2-synthetic-finite-on");
            auto out = store.run_outcome(a.id);
            CHECK(out && out->run == header.run && out->generation == header.generation &&
                  out->disposition == d::RecordedRunDisposition::completed);
            CHECK(!out->initiating_error && !out->abort_error && !out->stop_error && !out->close_error);
            CHECK(out->abort_outcome && out->abort_outcome->off_requested.get() &&
                  *out->abort_outcome->off_requested.get());
            CHECK(out->abort_outcome->emitters.size() == 1 &&
                  !out->abort_outcome->emitters[0].observed.get());
            std::ostringstream outcome_before;
            d::write_run_outcome(outcome_before, {2, *out});
            CHECK(store.recover(a.id).state == artifact::ArtifactState::finalized);
            CHECK(store.bundle_summary(a.id).records == 2);
            std::ostringstream outcome_after;
            d::write_run_outcome(outcome_after, {2, *store.run_outcome(a.id)});
            CHECK(outcome_before.str() == outcome_after.str());
            std::vector<std::string> expected;
            for (unsigned pass = 0; pass < 2; ++pass) {
                size_t n = 0;
                store.replay_bundles(a.id, [&](d::AcquisitionBundle b) {
                    CHECK(b.key.run_id == header.run && b.key.sequence.value == n &&
                          b.evidence.program.id == header.program.identity.id &&
                          b.evidence.program.hash == header.program.identity.hash);
                    CHECK(!b.frameset && b.triggers.empty() && b.evidence.frames.empty());
                    CHECK(b.evidence.implementations.at(0).implementation.value ==
                          "org.mantis.x1.f2.pty.simulation-only");
                    for (const auto &e : b.evidence.emitters)
                        CHECK(!e.observed.get() && !e.acknowledged.get() && e.exposure_effective.empty());
                    std::ostringstream bytes;
                    d::write_bundle(bytes, b);
                    if (n == 1)
                        CHECK(bytes.str() == expected_terminal);
                    if (pass)
                        CHECK(bytes.str() == expected.at(n));
                    else
                        expected.push_back(bytes.str());
                    ++n;
                });
                CHECK(n == 2);
            }
            bool rejected = false;
            try {
                store.recover(a.id);
            } catch (const Failure &e) {
                CHECK(e.error.code == Status::invalid_argument);
                rejected = true;
            }
            CHECK(rejected);
        }
    CHECK(captures == 1);
}
} // namespace
int main(int argc, char **argv) {
    try {
        CHECK(argc == 3);
        if (std::string_view(argv[1]) == "--verify") {
            verify(argv[2]);
            return 0;
        }
        client::Client client;
        auto devices = client.projected_devices();
        CHECK(devices.size() == 1);
        device::ProjectedGraph graph;
        graph.parent = {devices[0].parent_id()};
        for (const auto &component : devices[0].components()) {
            device::ProjectedComponent c;
            c.descriptor.id = {component.id()};
            c.kind = static_cast<device::ParticipantKind>(component.kind());
            graph.components.push_back(c);
        }
        auto p = x1_f2_fixture::program(graph, true);
        w::ProjectedCaptureRequest q;
        q.set_plugin_id(devices[0].plugin_id());
        q.set_parent_id(devices[0].parent_id());
        protocol::write_projected_program(q.mutable_program()->mutable_inline_program(), p);
        CHECK(client.validate_projected(q).accepted());
        auto capture = client.start_projected(q, "storage-qualified-start");
        auto deadline = std::chrono::steady_clock::now() + 5s;
        do {
            capture = client.projected_status(capture.id());
            if (capture.cleanup_resolved() && capture.has_recording_error() &&
                capture.storage_state() == w::PROJECTED_STORAGE_RECOVERABLE)
                break;
            std::this_thread::sleep_for(2ms);
        } while (std::chrono::steady_clock::now() < deadline);
        CHECK(capture.state() == w::PROJECTED_COMPLETED && capture.reason() == w::PROJECTED_REASON_NONE &&
              capture.committed_bundles() == 2);
        CHECK(capture.storage_state() == w::PROJECTED_STORAGE_RECOVERABLE && capture.has_recording_error() &&
              capture.recording_error().code() == static_cast<unsigned>(Status::io));
        CHECK(!capture.finalization_job_id().empty());
        bool failed = false;
        try {
            client.wait(capture.finalization_job_id());
        } catch (const Failure &) {
            failed = true;
        }
        CHECK(failed);
        std::ofstream expected(std::filesystem::path(argv[2]) / "expected-identities", std::ios::binary);
        expected << capture.run_id() << '\n'
                 << capture.generation_id() << '\n'
                 << capture.raw_artifact_id() << '\n';
        expected.close();
        CHECK(expected.good());
        w::Request shutdown;
        shutdown.mutable_shutdown();
        auto terminal = client.projected_bundle(capture.id());
        CHECK(terminal && terminal->evidence.disposition == d::AcquisitionDisposition::completed);
        std::ofstream terminal_file(std::filesystem::path(argv[2]) / "expected-terminal", std::ios::binary);
        d::write_bundle(terminal_file, *terminal);
        terminal_file.close();
        CHECK(terminal_file.good());
        client.call(shutdown);
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
