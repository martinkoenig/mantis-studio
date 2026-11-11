#include "recorder_observer.hpp"
#include <array>
#include <atomic>
#include <condition_variable>
#include <csignal>
#include <cstring>
#include <fstream>
#include <mantis/device_runtime.hpp>
#include <mutex>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <thread>
#include <time.h>
namespace mantis::recorder_observer {
namespace {
using Clock = std::chrono::steady_clock;
constexpr std::array<size_t, 4> limits{4096, 8192, 32768, 1024};
constexpr std::array<size_t, 4> offsets{0, 4096, 12288, 45056};
constexpr size_t record_count = 46080, output_limit = 8 * 1024 * 1024;
// Serialized as eleven explicit little-endian u64s; never dump native structs.
struct Event {
    uint64_t lane{}, ordinal{}, kind{}, item{}, begin{}, end{}, cpu_begin{}, cpu_end{}, a{}, b{}, flags{};
};
struct Row {
    Event event;
    // Single-writer lane prefix at completion, published by complete.release.
    // A snapshot cannot close a parent while omitting its later child records.
    uint64_t completion_prefix{};
    std::atomic_bool complete{};
};
static_assert(sizeof(Row) * record_count < output_limit);
static_assert(std::atomic_uint64_t::is_always_lock_free && std::atomic<const void *>::is_always_lock_free);
alignas(64) std::array<Row, record_count> rows;
struct alignas(64) Lane {
    std::atomic_uint64_t published{}, dropped{}, observer_cpu{};
};
std::array<Lane, 4> lanes;
std::atomic<const void *> selected{};
std::atomic_bool enabled{}, clock_error{}, identity_error{}, expired{}, observer_error{};
std::atomic_int64_t capture_at{};
std::atomic_uint first_state{};
Event first_event;
std::atomic_bool raw_ready{};
std::array<std::atomic_bool, 2> workers_running{};
std::array<char, 37> capture_id{}, raw_id{};
uint64_t ordinal{}, captures_seen{}, stream_count{};
std::atomic_uint64_t paced{};
bool control{};
int64_t origin{};
thread_local unsigned actor = 4;
std::jthread observer;
std::filesystem::path directory;
volatile std::sig_atomic_t export_requested{};
void (*old_signal)(int) = SIG_DFL;
bool signal_installed{};
int64_t wall() noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();
}
int64_t cpu(clockid_t clock = CLOCK_THREAD_CPUTIME_ID) noexcept {
    timespec value{};
    if (::clock_gettime(clock, &value)) {
        clock_error = true;
        return 0;
    }
    return value.tv_sec * 1000000000LL + value.tv_nsec;
}
void request_export(int) {
    export_requested = 1;
}
bool uuid(std::string_view value) noexcept {
    if (value.size() != 36)
        return false;
    for (size_t i = 0; i < value.size(); ++i)
        if ((i == 8 || i == 13 || i == 18 || i == 23)
                ? value[i] != '-'
                : !((value[i] >= '0' && value[i] <= '9') || (value[i] >= 'a' && value[i] <= 'f')))
            return false;
    return true;
}
std::array<uint64_t, 11> words(const Event &e) {
    return {e.lane, e.ordinal, e.kind, e.item, e.begin, e.end, e.cpu_begin, e.cpu_end, e.a, e.b, e.flags};
}
void number(std::ostream &out, uint64_t value) {
    for (unsigned i = 0; i < 8; ++i)
        out.put(static_cast<char>(value >> (8 * i)));
}
// Only the explicit native cut control supplies this gate. No frame path or
// ordinary sampler export takes its mutex. All waits bound failure; state
// predicates, not time or scheduler luck, establish the tested interleaving.
struct SnapshotCut {
    std::mutex mutex;
    std::condition_variable changed;
    bool ready{}, cut{}, completed{}, released{}, failed{};
    bool wait(std::unique_lock<std::mutex> &lock, bool &state) {
        if (!changed.wait_for(lock, std::chrono::seconds(5), [&] { return state || released || failed; }))
            failed = true;
        changed.notify_all();
        return state && !failed;
    }
    bool after_count_cut() {
        std::unique_lock lock(mutex);
        cut = true;
        changed.notify_all();
        return wait(lock, completed);
    }
    void release() {
        std::lock_guard lock(mutex);
        released = true;
        changed.notify_all();
    }
};
// Only the sampler writes files. Published begin fields are immutable. End and
// value fields are read ONLY after complete.acquire(), so an in-flight span is
// exported as pending, never as zero-duration or invented completed evidence.
// Completion also needs its entire same-lane publication prefix in this cut;
// otherwise a delayed exporter could close parents but omit their child rows.
void export_snapshot(unsigned index, bool first, size_t &output_bytes, SnapshotCut *test_cut = nullptr) {
    if (index >= 3 && !first)
        return;
    std::array<uint64_t, 4> counts{}, drops{};
    for (unsigned i = 0; i < 4; ++i) {
        counts[i] = lanes[i].published.load(std::memory_order_acquire);
        drops[i] = lanes[i].dropped.load();
    }
    if (test_cut && !test_cut->after_count_cut())
        return;
    const auto failure_state = first_state.load(std::memory_order_acquire);
    const bool has_capture = selected.load(std::memory_order_acquire) != nullptr;
    const auto snapshot = static_cast<uint64_t>(wall() - origin);
    nlohmann::json metadata{
        {"version", 1},
        {"record_bytes", 88},
        {"clock", "steady_ns_since_observer_start"},
        {"workload", control ? "observer-self-test" : "actual-acceptance"},
        {"thread_cpu_clock", "thread_duration_only"},
        {"origin_ns", origin},
        {"snapshot_ns", snapshot},
        {"capture_ordinal", ordinal},
        {"capture_selected", has_capture},
        {"capture_id", has_capture ? capture_id.data() : ""},
        {"raw_id", raw_ready.load(std::memory_order_acquire) ? raw_id.data() : ""},
        {"schema", 1},
        {"streams", has_capture ? stream_count : 0},
        {"source_paced", has_capture ? paced.load() : 0},
        {"queue_capacity", 32},
        {"enqueue_budget_ns", 50000000},
        {"counts", counts},
        {"dropped", drops},
        {"limits", limits},
        {"observer_emission_cpu_ns",
         std::array<uint64_t, 4>{lanes[0].observer_cpu.load(), lanes[1].observer_cpu.load(),
                                 lanes[2].observer_cpu.load(), lanes[3].observer_cpu.load()}},
        {"clock_error", clock_error.load()},
        {"identity_error", identity_error.load()},
        {"observer_error", observer_error.load()},
        {"expired", expired.load()},
        {"first_failure_pending", failure_state == 1},
        {"workers_running", std::array<bool, 2>{workers_running[0].load(), workers_running[1].load()}},
        {"first_failure", failure_state == 2 ? nlohmann::json(words(first_event)) : nlohmann::json()},
        {"observer_qualification", "UNQUALIFIED_REQUIRES_MATCHED_NATIVE_CONTROL"},
        {"queue_sample_scope", "separate_metric_lock_interval_not_linearization"},
        {"wait_attribution", "off_cpu_or_io_not_discriminated"}};
    const auto count = counts[0] + counts[1] + counts[2] + counts[3];
    // Normal snapshots reserve one header for the first failure even when a
    // previous full snapshot exhausted the payload budget.
    const auto budget = first ? output_limit : output_limit - 8208;
    const bool rejected = output_bytes + 8192 + count * 88 > budget;
    metadata["output_rejected"] = rejected;
    metadata["exported_counts"] = rejected ? std::array<uint64_t, 4>{} : counts;
    auto header = metadata.dump();
    if (header.size() > 8192 || output_bytes + header.size() + 16 > budget)
        return;
    const auto name = first ? "first-failure.bin" : "snapshot-" + std::to_string(index) + ".bin";
    const auto path = directory / name;
    if (std::filesystem::exists(path))
        return; // Never overwrite the first failure or an older execution.
    auto part = path;
    part += ".part";
    std::ofstream out(part, std::ios::binary);
    out.write("MREC0001", 8);
    number(out, header.size());
    out.write(header.data(), static_cast<std::streamsize>(header.size()));
    if (!rejected)
        for (unsigned lane = 0; lane < 4; ++lane)
            for (size_t i = 0; i < counts[lane]; ++i) {
                auto &row = rows[offsets[lane] + i];
                Event e;
                e.lane = row.event.lane;
                e.ordinal = row.event.ordinal;
                e.kind = row.event.kind;
                e.item = row.event.item;
                e.begin = row.event.begin;
                e.cpu_begin = row.event.cpu_begin;
                if (row.complete.load(std::memory_order_acquire) && row.completion_prefix <= counts[lane] &&
                    row.event.end <= snapshot)
                    e = row.event;
                else {
                    e.end = e.cpu_end = UINT64_MAX;
                    e.flags = 2;
                }
                for (auto word : words(e))
                    number(out, word);
            }
    out.flush();
    if (!out)
        return;
    out.close();
    if (!out)
        return;
    std::filesystem::rename(part, path); // Export ACK is a complete immutable file.
    output_bytes += header.size() + 16 + (rejected ? 0 : count * 88);
}
bool snapshot_cut_control(const void *owner) {
    SnapshotCut gate;
    std::jthread writer([&] {
        try {
            bind(owner, 1);
            for (auto kind : {Kind::source_wake, Kind::source_next, Kind::source_diagnostics, Kind::preview,
                              Kind::admission})
                point(kind, 0);
            point(Kind::admission_result, 0, 1, 0);
            point(Kind::source_sleep, 0);
            point(Kind::producer_end);
            unbind();
            bind(owner, 2);
            raw("00000000-0000-0000-0000-000000000002", 1);
            point(Kind::queue_pop);
            {
                Span service(Kind::writer_service, 0);
                {
                    Span callback(Kind::callback, 0);
                    {
                        Span store(Kind::store_total, 0);
                        {
                            std::unique_lock lock(gate.mutex);
                            gate.ready = true;
                            gate.changed.notify_all();
                            if (!gate.wait(lock, gate.cut))
                                throw std::runtime_error("Snapshot cut gate unavailable");
                        }
                        for (uint64_t kind = 21; kind <= 34; ++kind)
                            point(static_cast<Kind>(kind), 0);
                    }
                }
                point(Kind::payload, 0);
                point(Kind::session_lock_wait, 0);
                point(Kind::accounting, 0);
            }
            {
                std::unique_lock lock(gate.mutex);
                gate.completed = true;
                gate.changed.notify_all();
                if (!gate.wait(lock, gate.released))
                    throw std::runtime_error("Snapshot acknowledgement unavailable");
            }
            point(Kind::writer_end);
        } catch (...) {
            std::lock_guard lock(gate.mutex);
            gate.failed = true;
            gate.changed.notify_all();
        }
        unbind();
    });
    // Declared after writer: release every wait BEFORE jthread joins on unwind.
    struct Release {
        SnapshotCut &gate;
        ~Release() {
            gate.release();
        }
    } release{gate};
    {
        std::unique_lock lock(gate.mutex);
        if (!gate.wait(lock, gate.ready))
            return false;
    }
    size_t bytes{};
    export_snapshot(0, false, bytes, &gate);
    gate.release();
    writer.join();
    if (gate.failed || !gate.completed || !std::filesystem::exists(directory / "snapshot-0.bin"))
        return false;
    export_snapshot(1, false, bytes);
    return std::filesystem::exists(directory / "snapshot-1.bin");
}
// Exercises the actual Session writer catch/cleanup, not a second runtime.
class CallbackSource final : public device::ImageStream {
    device::Descriptor descriptor_{{"observer-callback-source"}, {}, {}, {}, {}, {}, {}};
    std::mutex mutex_;
    std::condition_variable changed_;
    bool released_{}, timed_out_{};
    unsigned calls_{};

