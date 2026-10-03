#pragma once
#include <condition_variable>
#include <deque>
#include <mantis/artifact_api.hpp>
#include <mutex>
#include <thread>
namespace mantis::jobs {
enum class State { queued, running, completed, failed, cancelled };
inline std::string state_name(State s) {
    switch (s) {
    case State::queued:
        return "Queued";
    case State::running:
        return "Running";
    case State::completed:
        return "Completed";
    case State::failed:
        return "Failed";
    case State::cancelled:
        return "Cancelled";
    }
    return "Unknown";
}
struct Snapshot {
    Id id;
    std::string name;
    State state{State::queued};
    double progress{};
    std::string status, diagnostics;
    std::optional<artifact::ArtifactReference> result;
};
struct Context {
    CancellationToken cancellation;
    std::function<void(double, std::string)> update;
};
using Task = std::function<std::optional<artifact::ArtifactReference>(Context &)>;
class Manager {
    struct Job {
        Snapshot snapshot;
        CancellationToken cancel;
        Task task;
    };
    mutable std::mutex mutex_;
    std::condition_variable_any ready_;
    std::map<Id, std::shared_ptr<Job>> jobs_;
    std::deque<std::shared_ptr<Job>> queue_;
    std::jthread worker_;
    LogSink logger_;

  public:
    explicit Manager(LogSink = {});
    ~Manager();
    Id submit(std::string, Task);
    std::vector<Snapshot> list() const;
    Snapshot get(const Id &) const;
    void cancel(const Id &);
    bool busy() const;
};
} // namespace mantis::jobs
