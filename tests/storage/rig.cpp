#include "../../src/artifact-store/segments.hpp"
#include "../integration/portable_environment.hpp"
#include "faults.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <condition_variable>
#include <cstring>
#include <exception>
#include <fstream>
#include <future>
#include <iostream>
#include <mantis/data_io.hpp>
#include <mantis/device_runtime.hpp>
#include <mantis/image_layout.hpp>
#include <mantis/replay.hpp>
#include <mantis/services.hpp>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>
#include <sys/resource.h>
#include <unistd.h>
using namespace mantis;
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
namespace st = artifact::storage_test;
#define CHECK(...)                                                                                           \
    do {                                                                                                     \
        if (!(__VA_ARGS__))                                                                                  \
            throw std::runtime_error("rig-storage line " + std::to_string(__LINE__) + ": " #__VA_ARGS__);    \
    } while (false)
namespace {
int64_t now() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();
}
constexpr size_t bound = 128;
struct Admission {
    int64_t began{}, ended{};
    bool accepted{};
};
struct Callback {
    int64_t began{}, ended{};
    uint64_t payload{};
};
struct State {
    std::mutex mutex;
    std::condition_variable changed;
    uint64_t epoch{};
    std::array<Admission, bound + 2> admissions{};
    size_t admitted{}, admission_overflow{};
    std::array<Callback, bound> callbacks{};
    std::array<data::Published, bound> packets{};
    size_t committed{};
    std::vector<LogRecord> errors;
    void signal() {
        ++epoch;
        changed.notify_all();
    }
};
std::atomic<State *> sampler{};
struct Resources {
    uint64_t rss_kib{}, peak_rss_kib{}, threads{}, fds{};
    int64_t cpu_ns{};
    Json json() const {
        return {{"rss_KiB", rss_kib},
                {"process_peak_rss_KiB", peak_rss_kib},
                {"threads", threads},
                {"fds", fds},
                {"process_cpu_ns", cpu_ns}};
    }
};
Resources resources() {
    Resources r;
    std::ifstream in("/proc/self/status");
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream field(line);
        std::string key;
        uint64_t value{};
        if (field >> key >> value) {
            if (key == "VmRSS:")
                r.rss_kib = value;
            if (key == "VmHWM:")
                r.peak_rss_kib = value;
            if (key == "Threads:")
                r.threads = value;
        }
    }
    CHECK(r.rss_kib > 0 && r.threads > 0);
    for ([[maybe_unused]] auto &entry : std::filesystem::directory_iterator("/proc/self/fd"))
        ++r.fds;
    rusage usage{};
    CHECK(::getrusage(RUSAGE_SELF, &usage) == 0);
    r.cpu_ns = (usage.ru_utime.tv_sec + usage.ru_stime.tv_sec) * 1000000000ll +
               (usage.ru_utime.tv_usec + usage.ru_stime.tv_usec) * 1000ll;
    return r;
}
Json distribution(std::vector<int64_t> values) {
    if (values.empty())
        return {{"samples", 0}};
    std::sort(values.begin(), values.end());
    auto percentile = [&](size_t percent) { return values[(values.size() * percent + 99) / 100 - 1]; };
    return {{"samples", values.size()},
            {"p50_ns", percentile(50)},
            {"p95_ns", percentile(95)},
            {"p99_ns", percentile(99)},
            {"max_ns", values.back()}};
}
std::string canonical(const data::Packet &p) {
    std::ostringstream bytes(std::ios::binary);
    data::write_packet(bytes, p);
    return bytes.str();
}
uint64_t number(const data::Metadata &m, const std::string &key) {
    return std::stoull(m.at(key));
}
uint64_t payload(const data::Packet &p) {
    uint64_t n{};
    for (const auto &f : p.frames)
        for (const auto &a : f->attributes)
            n += a.buffer.size();
    return n;
}
// Expected rasters are built once, independently of the plugin renderer. RAW10
// quartets use an explicit four-sample formula; every byte is then compared on
// every frame/replay. Only the first four exposure-counter pixels vary.
struct PixelOracle {
    std::array<std::vector<std::byte>, 2> rasters;
    explicit PixelOracle(const Json &profile) {
        const uint32_t width = profile.at("width"), height = profile.at("height"),
                       stride = profile.at("stride");
        const bool packed = profile.at("format") == "Y10P";
        for (size_t camera = 0; camera < 2; ++camera) {
            auto &out = rasters[camera];
            out.resize(size_t(stride) * height);
            auto value = [&](uint32_t x, uint32_t y) -> uint16_t {
                if (y == 0 && x < 4)
                    return 0;
                if (x + 4 * camera == width / 2 + 4 && y == height / 2)
                    return 1023;
                return uint16_t(((x + 4 * camera) * 13 + y * 7 + 17) % 251);
            };
            for (uint32_t y = 0; y < height; ++y) {
                auto *row = out.data() + size_t(y) * stride;
                for (uint32_t x = 0; x < width; x += 4) {
                    const auto a = value(x, y), b = value(x + 1, y), c = value(x + 2, y), d = value(x + 3, y);
                    const size_t offset = packed ? x / 4 * 5 : x;
                    row[offset] = std::byte(a / 4);
                    row[offset + 1] = std::byte(b / 4);
                    row[offset + 2] = std::byte(c / 4);
                    row[offset + 3] = std::byte(d / 4);
                    if (packed)
                        row[offset + 4] = std::byte(a % 4 + 4 * (b % 4) + 16 * (c % 4) + 64 * (d % 4));
                }
            }
        }
    }
};
// This oracle does not call the producer's model, renderer, packer or pairing.
// Decode each RAW10 sample independently and verify the analytic scene and four
// exposure-counter pixels, including every padding byte and low packing bits.
void exact(const data::Packet &p, const Json &profile, size_t index, const std::string &generation,
           const PixelOracle &oracle, bool source_limited = false) {
    const uint32_t width = profile.at("width"), height = profile.at("height"), stride = profile.at("stride");
    const bool packed = profile.at("format") == "Y10P";
    const uint64_t fps = profile.at("cameras")[0].at("rate_num");
    const std::string id = "org.mantis.virtual-rig:" + profile.at("id").get<std::string>();
    const auto calibration = profile.at("calibration_id").get<std::string>();
    CHECK(p.type == schema::frameset && p.frames.size() == 2 && p.attributes.empty());
    CHECK(p.header.sequence.value == index && p.header.metadata.at("generation") == generation);
    CHECK(p.header.metadata.at("provenance") == "simulation-only");
    CHECK(p.header.metadata.at("physical_exposure_skew") == "unavailable");
    CHECK(p.header.sync_quality == time::SyncQuality::software);
    CHECK(p.header.metadata.at("pairing_exclusions") == "[]");
    const auto native_index = p.frames[0]->header.sequence.value;
    CHECK(native_index >= index);
    if (!source_limited)
        CHECK(native_index == index);
    for (size_t camera = 0; camera < 2; ++camera) {
        const auto &f = *p.frames[camera];
        const auto &h = f.header;
        const auto &m = h.metadata;
        const auto &c = profile.at("cameras")[camera];
        const auto layout = data::image_layout(f);
        CHECK(f.type == schema::image && f.frames.empty() && f.attributes.size() == 1);
        CHECK(layout.width == width && layout.height == height && layout.row_stride == stride);
        CHECK(h.sequence.value == native_index && m.at("generation") == generation);
        CHECK(native_index - index <=
              number(p.header.metadata, std::string(camera ? "right" : "left") + ".source_overruns"));
        CHECK(m.at("role") == (camera ? "right" : "left"));
        CHECK(m.at("component_id") == id + "/" + c.at("id").get<std::string>());
        CHECK(m.at("stream_id") == id + "/" + c.at("stream").get<std::string>());
        CHECK(m.at("fourcc") == profile.at("format").get<std::string>());
        CHECK(h.calibration.id.value == calibration && h.calibration.revision == 1);
        CHECK(h.frame.id.value == id + "/" + c.at("id").get<std::string>() + "/optical");
        CHECK(h.timestamp.domain.id.value == c.at("clock").get<std::string>() + "/epoch/" + generation);
        CHECK(h.timestamp.nanoseconds == int64_t(native_index * 1000000000ull / fps));
        CHECK(number(m, "capture_event_ns") == uint64_t(h.timestamp.nanoseconds));
        CHECK(m.at("capture_event_clock") == id + "/reference/" + generation);
        CHECK(m.at("availability_clock") == m.at("capture_event_clock"));
        CHECK(number(m, "availability_ns") == number(m, "capture_event_ns"));
        CHECK(m.at("host_receive_clock") == "std.chrono.steady_clock");
        CHECK(h.received.nanoseconds >=
              int64_t(number(m, "reference_origin_host_ns") + number(m, "availability_ns")));
        CHECK(number(m, "processing_ns") >= uint64_t(h.received.nanoseconds));
        CHECK(m.at("simulated_trigger_generation") == generation &&
              number(m, "simulated_trigger_id") == native_index);
        CHECK(h.sync_quality == time::SyncQuality::software);
        CHECK(h.sync.id.value == id + "/simulated-trigger/" + generation && h.sync.trigger == native_index);
        const auto &a = f.attributes[0];
        CHECK(a.buffer.access() == memory::Access::read_only && a.buffer.size() == uint64_t(stride) * height);
        if (packed) {
            CHECK(a.descriptor.name == "org.mantis.image.packed_bytes");
            CHECK(a.descriptor.shape == std::vector<uint64_t>{uint64_t(stride) * height});
            CHECK(a.descriptor.stride == std::vector<uint64_t>{1});
            CHECK(m.at("org.mantis.image.layout") == "mipi-raw10-v1");
        } else {
            CHECK(a.descriptor.name == "org.mantis.pixels");
            CHECK(a.descriptor.shape == std::vector<uint64_t>({height, width}));
            CHECK(a.descriptor.stride == std::vector<uint64_t>({stride, 1}));
        }
        const auto mapped = a.buffer.map_read();
        CHECK(mapped);
        const auto prefix = packed ? 5u : 4u;
        for (unsigned byte = 0; byte < 4; ++byte)
            CHECK(std::to_integer<unsigned>((*mapped)[byte]) == ((native_index >> (8 * byte)) & 255));
        if (packed)
            CHECK((*mapped)[4] == std::byte{});
        CHECK(mapped->size() == oracle.rasters[camera].size());
        CHECK(std::memcmp(mapped->data() + prefix, oracle.rasters[camera].data() + prefix,
                          mapped->size() - prefix) == 0);
    }
}
uint64_t visible_segment_bytes(const std::filesystem::path &object) {
    uint64_t bytes{};
    for (const auto &entry : std::filesystem::directory_iterator(object)) {
        const auto name = entry.path().filename().string();
        if (name.ends_with(".segment") || name.ends_with(".segment.part"))
            bytes += entry.file_size();
    }
    return bytes; // Called only at a held writer checkpoint or after joins.
}
struct Barrier {
    int entered[2]{-1, -1}, released[2]{-1, -1};
    Barrier() {
        CHECK(::pipe(entered) == 0);
        CHECK(::pipe(released) == 0);
    }
    void release() {
        st::release_gate(released[1]);
    }
    void release_noexcept() noexcept {
        // Collapse may have a current and a next checkpoint armed. Both must
        // be released before joining, including exceptions between rearm and
        // release. At most two borrowed controls exist; spare tokens are local.
        const char bytes[]{1, 1};
        if (released[1] >= 0) {
            ssize_t result;
            do {
                result = ::write(released[1], bytes, sizeof bytes);
            } while (result < 0 && errno == EINTR);
        }
    }
    ~Barrier() {
        for (int fd : {entered[0], entered[1], released[0], released[1]})
            if (fd >= 0)
                ::close(fd);
    }
};
struct Directory {
    std::filesystem::path path;
    explicit Directory(const std::filesystem::path &parent)
        : path(parent / ("mantis-rig-" + Id::random().value)) {
        CHECK(std::filesystem::create_directory(path));
        // All deletion stays inside this freshly owned, random test directory.
    }
    ~Directory() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};
