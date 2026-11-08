#include "../fixtures/calibration_api.hpp"
#include <condition_variable>
#include <fstream>
#include <iostream>
#include <mantis/data_io.hpp>
#include <mantis/services.hpp>
#include <nlohmann/json.hpp>
#include <sstream>
#include <sys/stat.h>
#include <thread>
using namespace mantis;
#define CHECK(...)                                                                                           \
    do {                                                                                                     \
        if (!(__VA_ARGS__))                                                                                  \
            throw std::runtime_error("Line " + std::to_string(__LINE__) + ": " #__VA_ARGS__);                \
    } while (false)
struct Gate {
    std::mutex mutex;
    std::condition_variable cv;
    bool armed{}, entered{}, released{};
    void point(std::string_view phase) {
        if (phase != "write")
            return;
        std::unique_lock lock(mutex);
        if (!armed)
            return;
        entered = true;
        cv.notify_all();
        cv.wait(lock, [&] { return released; });
        armed = false;
    }
    void await() {
        std::unique_lock lock(mutex);
        CHECK(cv.wait_for(lock, std::chrono::seconds(10), [&] { return entered; }));
    }
    void release() {
        std::lock_guard lock(mutex);
        released = true;
        cv.notify_all();
    }
};
struct Release {
    Gate &gate;
    ~Release() {
        gate.release();
    }
};
services::X1RoleRequest request(const services::X1RoleInfo &s, bool swapped) {
    return {1, s.generation, s.discovery_token, s.parent_id, swapped};
}
template <class F> void rejects(F fn, Status code) {
    bool rejected = false;
    try {
        fn();
    } catch (const Failure &e) {
        CHECK(e.error.code == code);
        rejected = true;
    }
    CHECK(rejected);
}
jobs::Snapshot complete(services::Runtime &r, const Id &id, bool success = true) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(45);
    while (std::chrono::steady_clock::now() < deadline) {
        for (const auto &j : r.jobs())
            if (j.id == id && j.state != jobs::State::queued && j.state != jobs::State::running) {
                if (r.x1_roles().busy)
                    continue;
                CHECK((j.state == jobs::State::completed) == success);
                if (!success)
                    CHECK(!j.diagnostics.empty());
                return j;
            }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    throw std::runtime_error("X1 Job did not retire");
}
std::string packet_bytes(const data::Packet &packet) {
    std::ostringstream out;
    data::write_packet(out, packet);
    return out.str();
}
int main(int argc, char **argv) {
    auto root = std::filesystem::temp_directory_path() / Id::random().value;
    try {
        CHECK(argc == 3);
        std::filesystem::create_directories(root / "state");
        CHECK(::chmod(root.c_str(), 0700) == 0);
        CHECK(::chmod((root / "state").c_str(), 0700) == 0);
        nlohmann::json profile;
        std::ifstream(argv[2]) >> profile;
        profile["mode"]["width"] = 1280;
        profile["mode"]["height"] = 960;
        profile["mode"]["fps"] = 5;
        profile["max_v4l2_delta_ns"] = 110000000;
        const auto original = profile.dump();
        auto file = root / "profile.json";
        std::ofstream(file) << original;
        setenv("MANTIS_X1_PROFILE", file.c_str(), 1);
        setenv("MANTIS_X1_FAKE", "normal", 1);
        setenv("MANTIS_X1_FAKE_PACE", "1", 1);
        setenv("MANTIS_X1_ROLE_STATE_DIR", (root / "state").c_str(), 1);
        Gate gate;
        std::atomic_bool fail_refresh{}, fail_rollback{};
        services::Configuration config{argv[1],
                                       std::filesystem::path(argv[1]).parent_path() /
                                           "bin/mantis-plugin-host",
                                       root / "project",
                                       {},
                                       {"org.mantis.x1"}};
        config.x1_role_state_dir = root / "state";
        config.x1_role_checkpoint = [&](std::string_view phase) {
            gate.point(phase);
            if ((phase == "refresh" && fail_refresh) || (phase == "rollback" && fail_rollback))
                throw std::runtime_error("Injected " + std::string(phase));
        };
        {
            services::Runtime r(config);
            Release unwind{gate};
            auto initial = r.x1_roles();
            CHECK(initial.supported && !initial.swapped && initial.generation == 0);
            CHECK(initial.cameras[0].identity == "fixture/ov9281 18-0060");
            CHECK(initial.cameras[1].identity == "fixture/ov9281 20-0060");
            auto stale = request(initial, true);
            stale.expected_generation = 9;
            rejects([&] { r.apply_x1_roles(stale); }, Status::incompatible);
            auto invalid = request(initial, true);
            invalid.schema_version = 2;
            rejects([&] { r.validate_x1_roles(invalid); }, Status::incompatible);
            complete(r, r.validate_x1_roles(request(initial, true)));
            initial = r.x1_roles();
            setenv("MANTIS_X1_FAKE", "renumber", 1);
            auto renumbered = complete(r, r.apply_x1_roles(request(r.x1_roles(), true)), false);
            CHECK(renumbered.diagnostics.find("node resolution changed") != std::string::npos);
            CHECK(r.x1_roles().generation == 0 && !r.x1_roles().swapped);
            setenv("MANTIS_X1_FAKE", "normal", 1);
            complete(r, r.validate_x1_roles(request(r.x1_roles(), true)), false);
            initial = r.x1_roles();
            auto store = r.project_store();
            auto f = m6fixture::seed(*store);
            std::vector<calibration::DatasetCamera> cameras{
                {"left",
                 {"fixture/ov9281 18-0060"},
                 1280,
                 960,
                 {{"org.mantis.camera.left.optical"}, "Left optical"}},
                {"right",
                 {"fixture/ov9281 20-0060"},
                 1280,
                 960,
                 {{"org.mantis.camera.right.optical"}, "Right optical"}}};
            auto dataset = m6fixture::checked(calibration::artifacts::create_calibration_dataset(
                *store, m5fixture::fixture(f.charuco.value.target, f.raw_ids, cameras),
                f.charuco.reference()));
            auto left =
                complete(r, r.solve_camera_calibration({dataset.descriptor.id, "left", 3, {}})).result->id;
            auto right =
                complete(r, r.solve_camera_calibration({dataset.descriptor.id, "right", 3, {}})).result->id;
            auto rig = complete(r, r.solve_rig_calibration(
                                       {dataset.descriptor.id, left, right, 3, {{"fixture.rig"}, "Rig"}, {}}))
                           .result->id;
            r.activate_calibration({initial.parent_id}, rig);
            auto capture = r.start_capture({{initial.parent_id}});
            rejects([&] { r.apply_x1_roles(request(r.x1_roles(), true)); }, Status::busy);
            auto stopped = r.stop_capture(capture.id);
            CHECK(stopped.error.empty());
            complete(r, stopped.finalization_job);
            const auto raw_before = store->get(stopped.raw_artifact);
            std::vector<std::string> recorded;
            store->replay(stopped.raw_artifact,
                          [&](data::Published p) { recorded.push_back(packet_bytes(*p)); });
            CHECK(!recorded.empty());
            auto rejected = complete(r, r.apply_x1_roles(request(r.x1_roles(), true)), false);
            CHECK(rejected.diagnostics.find("Clear the current X1 active rig") != std::string::npos);
            CHECK(r.active_calibration({initial.parent_id})->artifact.id == rig);
            CHECK(!std::filesystem::exists(root / "state/x1-role-map.v1.json"));
            r.clear_calibration({initial.parent_id});
            auto before_swap = r.x1_roles();
            {
                std::lock_guard lock(gate.mutex);
                gate.armed = true;
            }
            auto apply = r.apply_x1_roles(request(before_swap, true));
            gate.await();
            CHECK(r.x1_roles().busy);
            CHECK(!r.devices().empty());
            CHECK(!r.jobs().empty());
            rejects([&] { r.start_capture({{before_swap.parent_id}}); }, Status::busy);
            rejects([&] { r.open_project(root / "other", true); }, Status::busy);
            rejects([&] { r.activate_calibration({before_swap.parent_id}, rig); }, Status::busy);
            rejects([&] { r.validate_projected({}); }, Status::busy);
            r.cancel_job(apply);
            CHECK(r.x1_roles().busy); // Cancellation does not release executing I/O.
            gate.release();
            complete(r, apply);
            auto after = r.x1_roles();
            CHECK(after.swapped && after.generation == 1 && !after.blocked);
            CHECK(after.parent_id == "org.mantis.x1:fixture/ov9281 20-0060:fixture/ov9281 18-0060");
            rejects([&] { r.apply_x1_roles(request(before_swap, true)); }, Status::incompatible);
            rejects([&] { r.activate_calibration({after.parent_id}, rig); }, Status::incompatible);
            auto next = r.start_capture({{after.parent_id}});
            auto next_stop = r.stop_capture(next.id);
            CHECK(next_stop.error.empty());
            complete(r, next_stop.finalization_job);
            auto packet = store->packet(next_stop.raw_artifact);
            CHECK(packet->frames.size() == 2);
            for (const auto &image : packet->frames) {
                const auto role = image->header.metadata.at("role");
                const auto physical = image->header.metadata.at("identity");
                CHECK(physical == (role == "left" ? "fixture/ov9281 20-0060" : "fixture/ov9281 18-0060"));
                CHECK(image->header.frame.id.value == "org.mantis.camera." + role + ".optical");
                const auto native = static_cast<unsigned>(image->header.sequence.value);
                const auto expected = (native * 7u + (role == "left" ? 97u : 0u)) & 255u;
                CHECK(std::to_integer<unsigned>(
                          m6fixture::checked(image->attributes[0].buffer.map_read())[0]) == expected);
            }
            std::vector<std::string> replayed;
            store->replay(stopped.raw_artifact,
                          [&](data::Published p) { replayed.push_back(packet_bytes(*p)); });
            CHECK(replayed == recorded);
            CHECK(store->get(stopped.raw_artifact).hash == raw_before.hash);
            fail_refresh = true;
            complete(r, r.apply_x1_roles(request(r.x1_roles(), false)), false);
            CHECK(r.x1_roles().swapped && r.x1_roles().generation == 1 && !r.x1_roles().blocked);
            fail_rollback = true;
            complete(r, r.apply_x1_roles(request(r.x1_roles(), false)), false);
            CHECK(r.x1_roles().blocked && r.devices().empty());
            rejects([&] { r.start_capture({{after.parent_id}}); }, Status::incompatible);
        }
        CHECK(std::ifstream(file).good());
        std::ifstream in(file);
        std::string unchanged((std::istreambuf_iterator<char>(in)), {});
        CHECK(unchanged == original);
        std::filesystem::remove_all(root);
        std::cout << "X1 asynchronous reservation, rig guard, physical packets, rollback and immutable "
                     "replay passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
        return 1;
    }
}
