#include "../../src/protocol/projected_adapter.hpp"
#include <iostream>
#include <mantis/client.hpp>
#include <thread>
using namespace mantis;
using namespace std::chrono_literals;
namespace d = mantis::data;
#define CHECK(...)                                                                                           \
    do {                                                                                                     \
        if (!(__VA_ARGS__))                                                                                  \
            throw std::runtime_error("Line " + std::to_string(__LINE__) + ": " #__VA_ARGS__);                \
    } while (false)
int main() {
    try {
        client::Client client;
        auto devices = client.projected_devices();
        CHECK(devices.size() == 1);
        d::AcquisitionProgram p;
        p.identity = {{{"sdk-program"}}, d::Unknown{}, d::Unavailable{}};
        p.participants.cameras = {{{{"camera-alpha"}}, {{"image-stream"}}, "imaging"}};
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
        wire::v1::ProjectedCaptureRequest q;
        q.set_plugin_id(devices[0].plugin_id());
        q.set_parent_id(devices[0].parent_id());
        protocol::write_projected_program(q.mutable_program()->mutable_inline_program(), p);
        q.mutable_config()->set_queue_capacity(1);
        CHECK(client.validate_projected(q).accepted());
        auto capture = client.start_projected(q, "sdk-explicit-retry");
        auto retry = client.start_projected(q, "sdk-explicit-retry");
        CHECK(capture.id() == retry.id());
        CHECK(capture.run_id() == retry.run_id());
        auto end = std::chrono::steady_clock::now() + 5s;
        do {
            capture = client.projected_status(capture.id());
            if (!capture.finalization_job_id().empty())
                break;
            std::this_thread::sleep_for(2ms);
        } while (std::chrono::steady_clock::now() < end);
        CHECK(!capture.finalization_job_id().empty());
        client.wait(capture.finalization_job_id());
        CHECK(client.projected_captures().size() == 1);
        CHECK(client.projected_status(capture.id()).committed_bundles() == 2);
        auto same = client.stop_projected(capture.id(), capture.run_id(), capture.generation_id());
        CHECK(same.reason() == wire::v1::PROJECTED_REASON_NONE);
        same = client.cancel_projected(capture.id(), capture.run_id(), capture.generation_id());
        CHECK(same.state() == wire::v1::PROJECTED_COMPLETED);
        auto job = client.replay(capture.raw_artifact_id(), false, true);
        auto verified = client.wait(job);
        CHECK(verified.status().find("passes=2") != std::string::npos);
        auto terminal = client.projected_bundle(job);
        CHECK(terminal);
        CHECK(!terminal->frameset);
        // Fetch a captured publication while paced replay is between frame and terminal.
        // Fixture publication spacing is advanced below by the test-only plugin.
        job = client.replay(capture.raw_artifact_id(), true, false);
        std::optional<d::AcquisitionBundle> frame;
        end = std::chrono::steady_clock::now() + 5s;
        do {
            auto latest = client.projected_bundle(job);
            if (latest && latest->frameset) {
                frame = std::move(latest);
                break;
            }
            std::this_thread::sleep_for(2ms);
        } while (std::chrono::steady_clock::now() < end);
        CHECK(frame);
        auto pixels = frame->frameset->frames[0]->attributes[0].buffer;
        frame.reset();
        auto bytes = pixels.map_read();
        CHECK(bytes && bytes->size() == 4 && (*bytes)[0] == std::byte{42});
        client.wait(job);
        job = client.replay(capture.raw_artifact_id(), true, false);
        end = std::chrono::steady_clock::now() + 5s;
        do {
            auto next = client.projected_bundle(job);
            if (next && next->frameset)
                break;
            std::this_thread::sleep_for(2ms);
        } while (std::chrono::steady_clock::now() < end);
        auto cancelling = std::chrono::steady_clock::now();
        wire::v1::Request cancel;
        cancel.mutable_job_cancel()->set_id(job);
        client.call(cancel);
        bool cancelled{};
        try {
            client.wait(job);
        } catch (const Failure &) {
            cancelled = true;
        }
        CHECK(cancelled);
        CHECK(std::chrono::steady_clock::now() - cancelling < 1s);
        std::cout << "Projected C++ SDK correlation, generation fencing and mapped lease lifetime passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
