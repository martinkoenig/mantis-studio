#include "../../plugins/first-party/devices/x1/role_config.hpp"
#include "../fixtures/x1-projected/program.hpp"
#include <algorithm>
#include <condition_variable>
#include <future>
#include <iostream>
#include <mantis/artifact_store.hpp>
#include <mantis/services.hpp>
#include <mutex>
#include <sys/stat.h>
#include <vector>
using namespace mantis;
using namespace std::chrono_literals;
#define CHECK(...)                                                                                           \
    do {                                                                                                     \
        if (!(__VA_ARGS__))                                                                                  \
            throw std::runtime_error("Line " + std::to_string(__LINE__) + ": " #__VA_ARGS__);                \
    } while (false)
namespace {
constexpr const char *primary_recording_error = "Injected recorder finalization failure";
struct Gates {
    std::mutex mutex;
    std::condition_variable changed;
    std::vector<std::string> seen;
    bool record_entered{}, record_done{}, role_entered{}, role_done{};
    bool record_release{}, role_release{}, manager_release{}, destroy_release{}, fail_record{};
    bool has(std::string_view phase) const {
        return std::find(seen.begin(), seen.end(), phase) != seen.end();
    }
    void point(std::string_view phase) {
        std::unique_lock lock(mutex);
        seen.emplace_back(phase);
        if (phase == "projected.recorder.body.retired")
            record_done = true;
        if (phase == "role.body.retired")
            role_done = true;
        changed.notify_all();
        if (phase == "projected.finalize.submit") {
            record_entered = true;
            changed.notify_all();
            changed.wait(lock, [&] { return record_release; });
            if (fail_record) {
                lock.unlock();
                throw std::runtime_error(primary_recording_error);
            }
        } else if (phase == "role.before-quiescent") {
            role_entered = true;
            changed.notify_all();
            changed.wait(lock, [&] { return role_release; });
        } else if (phase == "shutdown.manager.before-retire") {
            // The old-order negative control must fail without freeing Manager
            // underneath its gated producer during assertion unwind.
            changed.wait(lock, [&] { return manager_release && (!record_entered || record_done); });
        } else if (phase == "shutdown.projected.before-destroy") {
            // Likewise retain the actual projected object if the opposite bad
            // reset order is tested while a role Job still needs its snapshot.
            changed.wait(lock, [&] { return destroy_release && (!role_entered || role_done); });
        }
    }
    void await(std::string_view phase) {
        std::unique_lock lock(mutex);
        if (!changed.wait_for(lock, 15s, [&] { return has(phase); }))
            throw std::runtime_error("Missing owned shutdown checkpoint: " + std::string(phase));
    }
    std::string first_shutdown_boundary(bool include_quiesce = true) {
        std::unique_lock lock(mutex);
        auto boundary = [&](const std::string &p) {
            return (include_quiesce && p == "projected.quiesce") || p == "shutdown.manager.before-retire" ||
                   p == "shutdown.projected.before-destroy";
        };
        CHECK(changed.wait_for(lock, 15s, [&] { return std::any_of(seen.begin(), seen.end(), boundary); }));
        return *std::find_if(seen.begin(), seen.end(), boundary);
    }
    void release_record() {
        std::lock_guard lock(mutex);
        record_release = true;
        changed.notify_all();
    }
    void release_all() {
        std::lock_guard lock(mutex);
        record_release = role_release = manager_release = destroy_release = true;
        changed.notify_all();
    }
    void verify_retirement_order() {
        std::lock_guard lock(mutex);
        const auto position = [&](std::string_view p) {
            auto it = std::find(seen.begin(), seen.end(), p);
            CHECK(it != seen.end());
            return std::distance(seen.begin(), it);
        };
        CHECK(position("projected.producers.retired") < position("shutdown.manager.before-retire"));
        CHECK(position("shutdown.manager.retired") < position("shutdown.projected.before-destroy"));
        if (role_entered)
            CHECK(position("role.body.retired") < position("shutdown.manager.retired"));
    }
};
struct Release {
    Gates &gates;
    ~Release() {
        gates.release_all();
    }
};
struct Fixture {
    std::filesystem::path root = std::filesystem::temp_directory_path() / Id::random().value;
    std::filesystem::path state = root / "state", profile = root / "profile.json";
    std::filesystem::path plugins;
    Fixture(const std::filesystem::path &plugin_path, const std::filesystem::path &source)
        : plugins(plugin_path) {
        std::filesystem::create_directories(state);
        CHECK(::chmod(root.c_str(), 0700) == 0 && ::chmod(state.c_str(), 0700) == 0);
        std::filesystem::copy_file(source, profile);
        CHECK(::setenv("MANTIS_X1_PROFILE", profile.c_str(), 1) == 0);
        CHECK(::setenv("MANTIS_X1_ROLE_STATE_DIR", state.c_str(), 1) == 0);
    }
    ~Fixture() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }
    std::unique_ptr<services::Runtime> runtime(Gates &gates) {
        services::Configuration config{plugins, {}, root / "project", {}, {"org.mantis.x1"}};
        config.x1_role_state_dir = state;
        config.x1_role_checkpoint = [&](std::string_view p) { gates.point(p); };
        return std::make_unique<services::Runtime>(config);
    }
};
void projected_shutdown(const std::filesystem::path &plugins, const std::filesystem::path &profile,
                        bool fail_recording) {
    Gates gates;
    gates.fail_record = fail_recording;
    Fixture fixture(plugins, profile);
    auto runtime = fixture.runtime(gates);
    auto store = runtime->project_store();
    std::future<void> shutdown;
    Release unwind{gates}; // Opens owned gates before future/Runtime destruction can join.
    const auto devices = runtime->projected_devices();
    CHECK(devices.size() == 1 && devices[0].plugin_id == "org.mantis.x1");
    services::ProjectedCaptureRequest request;
    request.plugin_id = devices[0].plugin_id;
    request.parent = devices[0].graph.parent;
    request.program = x1_fixture::program(devices[0].graph);
    auto capture = runtime->start_projected(request, "role-enabled-shutdown");
    gates.await("projected.finalize.submit");
    if (fail_recording) {
        gates.release_record();
        gates.await("projected.recorder.body.retired");
        const auto failed = runtime->projected_status(capture.id);
        CHECK(failed.recording_error && failed.recording_error->message == primary_recording_error);
        CHECK(failed.storage_state == artifact::ArtifactState::recoverable);
        gates.release_all();
        runtime.reset();
        CHECK(store->get(capture.raw_artifact).state == artifact::ArtifactState::recoverable);
    } else {
        shutdown = std::async(std::launch::async, [&] { runtime.reset(); });
        // Positive entry observations, not sleep/deadline ordering assumptions.
        CHECK(gates.first_shutdown_boundary() == "projected.quiesce");
        gates.release_record();
        CHECK(gates.first_shutdown_boundary(false) == "shutdown.manager.before-retire");
        gates.await("shutdown.manager.before-retire");
        {
            std::lock_guard lock(gates.mutex);
            CHECK(gates.record_done && gates.has("projected.producers.retired"));
        }
        gates.release_all();
        shutdown.get();
        const auto result = store->get(capture.raw_artifact);
        CHECK(result.state == artifact::ArtifactState::finalized ||
              result.state == artifact::ArtifactState::recoverable);
    }
    const auto preserved = store->get(capture.raw_artifact);
    CHECK(preserved.bytes > 0);
    // Shutdown may cancel queued finalization. Verify the retained prefix through
    // the explicit recovery API before using the finalized-only replay reader.
    if (preserved.state == artifact::ArtifactState::recoverable)
        CHECK(store->recover(capture.raw_artifact).state == artifact::ArtifactState::finalized);
    uint64_t bundles{};
    store->replay_bundles(capture.raw_artifact, [&](data::AcquisitionBundle) { ++bundles; });
    CHECK(bundles > 0);
    gates.verify_retirement_order();
}
void role_job_shutdown(const std::filesystem::path &plugins, const std::filesystem::path &profile) {
    Gates gates;
    Fixture fixture(plugins, profile);
    auto runtime = fixture.runtime(gates);
    std::future<void> shutdown;
    Release unwind{gates};
    const auto state = runtime->x1_roles();
    runtime->apply_x1_roles({1, state.generation, state.discovery_token, state.parent_id, true});
    gates.await("role.before-quiescent");
    shutdown = std::async(std::launch::async, [&] { runtime.reset(); });
    CHECK(gates.first_shutdown_boundary() == "projected.quiesce");
    // Manager has closed admission/requested cancellation before this checkpoint.
    // The role task still owns its reservation and has yet to inspect ProjectedSessions.
    CHECK(gates.first_shutdown_boundary(false) == "shutdown.manager.before-retire");
    gates.release_all();
    shutdown.get();
    gates.verify_retirement_order();
    x1::roles::Store reopened(fixture.state, fixture.profile);
    CHECK(reopened.mapping() == x1::roles::Mapping{1, true});
}
} // namespace
int main(int argc, char **argv) {
    try {
        CHECK(argc == 3 || (argc == 4 && std::string_view(argv[3]) == "role-only"));
        CHECK(::setenv("MANTIS_X1_PROJECTED_FIXTURE", "1", 1) == 0);
        CHECK(::setenv("MANTIS_X1_FAKE", "normal", 1) == 0);
        CHECK(::setenv("MANTIS_X1_FAKE_PACE", "1", 1) == 0);
        if (argc == 3) {
            projected_shutdown(argv[1], argv[2], false);
            projected_shutdown(argv[1], argv[2], true);
        }
        role_job_shutdown(argv[1], argv[2]);
        std::cout << "Recorder/Manager/role retirement ordering and primary recoverable failure passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
