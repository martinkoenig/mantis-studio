#pragma once
#include <chrono>
#include <filesystem>
#include <string>
namespace mantis::artifact::storage_test {
enum class Action {
    fail,
    short_write,
    pause,
    short_pause,
    reject_commit,
    syscall_error,
    obstruct_publication
};
struct Plan {
    std::filesystem::path root;
    std::string id, point;
    Action action{Action::fail};
    uint64_t skip{}, bytes{};
    int error{}, notify{-1}, release{-1}; // Borrowed self-owned test pipes; no callbacks.
};
struct Event {
    char point[64]{}, id[257]{};
};
struct Metrics {
    uint64_t hits{}, written{}, short_results{}, rejected_commits{};
    int error{};
};
// Borrowed pipe/control lifetimes require all matched Store operations to retire
// before Arm destruction. Distinct artifacts/roots may be armed concurrently.
class Arm {
    size_t slot_;

  public:
    explicit Arm(Plan);
    ~Arm();
    Arm(const Arm &) = delete;
    Metrics metrics() const;
};
Event await_event(int, std::chrono::steady_clock::time_point);
void release_gate(int);
} // namespace mantis::artifact::storage_test
