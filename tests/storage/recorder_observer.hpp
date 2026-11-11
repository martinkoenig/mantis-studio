#pragma once
// Issue #12 only: private, opt-in actual-acceptance observer. Never installed.
#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <new>
#include <string_view>
#include <utility>
namespace mantis::recorder_observer {
inline constexpr uint64_t no_item = UINT64_MAX;
enum class Kind : uint64_t {
    capture_state = 1,
    source_wake = 2,
    source_next = 3,
    source_diagnostics = 4,
    preview = 5,
    admission = 6,
    admission_result = 7,
    source_sleep = 8,
    queue_pop = 10,
    writer_service = 11,
    callback = 12,
    payload = 13,
    session_lock_wait = 14,
    accounting = 15,
    store_total = 20,
    store_lock_wait = 21,
    store_dispatch = 22,
    packet_write = 23,
    packet_sync = 24,
    packet_hash = 25,
    journal_write = 26,
    journal_sync = 27,
    journal_rename = 28,
    journal_dir_sync = 29,
    packet_rename = 30,
    packet_dir_sync = 31,
    sql_commit = 32,
    journal_cleanup = 33,
    cleanup_dir_sync = 34,
    first_failure = 40,
    cleanup = 41,
    producer_end = 42,
    writer_end = 43,
    queue_sample = 44,
    queue_counts = 45,
    queue_timing = 46,
    observer_sample = 50
};
// Four single-writer lanes: control, producer, writer, observer. Integer records
// only on hot paths. No callback, allocation, new mutex or filesystem access.
void capture(const void *, std::string_view id, uint64_t streams, bool source_paced) noexcept;
void bind(const void *, unsigned lane) noexcept;
void unbind() noexcept;
class Scope {
    unsigned previous_;

  public:
    Scope(const void *, unsigned lane) noexcept;
    ~Scope();
};
void raw(std::string_view id, uint64_t schema) noexcept;
void source_mode(bool) noexcept;
void unavailable() noexcept;
void failure(const void *, uint64_t produced, uint64_t committed, uint64_t saturation,
             bool primary_saturation) noexcept;
int64_t due(std::chrono::steady_clock::time_point) noexcept;
class Span {
    unsigned lane_{4};
    uint64_t slot_{no_item};
    int exceptions_{std::uncaught_exceptions()};

  public:
    Span(Kind, uint64_t item = no_item, uint64_t a = 0, uint64_t b = 0) noexcept;
    ~Span() {
        finish();
    }
    Span(const Span &) = delete;
    void finish() noexcept;
    void values(uint64_t a, uint64_t b) noexcept;
};
void point(Kind, uint64_t item = no_item, uint64_t a = 0, uint64_t b = 0) noexcept;
// Extends through frame destruction/accounting to the NEXT pop entry. The last
// successful iteration is finished at an empty pop or before failure handling.
class Cycle {
    alignas(Span) unsigned char storage_[sizeof(Span)];
    Span *span_{};

  public:
    ~Cycle() {
        finish();
    }
    void start(uint64_t item) noexcept;
    void finish() noexcept;
};
template <class F> decltype(auto) measured(Kind kind, uint64_t item, F &&function) {
    Span span(kind, item);
    return std::forward<F>(function)();
}
template <class Queue> void queue(Queue &queue, uint64_t item) noexcept {
    // Includes acquisition of the EXISTING metric mutex; never label it as the
    // queue operation's linearization time or an exclusive admission wait.
    try {
        Span sample(Kind::queue_sample, item);
        const auto m = queue.metrics();
        sample.values(m.occupancy, m.high_water);
        sample.finish();
        point(Kind::queue_counts, item, m.pushed, m.popped);
        point(Kind::queue_timing, item, m.full_ns, m.last_failed_wait_ns);
    } catch (...) {
        unavailable(); // Observer failure cannot terminate/replace capture failure.
    }
}
// Called only by the isolated daemon wrapper, before/after the real entrypoint.
void start(const std::filesystem::path &, uint64_t capture_ordinal);
void stop() noexcept;
bool self_test(const std::filesystem::path &);
} // namespace mantis::recorder_observer
