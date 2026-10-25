#include "../fixtures/x1-projected/program.hpp"
#include <fstream>
#include <future>
#include <iostream>
#include <mantis/plugin_runtime.hpp>
#include <mantis/semantic_views.hpp>
#include <nlohmann/json.hpp>
using namespace mantis;
using namespace std::chrono_literals;
namespace d = mantis::data;
#define CHECK(...)                                                                                           \
    do {                                                                                                     \
        if (!(__VA_ARGS__))                                                                                  \
            throw std::runtime_error("Line " + std::to_string(__LINE__) + ": " #__VA_ARGS__);                \
    } while (false)
template <class T> T take(Result<T> v) {
    if (!v)
        throw Failure(v.error());
    return std::move(*v);
}
void take(Result<void> v) {
    if (!v)
        throw Failure(v.error());
}
template <class F> void busy(F f) {
    try {
        f();
    } catch (const Failure &e) {
        CHECK(e.error.code == Status::busy);
        return;
    }
    throw std::runtime_error("Expected BUSY");
}
std::optional<d::AcquisitionBundle> read(device::ProjectedExecutor &e) {
    for (size_t i = 0; i < 100; ++i) {
        auto b = take(e.next(20));
        if (b)
            return b;
    }
    throw std::runtime_error("No fixture bundle");
}
void images(const d::AcquisitionBundle &b, uint64_t sequence) {
    CHECK(b.frameset && b.frameset->header.sequence.value == sequence && b.frameset->frames.size() == 2);
    CHECK(b.evidence.frames.size() == 2 && b.evidence.frameset.get());
    for (size_t i = 0; i < 2; ++i) {
        auto image = b.frameset->frames[i];
        const auto &f = b.evidence.frames[i];
        CHECK(f.frame.native_sequence == image->header.sequence.value);
        CHECK(f.source_timestamp.get()->nanoseconds == image->header.timestamp.nanoseconds);
        CHECK(f.source_timestamp.get()->clock.domain.id == image->header.timestamp.domain.id);
        CHECK(f.host_received.get()->time.nanoseconds == image->header.received.nanoseconds);
        CHECK(f.exposure.presence() == d::Presence::unavailable);
        CHECK(f.sync.get()->quality == time::SyncQuality::software);
        CHECK(!f.sync.get()->hardware_association.get());
        auto bytes = take(image->attributes[0].buffer.map_read());
        CHECK(bytes.size() == 64 * 48 * 5 / 4);
        for (size_t k = 0; k < bytes.size(); ++k)
            CHECK(bytes[k] == std::byte((k + image->header.sequence.value * 7 + (i ? 97 : 0)) & 255));
        CHECK(image->header.metadata.at("fourcc") == "Y10P");
    }
    for (const auto &v : b.evidence.emitters) {
        CHECK(v.commanded.get());
        CHECK(v.acknowledged.presence() == d::Presence::unavailable);
        CHECK(v.observed.presence() == d::Presence::unavailable);
        CHECK(v.exposure_effective.size() == 2);
        for (const auto &e : v.exposure_effective)
            CHECK(e.state.presence() == d::Presence::unavailable);
    }
    CHECK(b.triggers.empty());
}
int main(int argc, char **argv) {
    try {
        CHECK(argc == 4);
        setenv("MANTIS_X1_PROFILE", argv[3], 1);
        setenv("MANTIS_X1_FAKE", "normal", 1);
        setenv("MANTIS_X1_FAKE_PACE", "1", 1);
        unsetenv("MANTIS_X1_PROJECTED_FIXTURE");
        unsetenv("MANTIS_X1_PROJECTED_FAULT");
        auto fixture = std::make_shared<plugins::Loaded>(argv[1]);
        plugins::Loaded real(std::filesystem::path(argv[2]) / "mantis-x1.so");
        CHECK(!fixture->api()->query_interface(MANTIS_PROJECTED_LIGHT_V1));
        setenv("MANTIS_X1_PROJECTED_FIXTURE", "1", 1);
        CHECK(!real.api()->query_interface(MANTIS_PROJECTED_LIGHT_V1));
        unsetenv("MANTIS_X1_FAKE");
        CHECK(!fixture->api()->query_interface(MANTIS_PROJECTED_LIGHT_V1));
        setenv("MANTIS_X1_FAKE", "normal", 1);

        auto graph = plugins::discover_projected_light(*fixture, 100).at(0);
        CHECK(graph.components.size() == 6);
        CHECK(graph.image_participants().size() == 2);
        CHECK(graph.parent.value == "org.mantis.x1:fixture/ov9281 18-0060:fixture/ov9281 20-0060");
        CHECK(graph.components[1].descriptor.id.value == graph.parent.value + "/left");
        CHECK(graph.components[2].descriptor.id.value == graph.parent.value + "/right");
        CHECK(graph.components[1].image_source->physical_identity == "fixture/ov9281 18-0060");
        CHECK(graph.limits.watchdog.presence() == d::Presence::unavailable &&
              graph.limits.interlock.presence() == d::Presence::unavailable &&
              graph.limits.fail_off.presence() == d::Presence::unavailable);
        for (const auto &c : graph.components)
            CHECK(!device::has_capability(c.descriptor, MANTIS_HARDWARE_TRIGGER_V1));
        setenv("MANTIS_X1_FAKE", "renumber", 1);
        CHECK(plugins::discover_projected_light(*fixture, 100)[0].parent == graph.parent);
        setenv("MANTIS_X1_FAKE", "normal", 1);
        auto p = x1_fixture::program(graph);
        auto open = [&] { return plugins::open_projected_light(fixture, graph.parent, 100); };
        // Both interfaces share private ownership, in addition to L5's parent gate.
        auto camera = fixture->query<MantisAcquisitionV1>(MANTIS_ACQUISITION_V1);
        void *instance{};
        CHECK(camera->open(plugins::host_api(), graph.parent.value.c_str(), &instance) == 0);
        busy([&] { return open(); });
        camera->destroy(instance);
        // Abort invalidates an already prepared snapshot before start can re-enable it.
        auto prepared = open();
        take(prepared->prepare(p, 100));
        take(prepared->abort(d::AcquisitionReason::user_cancel, 100));
        CHECK(!prepared->start({{"fenced-before-start"}}, {{"fenced-before-start-gen"}}, 100));
        take(prepared->close(100));
        prepared.reset();
        auto executor = open();
        instance = nullptr;
        CHECK(camera->open(plugins::host_api(), graph.parent.value.c_str(), &instance) == MANTIS_PL_BUSY &&
              !instance);
        busy([&] { return open(); });
        take(executor->prepare(p, 100));
        take(executor->start({{"fixture-run"}}, {{"fixture-generation"}}, 100));
        CHECK(!executor->start({{"duplicate"}}, {{"duplicate"}}, 100));
        for (uint64_t i = 0; i < 5; ++i) {
            auto b = *read(*executor);
            images(b, i);
            CHECK(b.key.sequence.value == i && b.key.run_id.id.value == "fixture-run");
            CHECK(b.evidence.program.id == p.identity.id && b.evidence.program.hash == p.identity.hash);
            CHECK(b.evidence.step.get()->step_index == p.steps[i].index);
            for (size_t j = 0; j < 2; ++j)
                CHECK(b.evidence.emitters[j].commanded.get()->state == p.steps[i].emitters[j].state);
        }
        CHECK(read(*executor)->evidence.disposition == d::AcquisitionDisposition::completed);
        auto diagnostics = nlohmann::json::parse(take(executor->diagnostics(100)));
        CHECK(diagnostics["copy_count"] == "1" && diagnostics["projected_pixel_copies"] == "0");
        CHECK(diagnostics["acquisition_copy_frames"] == "10");
        CHECK(diagnostics["acquisition_copy_bytes"] == "38400");
        CHECK(std::stoul(diagnostics["pending_high_water_left"].get<std::string>()) <= 2);
        take(executor->stop(100));
        take(executor->close(100));
        executor.reset();
        // Declaration order survives exactly; image order remains the accepted X1 FrameSet order.
        auto reordered = p;
        std::reverse(reordered.participants.cameras.begin(), reordered.participants.cameras.end());
        std::reverse(reordered.participants.emitters.begin(), reordered.participants.emitters.end());
        executor = open();
        take(executor->prepare(reordered, 100));
        take(executor->start({{"reordered"}}, {{"reordered-gen"}}, 100));
        auto ordered = *read(*executor);
        images(ordered, 0);
        CHECK(ordered.evidence.participants.cameras[0].component ==
              reordered.participants.cameras[0].component);
        CHECK(ordered.evidence.participants.emitters == reordered.participants.emitters);
        take(executor->close(100));
        executor.reset();
        // Camera-only mode still uses identical native pixels/headers and pairing.
        CHECK(camera->open(plugins::host_api(), graph.parent.value.c_str(), &instance) == 0);
        CHECK(camera->start(instance) == 0);
        struct Compare {
            uint64_t n{};
        } compare;
        auto emit = [](void *v, const MantisFrameSetV1 *fs) noexcept {
            return sdk::boundary([&] {
                auto &c = *static_cast<Compare *>(v);
                CHECK(fs->observation.packet.sequence == c.n);
                CHECK(fs->frame_count == 2);
                for (size_t i = 0; i < 2; ++i) {
                    const auto &f = fs->frames[i];
                    CHECK(f.packet.sequence == c.n);
                    CHECK(f.packet.device_time_ns == int64_t(c.n) * (1000000000 / 120));
                    CHECK(f.packet.attribute_count == 1);
                }
                ++c.n;
            });
        };
        while (compare.n < 5) {
            auto rc = camera->next(instance, 20, emit, &compare);
            CHECK(rc == 0 || rc == 2);
        }
        CHECK(camera->stop(instance) == 0);
        camera->destroy(instance);
        // Direct ABI tests hold a callback open: abort bypasses it, destroy refuses,
        // then callbacks quiesce and ownership can be reused. No new ON after fence.
        setenv("MANTIS_X1_PROJECTED_FAULT", "block", 1);
        executor = open();
        take(executor->prepare(p, 100));
        take(executor->start({{"blocked"}}, {{"blocked-gen"}}, 100));
        auto pending = std::async(std::launch::async, [&] { return executor->next(1000); });
        std::this_thread::sleep_for(20ms);
        auto begun = std::chrono::steady_clock::now();
        auto off = take(executor->abort(d::AcquisitionReason::user_cancel, 100));
        CHECK(std::chrono::steady_clock::now() - begun < 100ms);
        CHECK(off.inhibited.get() && *off.inhibited.get() && off.emitters.size() == 2);
        CHECK(take(pending.get()) == std::nullopt);
        for (const auto &e : off.emitters) {
            CHECK(e.commanded.get()->state == d::EmitterState::off);
            CHECK(!e.observed.get());
        }
        CHECK(!take(executor->next(0)));
        take(executor->stop(100));
        take(executor->close(100));
        executor.reset();
        unsetenv("MANTIS_X1_PROJECTED_FAULT");
        setenv("MANTIS_X1_FAKE", "stall-left", 1);
        executor = open();
        take(executor->prepare(p, 100));
        take(executor->start({{"camera-wait"}}, {{"camera-wait-gen"}}, 100));
        pending = std::async(std::launch::async, [&] { return executor->next(1000); });
        std::this_thread::sleep_for(20ms);
        begun = std::chrono::steady_clock::now();
        take(executor->abort(d::AcquisitionReason::device_failure, 100));
        CHECK(std::chrono::steady_clock::now() - begun < 100ms);
        CHECK(!take(pending.get()));
        take(executor->close(100));
        executor.reset();
        setenv("MANTIS_X1_FAKE", "normal", 1);
        // Hold an entered synchronous callback across abort and an attempted destroy.
        // Its borrowed state must remain valid; after it returns no later bundle enters.
        auto api = fixture->query<MantisProjectedLightV1>(MANTIS_PROJECTED_LIGHT_V1);
        void *raw{};
        CHECK(api->open(plugins::host_api(), graph.parent.value.c_str(), 100, &raw) == 0);
        plugins::semantic::ProgramView program_view(p);
        auto validated = [](void *, const MantisProgramValidationV1 *v) { return v->accepted ? 0 : 1; };
        CHECK(api->prepare(raw, program_view.get(), 100, validated, nullptr) == 0);
        CHECK(api->start(raw, "late-run", "late-generation", 100) == 0);
        struct Late {
            std::promise<void> entered, release;
            std::shared_future<void> gate = release.get_future().share();
            std::optional<d::AcquisitionBundle> bundle;
        } late;
        auto entered = late.entered.get_future();
        auto callback = [](void *v, const MantisSemanticPacketV1 *packet) noexcept {
            auto &state = *static_cast<Late *>(v);
            return sdk::boundary([&] {
                state.bundle =
                    std::get<d::AcquisitionBundle>(plugins::semantic::packet(packet, plugins::host_api()));
                state.entered.set_value();
                state.gate.wait();
                CHECK(state.bundle->key.run_id.id.value == "late-run");
            });
        };
        auto future = std::async(std::launch::async, [&] { return api->next(raw, 1000, callback, &late); });
        CHECK(entered.wait_for(1s) == std::future_status::ready);
        device::AbortOutcome outcome;
        auto aborted = [](void *v, const MantisAbortOutcomeV1 *o) noexcept {
            return sdk::boundary(
                [&] { *static_cast<device::AbortOutcome *>(v) = plugins::semantic::abort_outcome(o); });
        };
        begun = std::chrono::steady_clock::now();
        CHECK(api->abort(raw, MANTIS_ACQUISITION_REASON_DEVICE_FAILURE, 100, aborted, &outcome) == 0);
        CHECK(std::chrono::steady_clock::now() - begun < 100ms &&
              outcome.fenced_generation.get()->id.value == "late-generation");
        CHECK(api->destroy(raw, 0) == MANTIS_PL_BUSY);
        late.release.set_value();
        CHECK(future.get() == 0);
        CHECK(api->next(raw, 0, callback, &late) == MANTIS_PL_NOT_READY);
        CHECK(api->abort(raw, MANTIS_ACQUISITION_REASON_USER_CANCEL, 100, aborted, &outcome) == 0);
        std::string diagnostic;
        auto text = [](void *v, const char *s) noexcept {
            return sdk::boundary([&] { *static_cast<std::string *>(v) = s; });
        };
        CHECK(api->diagnostics(raw, 100, text, &diagnostic) == 0);
        CHECK(nlohmann::json::parse(diagnostic)["first_abort_reason"] == "3");
        CHECK(api->destroy(raw, 100) == 0);
        executor = open();
        take(executor->prepare(p, 100));
        take(executor->start({{"new-run"}}, {{"new-generation"}}, 100));
        auto fresh = *read(*executor);
        CHECK(fresh.evidence.frames[0].frame.stream.generation !=
              late.bundle->evidence.frames[0].frame.stream.generation);
        take(executor->close(100));
        executor.reset();
        // Failed cleanup keeps exact software OFF evidence; close refusal retains ownership.
        setenv("MANTIS_X1_PROJECTED_FAULT", "cleanup", 1);
        executor = open();
        take(executor->prepare(p, 100));
        take(executor->start({{"cleanup"}}, {{"cleanup-gen"}}, 100));
        off = take(executor->abort(d::AcquisitionReason::device_failure, 100));
        CHECK(off.error.category == MANTIS_ERROR_CLEANUP);
        CHECK(off.off_requested.get() && *off.off_requested.get());
        take(executor->close(100));
        executor.reset();
        setenv("MANTIS_X1_PROJECTED_FAULT", "close", 1);
        executor = open();
        CHECK(!executor->close(100));
        busy([&] { return open(); });
        take(executor->close(100));
        executor.reset();
        unsetenv("MANTIS_X1_PROJECTED_FAULT");
        auto begun_perf = std::chrono::steady_clock::now();
        uint64_t frames{};
        for (unsigned repeat = 0; repeat < 10; ++repeat) {
            executor = open();
            take(executor->prepare(p, 100));
            take(executor->start({{"repeat-" + std::to_string(repeat)}},
                                 {{"repeat-gen-" + std::to_string(repeat)}}, 100));
            for (unsigned i = 0; i < 5; ++i) {
                images(*read(*executor), i);
                ++frames;
            }
            take(executor->abort(d::AcquisitionReason::user_stop, 100));
            take(executor->stop(100));
            take(executor->close(100));
            executor.reset();
        }
        auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - begun_perf).count();
        std::cout << "X1 fixture: " << frames << " FrameSets in " << elapsed << "s; "
                  << double(frames) / elapsed
                  << " FrameSets/s; one acquisition copy/image, zero projected copies, no image encoding\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