  public:
    unsigned starts{}, stops{};
    const device::Descriptor &descriptor() const override {
        return descriptor_;
    }
    Result<void> start() override {
        ++starts;
        return {};
    }
    Result<data::Published> next() override {
        if (calls_++ == 0) {
            data::Packet image;
            image.type = schema::image;
            return data::publish(std::move(image));
        }
        std::unique_lock lock(mutex_);
        if (!changed_.wait_for(lock, std::chrono::seconds(5), [&] { return released_; }))
            timed_out_ = true;
        return data::Published{};
    }
    void release() {
        std::lock_guard lock(mutex_);
        released_ = true;
        changed_.notify_all();
    }
    Result<void> stop() override {
        ++stops;
        release();
        return {};
    }
    bool bounded() const {
        return !timed_out_; // Inspected only after Session retired its workers.
    }
};
bool callback_failure_control(bool nonstandard) {
    CallbackSource source;
    std::atomic_uint entered{};
    bool caught{};
    const std::string expected =
        nonstandard ? "Non-standard recorder exception" : "Observer callback failure";
    // Session's real constructor selects this third capture and owns its workers.
    captures_seen = 2;
    try {
        device::Session session(
            {{"00000000-0000-0000-0000-000000000001"}, {{"observer-callback-source"}}, {}}, {&source},
            [&](data::Published image) {
                ++entered;
                raw("00000000-0000-0000-0000-000000000002", 1);
                Span store(Kind::store_total, image->header.sequence.value);
                if (nonstandard)
                    throw 7;
                throw std::runtime_error(expected);
            },
            [&](const LogRecord &record) {
                if (record.component == "capture")
                    source.release(); // failed() has already stopped the producer.
            });
    } catch (const Failure &error) {
        caught = expected == error.what();
    }
    if (!caught || entered != 1 || source.starts != 1 || source.stops != 1 || !source.bounded() ||
        workers_running[0] || workers_running[1])
        return false;
    size_t bytes{};
    export_snapshot(0, false, bytes);
    return std::filesystem::exists(directory / "snapshot-0.bin");
}
} // namespace
int64_t due(Clock::time_point value) noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(value.time_since_epoch()).count() - origin;
}
void capture(const void *owner, std::string_view id, uint64_t streams, bool source_paced) noexcept {
    if (++captures_seen != ordinal)
        return;
    if (!uuid(id)) {
        identity_error = true;
        return;
    }
    std::memcpy(capture_id.data(), id.data(), id.size());
    stream_count = streams;
    paced = source_paced;
    capture_at = wall();
    selected.store(owner, std::memory_order_release);
}
void bind(const void *owner, unsigned lane) noexcept {
    actor = owner && owner == selected.load(std::memory_order_acquire) ? lane : 4;
    if (actor == 1 || actor == 2)
        workers_running[actor - 1] = true;
}
void unbind() noexcept {
    if (actor == 1 || actor == 2)
        workers_running[actor - 1] = false;
    actor = 4;
}
Scope::Scope(const void *owner, unsigned lane) noexcept : previous_(actor) {
    bind(owner, lane);
}
Scope::~Scope() {
    actor = previous_;
}
void raw(std::string_view id, uint64_t schema) noexcept {
    if (actor != 2)
        return;
    if (schema != 1 || !uuid(id)) {
        identity_error = true;
        return;
    }
    if (!raw_ready.load(std::memory_order_relaxed)) {
        std::memcpy(raw_id.data(), id.data(), id.size());
        raw_ready.store(true, std::memory_order_release);
    } else if (id != std::string_view(raw_id.data()))
        identity_error = true;
}
void source_mode(bool value) noexcept {
    if (actor == 0)
        paced = value;
}
void unavailable() noexcept {
    observer_error = true;
}
Span::Span(Kind kind, uint64_t item, uint64_t a, uint64_t b) noexcept {
    if (actor >= 4 || !enabled.load(std::memory_order_relaxed))
        return;
    const auto cost_begin = cpu();
    const auto timestamp = wall();
    const auto began = capture_at.load();
    const auto first = first_state.load(std::memory_order_acquire);
    const auto until =
        first == 2 ? origin + static_cast<int64_t>(first_event.begin) + 30000000000LL : began + 30000000000LL;
    if (began && timestamp >= until) {
        enabled = false;
        expired = true;
        return;
    }
    lane_ = actor;
    slot_ = lanes[lane_].published.load(std::memory_order_relaxed);
    if (slot_ == limits[lane_]) {
        lanes[lane_].dropped.fetch_add(1);
        slot_ = no_item;
        return;
    }
    auto &e = rows[offsets[lane_] + slot_].event;
    e.lane = lane_;
    e.ordinal = slot_;
    e.kind = static_cast<uint64_t>(kind);
    e.item = item;
    e.begin = static_cast<uint64_t>(timestamp - origin);
    e.cpu_begin = static_cast<uint64_t>(cpu());
    e.a = a;
    e.b = b;
    lanes[lane_].published.store(slot_ + 1, std::memory_order_release);
    lanes[lane_].observer_cpu.fetch_add(static_cast<uint64_t>(cpu() - cost_begin));
}
void Span::values(uint64_t a, uint64_t b) noexcept {
    if (slot_ != no_item) {
        auto &e = rows[offsets[lane_] + slot_].event;
        e.a = a;
        e.b = b;
    }
}
void Span::finish(bool exceptional) noexcept {
    if (slot_ == no_item)
        return;
    const auto cost_begin = cpu();
    auto &row = rows[offsets[lane_] + slot_];
    row.event.end = static_cast<uint64_t>(wall() - origin);
    row.event.cpu_end = static_cast<uint64_t>(cpu());
    row.event.flags = exceptional || std::uncaught_exceptions() > exceptions_ ? 1 : 0;
    row.completion_prefix = lanes[lane_].published.load(std::memory_order_relaxed);
    row.complete.store(true, std::memory_order_release);
    lanes[lane_].observer_cpu.fetch_add(static_cast<uint64_t>(cpu() - cost_begin));
    slot_ = no_item;
}
void point(Kind kind, uint64_t item, uint64_t a, uint64_t b) noexcept {
    Span event(kind, item, a, b);
}
void Cycle::start(uint64_t item) noexcept {
    finish();
    span_ = ::new (static_cast<void *>(storage_)) Span(Kind::writer_service, item);
}
void Cycle::finish(bool exceptional) noexcept {
    if (span_) {
        // A writer catch runs after unwinding, including frame destruction.
        // Preserve that exceptional outcome before the idempotent destructor.
        span_->finish(exceptional);
        span_->~Span();
        span_ = nullptr;
    }
}
void failure(const void *owner, uint64_t produced, uint64_t committed, uint64_t saturation,
             bool primary_saturation) noexcept {
    if (owner != selected.load(std::memory_order_acquire))
        return;
    unsigned expected{};
    if (!first_state.compare_exchange_strong(expected, 1))
        return;
    const auto timestamp = static_cast<uint64_t>(wall() - origin);
    first_event = {actor,
                   0,
                   static_cast<uint64_t>(Kind::first_failure),
                   saturation,
                   timestamp,
                   timestamp,
                   0,
                   0,
                   produced,
                   committed,
                   primary_saturation ? 4u : 0u};
    first_state.store(2, std::memory_order_release); // Separate slot survives buffer exhaustion.
    point(Kind::first_failure, saturation, produced, committed);
}
void start(const std::filesystem::path &path, uint64_t capture_ordinal) {
    if (!capture_ordinal || observer.joinable() || std::filesystem::exists(path))
        throw std::runtime_error("Recorder observer requires a fresh output directory and capture ordinal");
    std::filesystem::create_directories(path);
    directory = path;
    ordinal = capture_ordinal;
    origin = wall();
    // Prefault fixed buffers before the daemon starts. No hot-path allocation.
    for (auto &row : rows)
        row.event = {};
    old_signal = std::signal(SIGUSR1, request_export);
    if (old_signal == SIG_ERR)
        throw std::runtime_error("Recorder observer signal unavailable");
    signal_installed = true;
    enabled = true;
    observer = std::jthread([](std::stop_token stop_token) {
        actor = 3;
        unsigned exports{};
        size_t output_bytes{};
        bool failure_exported{};
        while (!stop_token.stop_requested()) {
            const auto next = Clock::now() + std::chrono::milliseconds(100);
            std::this_thread::sleep_until(next);
            const auto now = wall();
            const auto began = capture_at.load();
            const auto first = first_state.load(std::memory_order_acquire);
            const auto until = first == 2 ? origin + static_cast<int64_t>(first_event.begin) + 30000000000LL
                                          : began + 30000000000LL;
            if (began && now >= until) {
                enabled = false;
                expired = true;
            }
            Span sample(Kind::observer_sample, no_item, static_cast<uint64_t>(due(next)),
                        static_cast<uint64_t>(cpu(CLOCK_PROCESS_CPUTIME_ID)));
            sample.finish();
            try {
                if (first == 2 && !failure_exported) {
                    export_snapshot(exports++, true, output_bytes);
                    failure_exported = true;
                }
                if (export_requested) {
                    export_requested = 0;
                    export_snapshot(exports++, false, output_bytes);
                }
            } catch (...) {
                unavailable();
                // A missing/partial file is UNQUALIFIED, never a replacement error.
            }
        }
        try {
            export_snapshot(exports, false, output_bytes);
        } catch (...) {
        }
        actor = 4;
    });
}
void stop() noexcept {
    if (observer.joinable()) {
        observer.request_stop();
        observer.join();
    }
    enabled = false;
    if (signal_installed) {
        std::signal(SIGUSR1, old_signal);
        signal_installed = false;
    }
}
bool self_test(const std::filesystem::path &path, SelfTest mode) {
    // Native qualification only, never called by a product or this source-only
    // phase. Exercises the real buffer, pending publication and sticky failure
    // after exhaustion; the independent Python decoder checks exported bytes.
    if (std::filesystem::exists(path))
        return false;
    std::filesystem::create_directories(path);
    directory = path;
    origin = wall();
    ordinal = 3;
    control = true;
    enabled = true;
    if (mode == SelfTest::callback_failure || mode == SelfTest::nonstandard_failure) {
        const auto ok = callback_failure_control(mode == SelfTest::nonstandard_failure);
        enabled = false;
        return ok;
    }
    int owner{};
    constexpr auto id = "00000000-0000-0000-0000-000000000001";
    capture(&owner, id, 1, false);
    capture(&owner, id, 1, false);
    capture(&owner, id, 1, false);
    if (mode == SelfTest::snapshot_cut) {
        const auto ok = snapshot_cut_control(&owner);
        enabled = false;
        return ok;
    }
    bind(&owner, 2);
    raw("00000000-0000-0000-0000-000000000002", 1);
    Span pending(Kind::writer_service, 0);
    for (size_t i = 0; i < limits[2] + 1; ++i)
        point(Kind::payload, i);
    failure(&owner, 35, 1, 1, true);
    failure(&owner, 999, 999, 0, false);
    size_t bytes{};
    export_snapshot(0, true, bytes);
    const bool ok = lanes[2].published == limits[2] && lanes[2].dropped == 3 &&
                    !rows[offsets[2]].complete.load() && first_state == 2 && first_event.a == 35 &&
                    first_event.b == 1 && first_event.flags == 4 &&
                    std::filesystem::exists(path / "first-failure.bin");
    unbind();
    enabled = false;
    return ok;
}
} // namespace mantis::recorder_observer
