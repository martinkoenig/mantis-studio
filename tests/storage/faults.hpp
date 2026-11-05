#pragma once
#include <array>
#include <chrono>
#include <filesystem>
#include <functional>
#include <iosfwd>
#include <string>
#include <string_view>
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
// Passive, bounded qualification evidence. Read samples only after matched I/O
// has retired. Never enabled in a product binary; one serial trace at a time.
struct StageSample {
    char point[64]{};
    int64_t ns{};
};
class Trace {
  public:
    std::filesystem::path root;
    std::string id;
    std::array<StageSample, 4096> samples{};
    size_t count{}, overflow{};
    Trace(std::filesystem::path, std::string);
    ~Trace();
    Trace(const Trace &) = delete;
};
struct StreamCost {
    int64_t ordinary_ns{}, qualified_ns{};
    uint64_t bytes{};
};
// Diagnostic microprobe only: identical real packet encoding, ordinary versus
// qualified stream, same owned filesystem directory. No fsync/capacity claim.
StreamCost stream_cost(const std::filesystem::path &, const std::function<void(std::ostream &)> &);
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
void checkpoint(std::string_view, const std::filesystem::path &, std::string_view);
Event await_event(int, std::chrono::steady_clock::time_point);
void release_gate(int);
} // namespace mantis::artifact::storage_test
