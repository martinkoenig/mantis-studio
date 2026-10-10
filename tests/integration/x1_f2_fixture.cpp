#include "../fixtures/x1-f2/program.hpp"
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
template <class T> T take(Result<T> result) {
    if (!result)
        throw Failure(result.error());
    return std::move(*result);
}
void take(Result<void> result) {
    if (!result)
        throw Failure(result.error());
}
std::optional<d::AcquisitionBundle> read(device::ProjectedExecutor &e) {
    auto deadline = std::chrono::steady_clock::now() + 2s;
    do {
        auto b = take(e.next(20));
        if (b)
            return b;
    } while (std::chrono::steady_clock::now() < deadline);
    throw std::runtime_error("F2 publication timeout");
}
int main(int argc, char **argv) {
    try {
        CHECK(argc == 5);
        setenv("MANTIS_X1_PROFILE", argv[3], 1);
        setenv("MANTIS_X1_F2_CONFIG", argv[4], 1);
        setenv("MANTIS_X1_FAKE", "normal", 1);
        unsetenv("MANTIS_X1_F2_SIMULATION");
        unsetenv("MANTIS_X1_F2_FAULT");
        auto fixture = std::make_shared<plugins::Loaded>(argv[1]);
        plugins::Loaded physical(std::filesystem::path(argv[2]) / "mantis-x1.so");
        CHECK(!fixture->api()->query_interface(MANTIS_PROJECTED_LIGHT_V1));
        setenv("MANTIS_X1_F2_SIMULATION", "true", 1);
        CHECK(!fixture->api()->query_interface(MANTIS_PROJECTED_LIGHT_V1));
        setenv("MANTIS_X1_F2_SIMULATION", "1", 1);
        CHECK(!physical.api()->query_interface(MANTIS_PROJECTED_LIGHT_V1));
        unsetenv("MANTIS_X1_FAKE");
        CHECK(!fixture->api()->query_interface(MANTIS_PROJECTED_LIGHT_V1));
        setenv("MANTIS_X1_FAKE", "normal", 1);
        auto graph = plugins::discover_projected_light(*fixture, 100).at(0);
        CHECK(graph.components.size() == 5 && graph.image_participants().size() == 2);
        CHECK(graph.parent.value == "org.mantis.x1:fixture/ov9281 18-0060:fixture/ov9281 20-0060");
        CHECK(graph.components[3].descriptor.id.value == graph.parent.value + "/simulation-f2/L1");
        setenv("MANTIS_X1_FAKE", "renumber", 1);
        CHECK(plugins::discover_projected_light(*fixture, 100).at(0).parent == graph.parent);
        setenv("MANTIS_X1_FAKE", "normal", 1);
        auto p = x1_f2_fixture::program(graph);
        auto open = [&] { return plugins::open_projected_light(fixture, graph.parent, 100); };
        auto camera = fixture->query<MantisAcquisitionV1>(MANTIS_ACQUISITION_V1);
        void *camera_instance{};
        CHECK(camera->open(plugins::host_api(), graph.parent.value.c_str(), &camera_instance) == 0);
        try {
            open();
            throw std::runtime_error("Missing BUSY");
        } catch (const Failure &e) {
            CHECK(e.error.code == Status::busy);
        }
        camera->destroy(camera_instance);
        auto executor = open();
        camera_instance = nullptr;
        CHECK(camera->open(plugins::host_api(), graph.parent.value.c_str(), &camera_instance) == 3 &&
              !camera_instance);
        auto validation = take(executor->validate(p, 100));
        CHECK(validation.accepted);
        auto metrics = nlohmann::json::parse(take(executor->diagnostics(100)));
        CHECK(metrics["run_admissions"] == 0 && metrics["stop_requests"] == 0);
        for (unsigned bad = 0; bad < 8; ++bad) {
            auto q = p;
            if (bad == 0)
                q.repetitions = 2;
            if (bad == 1) {
                auto s = q.steps[0];
                s.index = 18;
                s.label = "second";
                q.steps.push_back(s);
            }
            if (bad == 2)
                q.steps[0].settle = 1ms;
            if (bad == 3)
                q.steps[0].evidence_requirement = d::EvidenceRequirement::controller_acknowledged;
            if (bad == 4)
                q.steps[0].evidence_requirement = d::EvidenceRequirement::exposure_effective;
            if (bad == 5)
                q.steps[0].max_duration = 1ms;
            if (bad == 6)
                q.bounds.max_on_duration = 1ms;
            if (bad == 7)
                q.steps[0].capture.mode = d::CaptureMode::free_running;
            auto result = executor->validate(q, 100);
            CHECK(!result || !result->accepted);
        }
        take(executor->prepare(p, 100));
        take(executor->abort(d::AcquisitionReason::user_cancel, 100));
        CHECK(!executor->start({{"stale-run"}}, {{"stale-generation"}}, 100));
        take(executor->close(100));
        executor.reset();
        for (bool on : {true, false}) {
            executor = open();
            p = x1_f2_fixture::program(graph, on);
            take(executor->prepare(p, 100));
            take(executor->start({{"run-f2"}}, {{"generation-f2"}}, 100));
            auto b = *read(*executor);
            CHECK(!b.frameset && b.triggers.empty() && b.evidence.frames.empty() &&
                  b.evidence.participants.cameras.empty());
            CHECK(b.evidence.step.get()->step_index == 17 && b.evidence.program.id == p.identity.id &&
                  b.evidence.program.hash == p.identity.hash);
            auto &e = b.evidence.emitters.at(0);
            CHECK(e.commanded.get()->state == (on ? d::EmitterState::on : d::EmitterState::off));
            CHECK(e.observed.presence() == d::Presence::unavailable &&
                  e.acknowledged.presence() == d::Presence::unavailable && e.exposure_effective.empty());
            auto terminal = *read(*executor);
            CHECK(terminal.evidence.disposition == d::AcquisitionDisposition::completed);
            CHECK(d::validate_successor(b, terminal));
            metrics = nlohmann::json::parse(take(executor->diagnostics(100)));
            CHECK(metrics["run_admissions"] == (on ? 1 : 0));
            CHECK(metrics["image_copies"] == 0);
            auto off = take(executor->abort(d::AcquisitionReason::user_stop, 100));
            CHECK(off.off_requested.get() && *off.off_requested.get());
            CHECK(off.emitters.size() == 1 && !off.emitters[0].observed.get());
            take(executor->close(100));
            executor.reset();
        }
        // Callback entered before abort retains its borrowed state; destroy refuses until callback retires.
        setenv("MANTIS_X1_F2_FAULT", "blocked", 1);
        executor = open();
        p = x1_f2_fixture::program(graph);
        take(executor->prepare(p, 100));
        take(executor->start({{"pending-run"}}, {{"pending-generation"}}, 100));
        read(*executor);
        auto wait = std::async(std::launch::async, [&] { return executor->next(1000); });
        auto off = take(executor->abort(d::AcquisitionReason::user_cancel, 100));
        CHECK(off.off_requested.get());
        CHECK(!take(wait.get()));
        take(executor->close(100));
        executor.reset();
        unsetenv("MANTIS_X1_F2_FAULT");
        // Entered callback state is pinned across priority abort; destroy must refuse it.
        auto api = fixture->query<MantisProjectedLightV1>(MANTIS_PROJECTED_LIGHT_V1);
        void *raw{};
        CHECK(api->open(plugins::host_api(), graph.parent.value.c_str(), 100, &raw) == 0);
        plugins::semantic::ProgramView pv(p);
        auto validation_callback = [](void *, const MantisProgramValidationV1 *v) {
            return v->accepted ? 0 : 1;
        };
        CHECK(api->prepare(raw, pv.get(), 100, validation_callback, nullptr) == 0);
        CHECK(api->start(raw, "entered-run", "entered-generation", 100) == 0);
        struct Gate {
            std::promise<void> entered, release;
            std::shared_future<void> future = release.get_future().share();
            std::optional<d::AcquisitionBundle> bundle;
        } gate;
        auto entered = gate.entered.get_future();
        auto callback = [](void *ptr, const MantisSemanticPacketV1 *v) noexcept {
            auto &g = *static_cast<Gate *>(ptr);
            return sdk::boundary([&] {
                g.bundle = std::get<d::AcquisitionBundle>(plugins::semantic::packet(v, plugins::host_api()));
                g.entered.set_value();
                g.future.wait();
                CHECK(g.bundle->key.run_id.id.value == "entered-run");
            });
        };
        auto pending_callback =
            std::async(std::launch::async, [&] { return api->next(raw, 1000, callback, &gate); });
        CHECK(entered.wait_for(1s) == std::future_status::ready);
        device::AbortOutcome outcome;
        auto aborted = [](void *ptr, const MantisAbortOutcomeV1 *v) noexcept {
            return sdk::boundary(
                [&] { *static_cast<device::AbortOutcome *>(ptr) = plugins::semantic::abort_outcome(v); });
        };
        CHECK(api->abort(raw, MANTIS_ACQUISITION_REASON_DEVICE_FAILURE, 100, aborted, &outcome) == 0);
        CHECK(outcome.fenced_generation.get()->id.value == "entered-generation" &&
              outcome.off_requested.get());
        CHECK(api->destroy(raw, 0) == MANTIS_PL_BUSY);
        gate.release.set_value();
        CHECK(pending_callback.get() == 0);
        CHECK(api->next(raw, 0, callback, &gate) == MANTIS_PL_NOT_READY);
        // L3 may have no run budget left after abort: all background work is retired already.
        CHECK(api->stop(raw, 0) == 0);
        CHECK(api->destroy(raw, 0) == 0);
        // Physical device/path selectors are outside the complete configuration schema.
        auto config = nlohmann::json::parse(std::ifstream(argv[4]));
        config["serial_device"] = "/dev/ttyUSB0";
        auto bad_path = std::filesystem::path(argv[1]).parent_path() / "rejected-config.json";
        {
            std::ofstream file(bad_path);
            file << config;
        }
        setenv("MANTIS_X1_F2_CONFIG", bad_path.c_str(), 1);
        try {
            open();
            throw std::runtime_error("Physical selector accepted");
        } catch (const Failure &e) {
            CHECK(e.error.code != Status::ok);
        }
        std::filesystem::remove(bad_path);
        // Reject special files before opening them as configuration/transport.
        setenv("MANTIS_X1_F2_CONFIG", "/dev/null", 1);
        try {
            open();
            throw std::runtime_error("Special device file accepted");
        } catch (const Failure &e) {
            CHECK(e.error.code != Status::ok);
        }
        setenv("MANTIS_X1_F2_CONFIG", argv[4], 1);
        for (unsigned i = 0; i < 20; ++i) {
            executor = open();
            take(executor->prepare(p, 100));
            take(executor->start({{"cycle-" + std::to_string(i)}}, {{"gen-" + std::to_string(i)}}, 100));
            take(executor->abort(d::AcquisitionReason::user_cancel, 100));
            take(executor->close(100));
            executor.reset();
        }
        CHECK(camera->open(plugins::host_api(), graph.parent.value.c_str(), &camera_instance) == 0);
        CHECK(camera->start(camera_instance) == 0);
        camera->stop(camera_instance);
        camera->destroy(camera_instance);
        std::cout << "F2 isolated L2 graph/profile/ownership/pure validation/control-only "
                     "evidence/fencing/repeated lifetime passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
