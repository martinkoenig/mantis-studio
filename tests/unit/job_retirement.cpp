#include <future>
#include <iostream>
#include <mantis/jobs.hpp>
namespace jobs = mantis::jobs;
#define CHECK(...)                                                                                           \
    do {                                                                                                     \
        if (!(__VA_ARGS__))                                                                                  \
            throw std::runtime_error("Line " + std::to_string(__LINE__) + ": " #__VA_ARGS__);                \
    } while (false)
struct Gate {
    std::mutex mutex;
    std::condition_variable cv;
    bool entered{}, released{};
    void release() {
        std::lock_guard lock(mutex);
        released = true;
        cv.notify_all();
    }
};
struct Unblock {
    Gate &gate;
    ~Unblock() {
        gate.release();
    }
};
struct Permit {
    std::atomic_bool &retired;
    ~Permit() {
        retired = true;
    }
};
int main() {
    try {
        Gate gate;
        std::atomic_bool retired{}, cancelled_retired{}, invoked{};
        jobs::Manager manager([&](const mantis::LogRecord &) {
            std::unique_lock lock(gate.mutex);
            if (gate.entered)
                return;
            gate.entered = true;
            gate.cv.notify_all();
            gate.cv.wait(lock, [&] { return gate.released; });
        });
        std::future<jobs::Snapshot> query;
        std::future<void> cancel;
        Unblock unwind{gate}; // Releases logger before futures or Manager join on assertion failure.
        auto permit = std::make_shared<Permit>(retired);
        auto id = manager.submit(
            "Blocked completion diagnostic",
            [permit](jobs::Context &context) -> std::optional<mantis::artifact::ArtifactReference> {
                CHECK(!context.id.value.empty());
                return {};
            });
        permit.reset();
        {
            std::unique_lock lock(gate.mutex);
            CHECK(gate.cv.wait_for(lock, std::chrono::seconds(2), [&] { return gate.entered; }));
        }
        CHECK(!retired);
        query = std::async(std::launch::async, [&] { return manager.get(id); });
        CHECK(query.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
        CHECK(query.get().state == jobs::State::completed);
        cancel = std::async(std::launch::async, [&] { manager.cancel(id); });
        CHECK(cancel.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
        cancel.get();
        auto queued = std::make_shared<Permit>(cancelled_retired);
        auto next = manager.submit(
            "Cancelled queued owner",
            [queued, &invoked](jobs::Context &) -> std::optional<mantis::artifact::ArtifactReference> {
                invoked = true;
                return {};
            });
        queued.reset();
        manager.cancel(next);
        CHECK(!cancelled_retired);
        gate.release();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (manager.get(next).state != jobs::State::cancelled &&
               std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();
        CHECK(manager.get(next).state == jobs::State::cancelled);
        CHECK(retired && cancelled_retired && !invoked);
        std::cout
            << "Job status/cancel stay available during diagnostic I/O; ownership retires exactly once\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