struct Run {
    State state;
    Barrier barrier;
    std::unique_ptr<st::Arm> arm, next_arm;
    std::unique_ptr<device::Session> session;
    int64_t began{now()}, monitor_ns{}, last_monitor{};
    size_t monitor_count{};
    Resources before{resources()}, peak{before};
    Json queue = Json::array();
    Run() {
        CHECK(sampler.exchange(&state) == nullptr);
    }
    ~Run() {
        barrier.release_noexcept(); // Must precede Session's synchronous joins.
        if (session)
            session->stop();
        if (session && std::uncaught_exceptions()) {
            try {
                const auto cause = session->error();
                if (!cause.empty())
                    std::cerr << "retained capture cause: " << cause << '\n';
            } catch (...) {
                // Failure evidence must not replace the initiating assertion.
            }
        }
        sampler = nullptr;
    }
    void sample() {
        const auto stamp = now();
        if (stamp - last_monitor < 50000000)
            return;
        last_monitor = stamp;
        const auto r = resources();
        peak.rss_kib = std::max(peak.rss_kib, r.rss_kib);
        peak.peak_rss_kib = std::max(peak.peak_rss_kib, r.peak_rss_kib);
        peak.cpu_ns = std::max(peak.cpu_ns, r.cpu_ns);
        peak.threads = std::max(peak.threads, r.threads);
        peak.fds = std::max(peak.fds, r.fds);
        if (session && queue.size() < 256)
            queue.push_back({{"steady_ns", stamp},
                             {"occupancy", session->metrics().occupancy},
                             {"produced", session->produced()},
                             {"callback_success", session->committed()}});
        monitor_ns += now() - stamp;
        ++monitor_count;
    }
    template <class F>
    void until(F predicate, const char *phase, Clock::time_point deadline = Clock::now() + 5s) {
        while (true) {
            uint64_t seen;
            {
                std::lock_guard lock(state.mutex);
                seen = state.epoch;
            }
            if (predicate())
                return;
            sample();
            std::unique_lock lock(state.mutex);
            state.changed.wait_until(lock, std::min(deadline, Clock::now() + 2ms),
                                     [&] { return state.epoch != seen; });
            if (Clock::now() >= deadline)
                throw std::runtime_error(std::string("Timed out observing ") + phase);
        }
    }
    void gate(const std::filesystem::path &root, const Id &id, uint64_t skip,
              const char *point = "segment-sync-before") {
        arm = std::make_unique<st::Arm>(st::Plan{root, id.value, point, st::Action::pause, skip, 0, 0,
                                                 barrier.entered[1], barrier.released[0]});
    }
};
Json accounting(const data::Metadata &m) {
    Json result;
    for (const std::string role : {"left", "right"}) {
        auto n = [&](const char *key) { return number(m, role + "." + key); };
        CHECK(n("exposures") == n("source_drops") + n("source_overruns") + n("published") +
                                    n("startup_unmatched") + n("steady_unmatched") + n("shutdown_unmatched") +
                                    n("source_depth") + n("pending_depth") + n("inflight_depth"));
        CHECK(n("received") >= n("published") + n("startup_unmatched") + n("steady_unmatched"));
        CHECK(n("source_depth") == 0 && n("pending_depth") == 0 && n("inflight_depth") == 0);
        result[role] = {{"exposures", n("exposures")},
                        {"published", n("published")},
                        {"source_overruns", n("source_overruns")},
                        {"source_drops", n("source_drops")},
                        {"pairing_exclusions", n("startup_unmatched") + n("steady_unmatched")},
                        {"shutdown_unmatched", n("shutdown_unmatched")}};
    }
    return result;
}
Json exercise(const std::filesystem::path &library, Json profile, const std::string &mode,
              const std::filesystem::path &parent) {
    std::cerr << "rig-storage phase " << mode << ' ' << profile.at("format") << '\n';
    const PixelOracle oracle(profile);
    const auto outside = resources();
    Json report;
    std::filesystem::path removed_directory;
    std::vector<std::weak_ptr<const data::Packet>> retired;
    {
        Directory dir(parent);
        removed_directory = dir.path;
        const auto plugins_path = dir.path / "plugins";
        std::filesystem::create_directory(plugins_path);
        std::filesystem::copy_file(library, plugins_path / library.filename());
        std::filesystem::copy_file(library.parent_path() / "virtual-rig.json",
                                   plugins_path / "virtual-rig.json");
        const auto config = dir.path / "rig.json";
        {
            std::ofstream out(config);
            out << profile.dump();
            out.close();
            CHECK(out.good());
        }
        CHECK(test::environment("MANTIS_VIRTUAL_RIG_PROFILE", config.string().c_str()) == 0);
        auto store = std::make_shared<artifact::Store>(dir.path / "Recording.mantis");
        plugins::Registry registry({}, dir.path / "hosts", {});
        registry.discover(plugins_path, {"org.mantis.virtual-rig"});
        auto streams = registry.devices();
        CHECK(streams.size() == 1 && streams[0]->source_paced() && streams[0]->components().size() == 2);
        const bool transient = mode == "transient", prolonged = mode == "prolonged",
                   collapse = mode == "collapse";
        const bool heavy = collapse || mode == "benchmark";
        const Id capture = Id::random();
        artifact::Provenance provenance;
        provenance.producer = "org.mantis.virtual-rig";
        const auto statuses = registry.statuses();
        const auto producer = std::find_if(statuses.begin(), statuses.end(), [&](const auto &status) {
            return status.manifest.id == provenance.producer;
        });
        CHECK(producer != statuses.end());
        char first_dot{}, second_dot{};
        std::istringstream version(producer->manifest.version);
        CHECK(version >> provenance.version.major >> first_dot >> provenance.version.minor >> second_dot >>
              provenance.version.patch);
        CHECK(first_dot == '.' && second_dot == '.' && version.peek() == std::char_traits<char>::eof());
        provenance.parameters = streams[0]->descriptor().metadata;
        provenance.parameters["producer_plugin_version"] = producer->manifest.version;
        provenance.parameters["logical_device_id"] = streams[0]->descriptor().id.value;
        provenance.parameters["format_version"] = "2";
        Json components = Json::array();
        for (const auto &child : streams[0]->components())
            components.push_back(
                {{"id", child.id.value}, {"metadata", child.metadata}, {"capabilities", child.capabilities}});
        provenance.parameters["components"] = components.dump();
        provenance.parameters["capture_id"] = capture.value;
        provenance.parameters["segment_max_bytes"] = heavy                       ? "8388608"
                                                     : profile.at("width") == 64 ? "65536"
                                                                                 : "1048576";
        auto raw = store->begin({"org.mantis.RawCapture", 2}, provenance);
        st::Trace stages(store->root(), raw.value);
        Run run;
        if (transient || prolonged)
            run.gate(store->root(), raw, 1, "raw-record-before");
        if (collapse)
            run.gate(store->root(), raw, 5); // Explicit healthy phase: 6 x ~4 records.
        run.session = std::make_unique<device::Session>(
            device::CaptureDescriptor{capture, {streams[0]->descriptor().id}, {}},
            std::vector<device::ImageStream *>{streams[0].get()},
            [&](data::Published packet) {
                const auto began = now();
                store->append(raw, *packet);
                const auto ended = now();
                std::lock_guard lock(run.state.mutex);
                CHECK(run.state.committed < bound);
                auto &slot = run.state.callbacks[run.state.committed];
                slot = {began, ended, payload(*packet)};
                run.state.packets[run.state.committed++] = std::move(packet);
                run.state.signal();
            },
            [&](const LogRecord &record) {
                std::lock_guard lock(run.state.mutex);
                if (run.state.errors.size() < 4)
                    run.state.errors.push_back(record);
                run.state.signal();
            });
        Json fault;
        if (transient || prolonged || collapse) {
            const auto entered = st::await_event(run.barrier.entered[0], Clock::now() + 5s);
            CHECK(std::string(entered.point) == (collapse ? "segment-sync-before" : "raw-record-before") &&
                  std::string(entered.id) == raw.value);
            const auto initial = run.session->metrics().occupancy;
            const auto initial_produced = run.session->produced();
            const auto entered_ns = now();
            CHECK(run.session->active() && run.session->diagnostics().at("recorder_writer_running") == "1");
            if (!collapse) {
                run.until(
                    [&] {
                        return !run.session->active() ||
                               run.session->produced() >= initial_produced + (prolonged ? 33 : 4);
                    },
                    "source delivery while storage blocked");
                if (prolonged)
                    run.until(
                        [&] { return run.session->diagnostics().at("recorder_producer_running") == "0"; },
                        "explicit saturation and producer retirement");
                fault = {{"entered_ns", entered_ns},
                         {"observed_ns", now()},
                         {"initial_occupancy", initial},
                         {"initial_produced", initial_produced},
                         {"blocked_produced", run.session->produced()},
                         {"blocked_occupancy", run.session->metrics().occupancy},
                         {"callback_success_while_blocked", run.session->committed()}};
                const auto d = run.session->diagnostics();
                const auto measured_fps = std::stod(d.at("recorder_observed_frames_per_second"));
                CHECK(measured_fps > 0);
                fault["measured_arrival_fps"] = measured_fps;
                fault["remaining_queue_budget_estimate_ns"] = double(32 - initial) / measured_fps * 1e9;
                CHECK(run.session->produced() > initial_produced);
                if (prolonged) {
                    CHECK(run.session->error().find("LOSSLESS writer queue saturated") != std::string::npos);
                    CHECK(run.session->metrics().occupancy == 32 && run.session->metrics().high_water == 32);
                    CHECK(run.session->saturation() == 1);
                    CHECK(number(d, "recorder_failed_enqueue_wait_ns") >= 50000000);
                    CHECK(d.at("recorder_producer_running") == "0");
                    CHECK(d.at("recorder_writer_running") == "1" &&
                          number(d, "recorder_cleanup_finished_ns") == uint64_t(-1));
                } else {
                    CHECK(run.session->active() && run.session->saturation() == 0);
                    CHECK(run.session->metrics().occupancy > initial &&
                          run.session->metrics().occupancy < 32);
                    CHECK(double(now() - entered_ns) <
                          fault["remaining_queue_budget_estimate_ns"].get<double>());
                }
                const auto source_at_release = streams[0]->diagnostics();
                fault["source_overruns_at_release_left"] = number(source_at_release, "left.source_overruns");
                fault["source_overruns_at_release_right"] =
                    number(source_at_release, "right.source_overruns");
                run.barrier.release(); // No claim that stop can cancel the synchronous Store operation.
                if (transient)
                    run.until(
                        [&] {
                            return !run.session->active() ||
                                   (run.session->metrics().occupancy == 0 &&
                                    run.session->committed() >= std::max(initial_produced + 6, uint64_t{16}));
                        },
                        "drained transient queue");
            } else {
                // Rate-limit actual segment sync service after a measured healthy
                // phase. This is a bandwidth-deficit model, not SSD thermal proof.
                CHECK(initial < 32 && run.session->saturation() == 0);
                const auto before_degrade = streams[0]->diagnostics();
                fault["healthy_writer_control_active_unsaturated"] = true;
                fault["pre_degrade_source_overruns_left"] = number(before_degrade, "left.source_overruns");
                fault["pre_degrade_source_overruns_right"] = number(before_degrade, "right.source_overruns");
                fault["healthy_source_full_fidelity"] = number(before_degrade, "left.source_overruns") == 0 &&
                                                        number(before_degrade, "right.source_overruns") == 0;
                fault["healthy_boundary_ns"] = entered_ns;
                fault["initial_occupancy"] = initial;
                fault["healthy_visible_segment_bytes"] =
                    visible_segment_bytes(store->root() / "objects" / raw.value);
                const auto measured_arrival =
                    std::stod(run.session->diagnostics().at("recorder_observed_frames_per_second"));
                CHECK(measured_arrival > 0);
                fault["measured_arrival_fps_at_degrade"] = measured_arrival;
                fault["queue_budget_at_degrade_estimate_ns"] = double(32 - initial) * 1e9 / measured_arrival;
                auto deadline = Clock::now();
                unsigned gates{};
                while (run.session->active() && gates < 12) {
                    run.next_arm = std::make_unique<st::Arm>(
                        st::Plan{store->root(), raw.value, "segment-sync-before", st::Action::pause, 0, 0, 0,
                                 run.barrier.entered[1], run.barrier.released[0]});
                    deadline += 150ms; // ~9.2 MB / 150ms < 276.48 MB/s, intentionally insufficient.
                    {
                        std::unique_lock lock(run.state.mutex);
                        run.state.changed.wait_until(lock, deadline, [&] { return !run.session->active(); });
                    }
                    run.sample();
                    fault["degraded_visible_segment_bytes"] =
                        visible_segment_bytes(store->root() / "objects" / raw.value);
                    fault["degraded_visible_bytes_sample_ns"] = now();
                    run.barrier.release();
                    ++gates;
                    if (!run.session->active())
                        break;
                    const auto event = st::await_event(run.barrier.entered[0], Clock::now() + 5s);
                    CHECK(std::string(event.point) == "segment-sync-before");
                    // Entry at the next checkpoint proves the previous matched
                    // operation retired; only now destroy its control.
                    run.arm.reset();
                    run.arm = std::move(run.next_arm);
                }
                fault["degraded_gate_count"] = gates;
                fault["degraded_end_ns"] = now();
            }
        } else {
            run.until(
                [&] {
                    return !run.session->active() ||
                           run.session->committed() >= (heavy                            ? 80u
                                                        : profile.at("format") == "Y10P" ? 4u
                                                        : profile.at("width") == 64      ? 16u
                                                                                         : 8u);
                },
                "bounded capture");
        }
        run.barrier.release_noexcept();
        const auto source_at_stop_request = streams[0]->diagnostics();
        const auto stop_began = now();
        run.session->stop();
        const auto stop_ended = now();
        const auto after_stop = resources();
        const auto written_segment_bytes = visible_segment_bytes(store->root() / "objects" / raw.value);
        run.arm.reset(); // All matched I/O has retired before control destruction.
        run.next_arm.reset();
        run.sample();
        const auto d = run.session->diagnostics();
        const auto source = streams[0]->diagnostics();
        const auto counts = accounting(source);
        CHECK(run.state.committed > 0);
        const auto cost = st::stream_cost(
            dir.path, [&](std::ostream &out) { artifact::segments::append(out, *run.state.packets[0]); });
        const Json stream_probe{
            {"ordinary_ns", cost.ordinary_ns}, {"qualified_ns", cost.qualified_ns}, {"bytes", cost.bytes}};
        const bool source_loss = number(source_at_stop_request, "left.source_overruns") ||
                                 number(source_at_stop_request, "right.source_overruns");
        if (number(source, "left.source_overruns") || number(source, "right.source_overruns"))
            std::cerr << "source-overrun evidence " << mode << " " << profile.at("format") << " "
                      << Json{{"at_stop", counts}, {"at_release", fault}}.dump() << '\n';
        const auto count = run.state.committed;
        CHECK(run.session->committed() == count && count > 0);
        CHECK(run.session->metrics().occupancy == 0 && run.session->metrics().dropped == 0);
        CHECK(d.at("recorder_writer_running") == "0" && d.at("recorder_producer_running") == "0");
        CHECK(d.at("recorder_secondary_error").empty() && d.at("recorder_cleanup_errors") == "0");
        CHECK(run.state.admission_overflow == 0 && stages.overflow == 0);
        const bool saturated = run.session->saturation() != 0;
        if (prolonged || collapse) {
            CHECK(saturated &&
                  run.session->error().find("LOSSLESS writer queue saturated") != std::string::npos);
            CHECK(run.session->produced() == count + 1); // Exactly one explicitly refused publication.
            CHECK(!run.state.errors.empty() && run.state.errors.front().message.find(
                                                   "LOSSLESS writer queue saturated") != std::string::npos);
        } else if (!heavy) {
            CHECK(run.session->error().empty() && !saturated && run.session->produced() == count);
        }
        for (const auto role : {"left", "right"}) {
            CHECK(number(source, std::string(role) + ".published") == run.session->produced());
            CHECK(number(source, std::string(role) + ".source_drops") == 0);
            CHECK(number(source, std::string(role) + ".startup_unmatched") == 0);
            CHECK(number(source, std::string(role) + ".steady_unmatched") == 0);
            if (prolonged)
                CHECK(fault.at(std::string("source_overruns_at_release_") + role) == 0);
            else if (!heavy)
                CHECK(number(source_at_stop_request, std::string(role) + ".source_overruns") == 0);
        }
        const auto generation = run.state.packets[0]->header.metadata.at("generation");
        const auto historical = canonical(*run.state.packets[0]);
        // Restart the real instance; historical buffers/epochs must stay immutable.
        CHECK(streams[0]->start());
        auto restarted = streams[0]->next();
        CHECK(streams[0]->stop() && restarted && *restarted);
        CHECK((*restarted)->header.sequence.value == 0);
        CHECK((*restarted)->header.metadata.at("generation") != generation);
        exact(**restarted, profile, 0, (*restarted)->header.metadata.at("generation"), oracle);
        CHECK(canonical(*run.state.packets[0]) == historical);
        std::set<const void *> buffers;
        for (size_t n = 0; n < count; ++n) {
            exact(*run.state.packets[n], profile, n, generation, oracle, heavy && source_loss);
            if (n)
                CHECK(run.state.packets[n]->frames[0]->header.sequence.value >
                      run.state.packets[n - 1]->frames[0]->header.sequence.value);
            retired.push_back(run.state.packets[n]);
            for (const auto &child : run.state.packets[n]->frames)
                CHECK(buffers.insert(child->attributes[0].buffer.identity()).second);
        }
        const auto error = run.session->error();
        const auto raw_before = store->get(raw);
        CHECK(raw_before.state == artifact::ArtifactState::open);
        std::string initial_state;
        if (!error.empty()) {
            store->abandon(raw);
            CHECK(store->get(raw).state == artifact::ArtifactState::recoverable);
            initial_state = "RECOVERABLE-after-abandon";
        } else {
            store->prepare_finalize(raw);
            CHECK(store->finalize(raw).state == artifact::ArtifactState::finalized);
            initial_state = "FINALIZED";
        }
        const auto finalize_or_abandon_ended = now();
        const auto reopen_began = now();
        store.reset();
        store = std::make_shared<artifact::Store>(dir.path / "Recording.mantis");
        if (!error.empty()) {
            CHECK(store->get(raw).state == artifact::ArtifactState::recoverable);
            CHECK(store->recover(raw).state == artifact::ArtifactState::finalized);
        }
        const auto recovery_ended = now();
        const auto artifact = store->get(raw);
        CHECK(artifact.provenance.producer == provenance.producer);
        CHECK(artifact.provenance.version.major == provenance.version.major &&
              artifact.provenance.version.minor == provenance.version.minor &&
              artifact.provenance.version.patch == provenance.version.patch);
        for (const auto &[key, value] : provenance.parameters)
            CHECK(artifact.provenance.parameters.at(key) == value);
        CHECK(artifact.type.schema_version == 2 &&
              artifact.provenance.parameters.at("capture_id") == capture.value);
        CHECK(artifact.provenance.calibration.id.value == profile.at("calibration_id").get<std::string>());
        CHECK(store->record_count(raw) == count && artifact.chunks >= 2);
        const auto initial_observations =
            Json::parse(artifact.provenance.parameters.at("initial_observations"));
        CHECK(initial_observations.size() == 2);
        for (size_t camera = 0; camera < 2; ++camera)
            CHECK(initial_observations[camera].at("clock_domain").at("id") ==
                  run.state.packets[0]->frames[camera]->header.timestamp.domain.id.value);
        uint64_t disk_records{};
        for (uint64_t segment = 0; segment < artifact.chunks; ++segment) {
            const auto scan = artifact::segments::scan(store->object_path(raw, segment));
            CHECK(!scan.incomplete && scan.corruption.empty() && !scan.records.empty());
            for (size_t record = 0; record < scan.records.size(); ++record) {
                CHECK(artifact::segments::packet(scan, record)->header.sequence.value == disk_records++);
            }
        }
        CHECK(disk_records == count);
        uint64_t mapped_views{}, mapped_packet_backing_max{};
        for (unsigned pass = 0; pass < 2; ++pass) {
            auto replay = device::recorded_source(store, raw, false);
            CHECK(replay->start());
            for (size_t n = 0; n < count; ++n) {
                const auto value = replay->next();
                CHECK(value && *value);
                exact(**value, profile, n, generation, oracle, heavy && source_loss);
                CHECK(canonical(**value) == canonical(*run.state.packets[n]));
                std::set<const void *> mapped_backings;
                uint64_t mapped_bytes{};
                for (const auto &frame : (*value)->frames)
                    for (const auto &attribute : frame->attributes) {
                        CHECK(attribute.buffer.domain() == memory::MemoryDomain::shared_memory);
                        ++mapped_views;
                        if (mapped_backings.insert(attribute.buffer.identity()).second)
                            mapped_bytes += attribute.buffer.backing_size();
                    }
                mapped_packet_backing_max = std::max(mapped_packet_backing_max, mapped_bytes);
            }
            auto end = replay->next();
            CHECK(end && !*end && replay->stop());
        }
        std::vector<int64_t> callback_times, wait_times;
        size_t failed{};
        int64_t first_failed_admission_ns{-1}, failed_admission_duration_ns{};
        for (size_t i = 0; i < run.state.admitted; ++i) {
            const auto &a = run.state.admissions[i];
            wait_times.push_back(a.ended - a.began);
            failed += !a.accepted;
            if (!a.accepted && first_failed_admission_ns < 0) {
                first_failed_admission_ns = a.ended;
                failed_admission_duration_ns = a.ended - a.began;
            }
        }
        CHECK(failed == (saturated ? 1u : 0u));
        uint64_t recorded_bytes{}, window_bytes{}, before_bytes{}, after_bytes{}, drain_bytes{};
        const size_t warmup = heavy ? 8 : 2;
        const auto window_start = run.state.callbacks[std::min(warmup, count - 1)].began;
        const auto window_end = run.state.callbacks[count - 1].ended;
        const auto split = collapse ? fault.at("healthy_boundary_ns").get<int64_t>() : window_end;
        const auto degraded_end =
            collapse ? (first_failed_admission_ns >= 0 ? first_failed_admission_ns
                                                       : fault.at("degraded_end_ns").get<int64_t>())
                     : window_end;
        for (size_t i = 0; i < count; ++i) {
            const auto &c = run.state.callbacks[i];
            callback_times.push_back(c.ended - c.began);
            recorded_bytes += c.payload;
            if (i >= warmup) {
                window_bytes += c.payload;
                if (c.ended <= split)
                    before_bytes += c.payload;
                else if (c.ended <= degraded_end)
                    after_bytes += c.payload;
                else
                    drain_bytes += c.payload;
            }
        }
        Json event_samples = Json::array();
        for (size_t i = 0; i < count; ++i) {
            const auto &a = run.state.admissions[i];
            const auto &c = run.state.callbacks[i];
            const auto &left = run.state.packets[i]->frames[0]->header;
            const auto &right = run.state.packets[i]->frames[1]->header;
            CHECK(a.accepted && a.ended >= a.began && c.began >= a.began && c.ended >= c.began);
            CHECK(a.began >= left.received.nanoseconds && a.began >= right.received.nanoseconds);
            if (i < 4 || i + 4 >= count)
                event_samples.push_back(
                    {{"publication_sequence", i},
                     {"left_native_sequence", left.sequence.value},
                     {"right_native_sequence", right.sequence.value},
                     {"exposure_reference_ns", number(left.metadata, "capture_event_ns")},
                     {"reference_clock", left.metadata.at("capture_event_clock")},
                     {"reference_origin_host_ns", number(left.metadata, "reference_origin_host_ns")},
                     {"left_received_steady_ns", left.received.nanoseconds},
                     {"right_received_steady_ns", right.received.nanoseconds},
                     {"source_processing_started_steady_ns", number(left.metadata, "processing_ns")},
                     {"session_admission_begin_steady_ns", a.began},
                     {"session_admission_end_steady_ns", a.ended},
                     {"callback_begin_steady_ns", c.began},
                     {"callback_end_steady_ns", c.ended}});
        }
        Json delivery_fps;
        for (size_t camera = 0; camera < 2; ++camera) {
            const auto elapsed_delivery =
                run.state.packets[count - 1]->frames[camera]->header.received.nanoseconds -
                run.state.packets[0]->frames[camera]->header.received.nanoseconds;
            delivery_fps[camera ? "right" : "left"] =
                elapsed_delivery > 0 ? double(count - 1) * 1e9 / double(elapsed_delivery) : 0;
        }
        uint64_t directory_bytes{};
        for (const auto &entry : std::filesystem::recursive_directory_iterator(store->root()))
            if (entry.is_regular_file())
                directory_bytes += entry.file_size();
        CHECK(directory_bytes < 512ull * 1024 * 1024);
        Json stage_report = Json::array();
        size_t durable_publications{};
        for (size_t i = 0; i < stages.count; ++i) {
            const auto &s = stages.samples[i];
            stage_report.push_back({{"stage", s.point}, {"steady_ns", s.ns}});
            durable_publications += std::string_view(s.point) == "chunk-committed";
        }
        const auto elapsed = stop_ended - run.began;
        const auto window_ns = window_end - window_start;

        report = {
            {"mode", mode},
            {"format", profile.at("format")},
            {"width", profile.at("width")},
            {"height", profile.at("height")},
            {"requested_fps_per_camera", profile.at("cameras")[0].at("rate_num")},
            {"verdict", error.empty() && !source_loss ? "RECORDED"
                        : saturated                   ? "EXPLICIT_RECOVERED_SATURATION"
                                                      : "HOST_SOURCE_LIMITATION"},
            {"full_fidelity_success", error.empty() && !source_loss},
            {"primary_error", error},
            {"event_samples", event_samples},
            {"clock", "steady_clock absolute ns for fixture/admission/callback/Store; device/reference "
                      "domains separately verified"},
            {"session_diagnostics", d},
            {"source_accounting", counts},
            {"source_at_stop_request", source_at_stop_request},
            {"fault", fault},
            {"recorded_payload_bytes", recorded_bytes},
            {"output_artifact_bytes", artifact.bytes},
            {"directory_bytes_including_metadata", directory_bytes},
            {"visible_segment_bytes_after_join", written_segment_bytes},
            {"store_visible_bytes_per_capture_wall_MB_s",
             double(written_segment_bytes) * 1e3 / double(stop_ended - run.began)},
            {"durably_published_chunks_before_finalize", raw_before.chunks},
            {"durably_published_bytes_before_finalize", raw_before.bytes},
            {"mapped_replay_views_verified", mapped_views},
            {"observed_mapped_packet_backing_max_bytes", mapped_packet_backing_max},
            {"segment_max_bytes", provenance.parameters.at("segment_max_bytes")},
            {"per_camera_observed_delivery_fps", delivery_fps},
            {"callback_success_samples", count},
            {"finalize_or_abandon_ns", finalize_or_abandon_ended - stop_ended},
            {"retained_packet_buffers", buffers.size()},
            {"retained_payload_bytes", recorded_bytes},
            {"host_pixel_allocations", number(source, "host_pixel_allocations")},
            {"materialized_pixel_bytes", number(source, "materialized_pixel_bytes")},
            {"window",
             {{"warmup_callbacks", warmup},
              {"start_ns", window_start},
              {"end_ns", window_end},
              {"callback_samples", count > warmup ? count - warmup : 0},
              {"actual_payload_MB_s", window_ns > 0 ? double(window_bytes) * 1e3 / double(window_ns) : 0},
              {"callback_success_fps",
               window_ns > 0 ? double(count > warmup ? count - warmup : 0) * 1e9 / double(window_ns) : 0},
              {"healthy_payload_bytes", before_bytes},
              {"degraded_payload_bytes", after_bytes},
              {"post_failure_drain_payload_bytes", drain_bytes},
              {"healthy_window_ns", split - window_start},
              {"healthy_actual_payload_MB_s",
               split > window_start ? double(before_bytes) * 1e3 / double(split - window_start) : 0},
              {"degraded_window_ns", collapse ? degraded_end - split : 0},
              {"degraded_actual_payload_MB_s", collapse && degraded_end > split
                                                   ? double(after_bytes) * 1e3 / double(degraded_end - split)
                                                   : 0}}},
            {"capture_elapsed_ns", elapsed},
            {"stop_after_release_ns", stop_ended - stop_began},
            {"reopen_recovery_ns", recovery_ended - reopen_began},
            {"same_packet_stream_microprobe", stream_probe},
            {"callback_latency", distribution(callback_times)},
            {"admission_wait", distribution(wait_times)},
            {"first_failed_admission_steady_ns", first_failed_admission_ns},
            {"failed_admission_duration_ns", failed_admission_duration_ns},
            {"durable_segment_publications", durable_publications},
            {"artifact_chunks", artifact.chunks},
            {"artifact_initial_state", initial_state},
            {"artifact_final_state", "FINALIZED"},
            {"two_byte_exact_replays", true},
            {"monitor_samples", run.monitor_count},
            {"monitor_ns", run.monitor_ns},
            {"resource_before", run.before.json()},
            {"resource_after_stop", after_stop.json()},
            {"resource_sampled_peak", run.peak.json()},
            {"cpu_percent_one_core",
             double(after_stop.cpu_ns - run.before.cpu_ns) * 100.0 / double(stop_ended - run.began)},
            {"queue_progression", run.queue},
            {"store_stages", stage_report}};
        run.session.reset();
        CHECK(test::environment("MANTIS_VIRTUAL_RIG_PROFILE", nullptr) == 0);
    }
    for (const auto &packet : retired)
        CHECK(packet.expired());
    const auto cleaned = resources();
    CHECK(cleaned.fds == outside.fds && cleaned.threads == outside.threads);
    CHECK(!std::filesystem::exists(removed_directory));
    report["owned_directory_removed"] = true;
    report["resource_after_cleanup"] = cleaned.json();
    report["retired_packet_owners"] = retired.size();
    return report;
}
// A public service startup failure must retain the same first cause and must
// not manufacture a successful capture/finalization handle. The Store gate is
// inside the real first append; the async call remains blocked until release.
Json startup_failure(const std::filesystem::path &library, Json profile,
                     const std::filesystem::path &parent) {
    const auto before = resources();
    const PixelOracle oracle(profile);
    Directory dir(parent);
    const auto plugin_dir = dir.path / "plugins";
    std::filesystem::create_directory(plugin_dir);
    std::filesystem::copy_file(library, plugin_dir / library.filename());
    std::filesystem::copy_file(library.parent_path() / "virtual-rig.json", plugin_dir / "virtual-rig.json");
    const auto config = dir.path / "rig.json";
    {
        std::ofstream out(config);
        out << profile.dump();
        out.close();
        CHECK(out.good());
    }
    CHECK(test::environment("MANTIS_VIRTUAL_RIG_PROFILE", config.string().c_str()) == 0);
    const auto root = dir.path / "Service.mantis";
    Id raw;
    size_t accepted{};
    Json evidence;
    {
        Run run;
        services::Runtime runtime({plugin_dir, {}, root, {}, {"org.mantis.virtual-rig"}, {}});
        const auto devices = runtime.devices();
        const auto parent_device = std::find_if(devices.begin(), devices.end(), [](const auto &d) {
            return d.plugin_id == "org.mantis.virtual-rig" && d.parent.value.empty();
        });
        CHECK(parent_device != devices.end());
        run.arm = std::make_unique<st::Arm>(st::Plan{root,
                                                     {},
                                                     "raw-first-record-before",
                                                     st::Action::pause,
                                                     0,
                                                     0,
                                                     0,
                                                     run.barrier.entered[1],
                                                     run.barrier.released[0]});
        std::future<std::string> attempt;
        struct Release {
            Barrier &barrier;
            ~Release() {
                barrier.release_noexcept();
            }
        } release{run.barrier}; // Releases before future or Runtime destruction on every exception.
        attempt = std::async(std::launch::async, [&] {
            try {
                runtime.start_capture({parent_device->id});
                return std::string("unexpected successful startup");
            } catch (const std::exception &error) {
                return std::string(error.what());
            }
        });
        const auto event = st::await_event(run.barrier.entered[0], Clock::now() + 5s);
        CHECK(std::string(event.point) == "raw-first-record-before");
        run.until(
            [&] {
                std::lock_guard lock(run.state.mutex);
                return run.state.admitted && !run.state.admissions[run.state.admitted - 1].accepted;
            },
            "service startup failed admission");
        CHECK(attempt.wait_for(0ms) != std::future_status::ready);
        run.barrier.release();
        CHECK(attempt.wait_for(5s) == std::future_status::ready);
        const auto error = attempt.get();
        CHECK(error.find("LOSSLESS writer queue saturated") != std::string::npos);
        CHECK(runtime.captures().empty());
        for (const auto &record : runtime.events(0)) {
            if (record.component == "capture.diagnostics")
                evidence = Json::parse(record.message);
        }
        CHECK(evidence.at("recorder_error") == error);
        CHECK(evidence.at("recorder_secondary_error") == "");
        CHECK(evidence.at("recorder_queue_high_water") == "32");
        CHECK(evidence.at("recorder_producer_running") == "0" &&
              evidence.at("recorder_writer_running") == "0");
        CHECK(evidence.at("recorder_queue_occupancy") == "0");
        const auto artifacts = runtime.artifacts();
        CHECK(artifacts.size() == 1 && artifacts[0].type.schema_version == 2);
        CHECK(artifacts[0].state == artifact::ArtifactState::recoverable);
        raw = artifacts[0].id;
        for (size_t i = 0; i < run.state.admitted; ++i)
            accepted += run.state.admissions[i].accepted;
        CHECK(accepted == 33 && run.state.admitted == accepted + 1);
        CHECK(evidence.at("recorder_committed") == std::to_string(accepted));
        run.arm.reset();
    }
    auto store = std::make_shared<artifact::Store>(root);
    CHECK(store->get(raw).state == artifact::ArtifactState::recoverable);
    CHECK(store->recover(raw).state == artifact::ArtifactState::finalized);
    CHECK(store->record_count(raw) == accepted);
    std::vector<std::string> reference;
    std::string generation;
    for (unsigned pass = 0; pass < 2; ++pass) {
        auto source = device::recorded_source(store, raw, false);
        CHECK(source->start());
        for (size_t i = 0; i < accepted; ++i) {
            auto next = source->next();
            CHECK(next && *next);
            if (generation.empty())
                generation = (*next)->header.metadata.at("generation");
            exact(**next, profile, i, generation, oracle);
            if (!pass)
                reference.push_back(canonical(**next));
            else
                CHECK(canonical(**next) == reference[i]);
        }
        auto end = source->next();
        CHECK(end && !*end && source->stop());
    }
    CHECK(test::environment("MANTIS_VIRTUAL_RIG_PROFILE", nullptr) == 0);
    store.reset();
    std::filesystem::remove_all(dir.path);
    CHECK(!std::filesystem::exists(dir.path));
    const auto after = resources();
    CHECK(after.fds == before.fds && after.threads == before.threads);
    return {{"resource_before", before.json()}, {"resource_after_cleanup", after.json()},
            {"owned_directory_removed", true},  {"mode", "public-service-startup-saturation"},
            {"failure_evidence", evidence},     {"recovered_prefix", accepted},
            {"two_byte_exact_replays", true}};
}
} // namespace
namespace mantis::device::qualification_test {
void admission(int64_t began, int64_t ended, bool accepted) noexcept {
    if (auto *s = sampler.load()) {
        std::lock_guard lock(s->mutex);
        if (s->admitted < s->admissions.size())
            s->admissions[s->admitted++] = {began, ended, accepted};
        else
            ++s->admission_overflow;
        s->signal();
    }
}
} // namespace mantis::device::qualification_test
int main(int argc, char **argv) {
    try {
        CHECK(argc == 4 || argc == 5);
        const std::string mode = argv[1];
        CHECK(mode == "regression" || mode == "benchmark" || mode == "collapse");
        std::ifstream input(argv[3]);
        Json p = Json::parse(input);
        CHECK(p.at("width") == 1280 && p.at("height") == 720 && p.at("stride") == 1600);
        CHECK(p.at("format") == "Y10P" && p.at("seed") == 17 && p.at("mode") == "trigger");
        CHECK(p.at("cameras").size() == 2 && p.at("faults").empty());
        for (const auto &camera : p.at("cameras")) {
            CHECK(camera.at("rate_num") == 120 && camera.at("rate_den") == 1);
            for (const auto field :
                 {"phase_ns", "latency_ns", "offset_ns", "drift_ppm", "jitter_ns", "delivery_ns"})
                CHECK(camera.at(field) == 0);
            CHECK(camera.at("comparable") == true);
        }
        const auto parent = std::filesystem::absolute(argc == 5 ? std::filesystem::path(argv[4])
                                                                : std::filesystem::temp_directory_path())
                                .lexically_normal();
        CHECK(std::filesystem::is_directory(parent));
        CHECK(std::filesystem::space(parent).available >=
              (mode == "regression" ? 512ull : 1024ull) * 1024 * 1024);
        const auto cpu = resources();
        const auto overhead_start = now();
        State overhead;
        sampler = &overhead;
        // Fixed-size sampler microprobe: includes two clock reads, one stored
        // sample, coordinator notification and loop/reset cost. No pixel/I/O work.
        for (size_t i = 0; i < 10000; ++i) {
            overhead.admitted = 0; // Every iteration measures a stored sample, not overflow.
            device::qualification_test::admission(now(), now(), true);
        }
        sampler = nullptr;
        const auto sampler_ns = double(now() - overhead_start) / 10000;
        double storage_trace_ns{};
        {
            st::Trace trace(parent, "microprobe");
            const auto began = now();
            for (size_t i = 0; i < 1000; ++i)
                st::checkpoint("segment-sync-before", parent, "microprobe");
            storage_trace_ns = double(now() - began) / 1000;
            CHECK(trace.count == 1000 && trace.overflow == 0);
        }
        Json result{{"version", 1},
                    {"sampler_microprobe_iterations", 10000},
                    {"sampler_ns_per_call", sampler_ns},
                    {"store_trace_microprobe_samples", 1000},
                    {"store_trace_ns_per_checkpoint", storage_trace_ns},
                    {"cpu_scope", "whole process; one logical core=100%, no physical-device claim"},
                    {"frame_bound_per_run", bound},
                    {"payload_bound_per_run_bytes", bound * 2304000ull},
                    {"cases", Json::array()}};
        if (mode == "regression") {
            for (auto &camera : p["cameras"])
                camera["rate_num"] = 30;
            auto community = p;
            community["width"] = 640;
            community["height"] = 480;
            community["stride"] = 640;
            community["format"] = "GREY";
            community["calibration_id"] = "org.mantis.virtual-rig.pinhole-640x480.v1";
            result["cases"].push_back(exercise(argv[2], community, "matrix-community", parent));
            // Latency recovery requires a feasible post-release service budget.
            // The same real plugin/Session/Store path uses small stereo payloads
            // here so Debug sanitizer hash/encode cost does not turn a latency
            // spike into an unintended sustained-bandwidth-deficit experiment.
            // Real-size integrity and full-rate deficit modes remain separate.
            auto latency = community;
            latency["width"] = 64;
            latency["height"] = 48;
            latency["stride"] = 64;
            latency["calibration_id"] = "org.mantis.virtual-rig.pinhole-64x48.v1";
            for (const auto scenario : {"latency-control", "transient", "prolonged"}) {
                result["cases"].push_back(exercise(argv[2], latency, scenario, parent));
                if (std::string_view(scenario) == "prolonged")
                    result["cases"].push_back(startup_failure(argv[2], latency, parent));
            }
            result["cases"].push_back(exercise(argv[2], p, "healthy", parent));
        } else
            result["cases"].push_back(exercise(argv[2], p, mode, parent));
        result["process_cpu_ns"] = resources().cpu_ns - cpu.cpu_ns;
        std::cout << result.dump(2) << '\n';
        if (mode == "benchmark" && !result["cases"][0].at("full_fidelity_success").get<bool>())
            return 2;
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
