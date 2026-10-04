#include "x1/backend.hpp"
#include "x1/pairing.hpp"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <mantis/sdk.hpp>
#include <nlohmann/json.hpp>
namespace {
using Json = nlohmann::json;
struct Configuration {
    x1::Profile profile;
    std::unique_ptr<x1::Backend> backend;
};
Configuration configuration() {
    auto path = std::getenv("MANTIS_X1_PROFILE");
    if (!path || !*path) throw std::runtime_error("Set MANTIS_X1_PROFILE to an explicit left/right device profile");
    auto fake = std::getenv("MANTIS_X1_FAKE");
    return {x1::load_profile(path), fake && *fake ? x1::fake_backend(fake) : x1::linux_backend()};
}
std::string identity(const x1::CameraInfo &c) { return c.bus + "/" + c.sensor; }
std::string parent_id(const std::array<x1::CameraInfo, 2> &c) { return "org.mantis.x1:" + identity(c[0]) + ":" + identity(c[1]); }
int enumerate(MantisDiscoverEmitV1 emit, void *ctx) noexcept {
    return mantis::sdk::boundary([&] {
        if (!emit) throw std::runtime_error("Missing discovery callback");
        // No implicit role assignment, nor fake fallback on a machine without hardware.
        if (!std::getenv("MANTIS_X1_PROFILE")) return;
        auto config = configuration();
        auto cameras = x1::assign(config.profile, config.backend->discover(config.profile));
        auto parent = parent_id(cameras);
        const char *parent_caps[]{MANTIS_FRAMESET_STREAM_V1};
        x1::Metadata info{{"profile", config.profile.json}, {"buffer_mode", "V4L2 MMAP + one acquisition copy"},
            {"copy_count", "1"}, {"profile_status", "configured"}, {"producer_version", "0.2.0"},
            {"host_receive_clock", "linux.monotonic"},
            {"device_timestamp_clock", "V4L2 clock/source flags preserved per observation"},
            {"sync_configuration", config.profile.hardware_sync_configured ? "hardware sync configured" : "hardware sync not configured"}};
        info["backend"] = std::getenv("MANTIS_X1_FAKE") ? "deterministic fixture" : "Linux V4L2";
        auto metadata = Json(info).dump();
        MantisDiscoveredDeviceV1 d{sizeof(d), 1, parent.c_str(), "", "Mantis X1", parent_caps, 1, metadata.c_str()};
        mantis::sdk::check(emit(ctx, &d));
        for (size_t i = 0; i < 2; ++i) {
            auto id = parent + (i ? "/right" : "/left");
            const char *caps[]{MANTIS_IMAGE_STREAM_V1};
            auto json = Json(x1::Metadata{{"role", i ? "right" : "left"}, {"identity", identity(cameras[i])},
                {"video_node", cameras[i].video}, {"sensor", cameras[i].sensor},
                {"width", std::to_string(config.profile.mode.width)}, {"height", std::to_string(config.profile.mode.height)},
                {"fourcc", config.profile.mode.fourcc}, {"target_fps", std::to_string(config.profile.mode.fps)}}).dump();
            d = {sizeof(d), 1, id.c_str(), parent.c_str(), i ? "Measurement Camera Right" : "Measurement Camera Left", caps, 1, json.c_str()};
            mantis::sdk::check(emit(ctx, &d));
        }
    });
}
struct Pending {
    std::unique_ptr<mantis::sdk::Buffer> buffer;
    x1::FrameView frame;
    std::string metadata;
    uint64_t bytes{};
};
struct Device {
    const MantisHostV1 *host;
    Configuration config;
    std::array<x1::CameraInfo, 2> cameras;
    std::array<std::unique_ptr<x1::Camera>, 2> streams;
    std::unique_ptr<x1::Setup> setup;
    std::array<x1::PendingQueue<Pending>, 2> pending;
    std::array<std::optional<uint32_t>, 2> last_sequence;
    std::array<std::optional<int64_t>, 2> last_timestamp;
    std::array<std::string, 2> last_clock;
    std::array<int64_t, 2> last_receive{};
    x1::Metadata metrics;
    uint64_t sequence{};
    int64_t started{};
    bool running{};
    bool aligned{};
    uint64_t startup_discarded{};
    void pending_depth() {
        for (size_t i = 0; i < 2; ++i) {
            const std::string role = i ? "right" : "left";
            metrics["pending_" + role] = std::to_string(pending[i].size());
            auto &high = metrics["pending_high_water_" + role];
            high = std::to_string(std::max(high.empty() ? size_t{} : size_t(std::stoull(high)), pending[i].size()));
        }
    }
    void stop() noexcept {
        // Lookahead/startup observations are not published FrameSets. Account for
        // every retained tail on stop/failure; the recorder itself never drops.
        try {
            for (size_t i = 0; i < 2; ++i) if (!pending[i].empty())
                increment(i ? "shutdown_unmatched_right" : "shutdown_unmatched_left", pending[i].size());
        } catch (...) {}
        for (auto &queue : pending) queue.clear();
        try { pending_depth(); } catch (...) {}
        for (size_t i = 0; i < streams.size(); ++i) if (streams[i]) {
            streams[i]->stop();
            try { for (const auto &[key, value] : streams[i]->diagnostics()) metrics[(i ? "right_" : "left_") + key] = value; } catch (...) {}
        }
        streams = {}; running = false;
        if (setup) {
            try { setup->rollback(); } catch (const std::exception &e) {
                try { metrics["setup_rollback_error"] = e.what(); } catch (...) {}
            }
            setup.reset();
        }
    }
    ~Device() { stop(); }
    void increment(const std::string &key, uint64_t n = 1) {
        auto &value = metrics[key]; value = std::to_string((value.empty() ? 0 : std::stoull(value)) + n);
    }
    template<class F> int guard(F &&fn) noexcept {
        try { fn(); return 0; }
        catch (const std::exception &e) { try { metrics["error"] = e.what(); } catch (...) {} stop(); return 1; }
        catch (...) { try { metrics["error"] = "Unknown acquisition error"; } catch (...) {} stop(); return 1; }
    }
};
int open_device(const MantisHostV1 *host, const char *id, void **out) noexcept {
    return mantis::sdk::boundary([&] {
        if (!mantis::sdk::compatible(host) || !id || !out) throw std::runtime_error("Invalid acquisition open");
        auto config = configuration();
        auto cameras = x1::assign(config.profile, config.backend->discover(config.profile));
        if (id != parent_id(cameras)) throw std::runtime_error("Selected acquisition identity no longer available");
        auto device = std::make_unique<Device>(); device->host = host;
        device->config = std::move(config); device->cameras = std::move(cameras);
        *out = device.release();
    });
}
void destroy(void *p) { delete static_cast<Device *>(p); }
int start(void *p) noexcept {
    auto &d = *static_cast<Device *>(p);
    return d.guard([&] {
        d.stop(); d.metrics.clear(); d.sequence = 0; d.last_sequence = {}; d.last_timestamp = {}; d.last_clock = {};
        d.aligned = false; d.startup_discarded = 0;
        d.setup = d.config.backend->prepare(d.config.profile, d.cameras);
        if (d.config.profile.format_version == 2 && !d.setup)
            throw std::runtime_error("Profile v2 requires plugin-owned media setup; backend does not provide it");
        for (size_t i = 0; i < 2; ++i) d.streams[i] = d.config.backend->open(d.cameras[i], d.config.profile.mode);
        for (auto &stream : d.streams) stream->start();
        if (d.setup) d.metrics = d.setup->diagnostics();
        for (size_t i = 0; i < 2; ++i) for (const auto &[key, value] : d.streams[i]->diagnostics())
            d.metrics[(i ? "right_" : "left_") + key] = value;
        if (d.setup) d.setup->commit();
        d.started = x1::monotonic_ns(); d.last_receive.fill(d.started); d.running = true;
        d.metrics["sync_configuration"] = d.config.profile.hardware_sync_configured ? "hardware sync configured" : "hardware sync not configured";
        d.metrics["sync_quality"] = "software";
        d.metrics["hardware_sync_configured"] = d.config.profile.hardware_sync_configured ? "true" : "false";
        d.metrics["pairing_mode"] = d.config.profile.hardware_sync_configured ? "native-sequence-and-timestamp" : "timestamp-nearest";
        d.metrics["pairing_state"] = "startup";
        d.metrics["pairing_pending_capacity_per_camera"] = std::to_string(x1::pairing_capacity);
        d.metrics["startup_discard_limit"] = std::to_string(x1::startup_discard_limit);
        d.metrics["max_v4l2_delta_ns"] = std::to_string(d.config.profile.max_timestamp_delta_ns);
        for (const char *key : {"left.native_sequence", "right.native_sequence", "native_sequence_offset",
             "native_counter_equality", "paired_v4l2_delta_ns", "v4l2_delta_ns", "host_arrival_delta_ns"})
            d.metrics[key] = "unavailable";
        d.metrics["exposure_skew"] = "unavailable";
        d.metrics["copy_count"] = "1";
        d.metrics["buffer_mode"] = "MMAP with one acquisition copy";
        for (size_t i = 0; i < 2; ++i) {
            std::string role = i ? "right." : "left.";
            d.metrics[role + "identity"] = identity(d.cameras[i]); d.metrics[role + "video_node"] = d.cameras[i].video;
            d.metrics[role + "target_fps"] = std::to_string(d.config.profile.mode.fps);
            for (const char *counter : {"frames", "sequence_gaps", "capture_errors"}) d.metrics[role + counter] = "0";
        }
        for (const char *counter : {"pairing_failures", "timestamp_discontinuities", "unmatched_frames",
             "startup_unmatched_left", "startup_unmatched_right", "shutdown_unmatched_left", "shutdown_unmatched_right",
             "pairing_pending_saturation"}) d.metrics[counter] = "0";
        d.pending_depth();
    });
}
MantisObservationV1 observation(const Device &d, size_t i, MantisAttributeV1 &a) {
    const auto &p = d.pending[i][0]; const auto &f = p.frame;
    a = {sizeof(a), 1, "org.mantis.pixels", "intensity", 1, 2,
        {f.height, f.width}, {f.stride, 1}, p.buffer->get(), 0, p.bytes};
    if (f.fourcc == "Y10P") {
        a.name = "org.mantis.image.packed_bytes"; a.unit = "byte";
        a.rank = 1; a.shape[0] = p.bytes; a.shape[1] = 0;
        a.stride[0] = 1; a.stride[1] = 0;
    }
    MantisPacketV1 packet{sizeof(packet), 1, MANTIS_IMAGE, 1, f.sequence, f.timestamp_ns,
        f.clock.c_str(), d.config.profile.calibration_id.c_str(), d.config.profile.calibration_revision,
        i ? "org.mantis.camera.right.optical" : "org.mantis.camera.left.optical", &a, 1};
    return {sizeof(MantisObservationV1), 1, packet, f.received_ns, "org.mantis.x1.measurement", d.sequence, 1, p.metadata.c_str()};
}
int next(void *p, uint32_t timeout, MantisFrameSetEmitV1 emit, void *ctx) noexcept {
    auto &d = *static_cast<Device *>(p); bool emitted = false;
    auto status = d.guard([&] {
        if (!d.running || !emit || timeout > 1000) throw std::runtime_error("Invalid next call/stopped stream");
        unsigned reads{};
        while (true) {
            if (!d.aligned && x1::monotonic_ns() - d.started > int64_t(d.config.profile.stall_ms) * 1000000) {
                d.increment("pairing_failures"); throw std::runtime_error("Startup timestamp alignment timed out");
            }
            std::array<std::array<x1::PairingObservation, x1::pairing_capacity>, 2> observations{};
            for (size_t i = 0; i < 2; ++i) for (size_t j = 0; j < d.pending[i].size(); ++j) {
                const auto &f = d.pending[i][j].frame;
                observations[i][j] = {f.sequence, f.timestamp_ns, f.clock};
            }
            for (size_t i = 0; i < 2; ++i) {
                const auto suffix = i ? "right" : "left";
                d.metrics[std::string("pairing_candidate_sequence_") + suffix] = d.pending[i].empty()
                    ? "unavailable" : std::to_string(d.pending[i][0].frame.sequence);
                d.metrics[std::string("pairing_candidate_timestamp_ns_") + suffix] = d.pending[i].empty()
                    ? "unavailable" : std::to_string(d.pending[i][0].frame.timestamp_ns);
                d.metrics[std::string("pairing_lookahead_timestamp_ns_") + suffix] = d.pending[i].size() < 2
                    ? "unavailable" : std::to_string(d.pending[i][1].frame.timestamp_ns);
            }
            auto decision = x1::choose_pair({observations[0].data(), d.pending[0].size()},
                {observations[1].data(), d.pending[1].size()}, d.config.profile.hardware_sync_configured,
                static_cast<uint64_t>(d.config.profile.max_timestamp_delta_ns), d.aligned);
            if (decision.action == x1::PairAction::fail) {
                d.increment("pairing_failures"); throw std::runtime_error(decision.error);
            }
            if (decision.action == x1::PairAction::pair) break;
            if (decision.action == x1::PairAction::discard_left || decision.action == x1::PairAction::discard_right) {
                if (d.startup_discarded == x1::startup_discard_limit) {
                    d.increment("pairing_failures"); throw std::runtime_error("Startup timestamp alignment exceeded unmatched-observation limit");
                }
                size_t i = decision.action == x1::PairAction::discard_right ? 1 : 0;
                d.pending[i].pop(); ++d.startup_discarded;
                d.increment(i ? "startup_unmatched_right" : "startup_unmatched_left");
                d.pending_depth(); continue;
            }
            if (reads == 2) return; // bounded work and caller timeout, even while aligning
            ++reads;
            size_t i = decision.action == x1::PairAction::right ? 1 : 0;
            std::string role = i ? "right." : "left.";
            if (d.pending[i].size() == x1::pairing_capacity) {
                d.increment("pairing_pending_saturation"); d.increment("pairing_failures");
                throw std::runtime_error(role + "pairing pending queue saturated");
            }
            bool received{};
            try {
                received = d.streams[i]->next(timeout / 2, [&](const x1::FrameView &frame) {
                    d.increment(role + "frames");
                    if (d.last_sequence[i] && frame.sequence != static_cast<uint32_t>(*d.last_sequence[i] + 1)) {
                        uint32_t delta = frame.sequence - *d.last_sequence[i];
                        if (delta > 1 && delta < 0x80000000u) d.increment(role + "sequence_gaps", delta - 1);
                        else d.increment(role + "repeated_or_reversed_sequences");
                        throw std::runtime_error(role + "native sequence discontinuity; raw capture failed");
                    }
                    if (d.last_timestamp[i] && frame.timestamp_ns <= *d.last_timestamp[i]) {
                        d.increment("timestamp_discontinuities"); throw std::runtime_error(role + "timestamp discontinuity");
                    }
                    if (!d.last_clock[i].empty() && d.last_clock[i] != frame.clock) {
                        d.increment("timestamp_discontinuities"); throw std::runtime_error(role + "timestamp clock discontinuity");
                    }
                    d.last_clock[i] = frame.clock;
                    d.last_sequence[i] = frame.sequence; d.last_timestamp[i] = frame.timestamp_ns;
                    d.last_receive[i] = x1::monotonic_ns();
                    auto elapsed = double(d.last_receive[i] - d.started) / 1e9;
                    d.metrics[role + "receive_fps"] = elapsed > 0 ? std::to_string(double(std::stoull(d.metrics.at(role + "frames"))) / elapsed) : "unavailable";
                    d.metrics[role + "last_received_native_sequence"] = std::to_string(frame.sequence);
                    d.metrics[role + "width"] = std::to_string(frame.width); d.metrics[role + "height"] = std::to_string(frame.height);
                    d.metrics[role + "fourcc"] = frame.fourcc;
                    d.metrics[role + "stride"] = std::to_string(frame.stride);
                    d.metrics[role + "buffer_size"] = std::to_string(frame.buffer_size);
                    d.metrics[role + "timestamp_clock"] = frame.clock;
                    d.metrics[role + "timestamp_flags"] = std::to_string(frame.flags);
                    Pending value; value.frame = frame; value.bytes = frame.bytes.size();
                    value.buffer = std::make_unique<mantis::sdk::Buffer>(d.host, frame.bytes.size());
                    std::memcpy(value.buffer->writable().data(), frame.bytes.data(), frame.bytes.size());
                    value.buffer->publish();
                    auto info = frame.controls;
                    info.insert({{"role", i ? "right" : "left"}, {"identity", identity(d.cameras[i])},
                        {"video_node", d.cameras[i].video}, {"fourcc", frame.fourcc},
                        {"v4l2_flags", std::to_string(frame.flags)}, {"buffer_size", std::to_string(frame.buffer_size)}});
                    info["requested_target_fps"] = std::to_string(d.config.profile.mode.fps);
                    const std::string prefix = i ? "right_" : "left_";
                    for (const auto &[key, diagnostic] : d.metrics) if (key.starts_with(prefix) &&
                        (key.find("vblank") != std::string::npos || key.find("driver_interval") != std::string::npos || key.find("initial_") != std::string::npos))
                        info[key.substr(prefix.size())] = diagnostic;
                    info["org.mantis.image.width"] = std::to_string(frame.width);
                    info["org.mantis.image.height"] = std::to_string(frame.height);
                    info["org.mantis.image.row_stride_bytes"] = std::to_string(frame.stride);
                    if (frame.fourcc == "Y10P") {
                        info["org.mantis.image.layout"] = "mipi-raw10-v1";
                        info["org.mantis.image.bits_per_sample"] = "10";
                    }
                    value.metadata = Json(info).dump();
                    // Keep only extent, never dereference the expired driver view.
                    value.frame.bytes = {};
                    d.pending[i].push(std::move(value)); d.pending_depth();
                });
            } catch (...) { d.increment(role + "capture_errors"); throw; }
            if (!received) {
                if (x1::monotonic_ns() - d.last_receive[i] > int64_t(d.config.profile.stall_ms) * 1000000) {
                    d.increment(role + "capture_errors"); d.increment("unmatched_frames", d.pending[1 - i].size());
                    throw std::runtime_error(role + "camera stalled");
                }
                return;
            }
        }
        const auto &left = d.pending[0][0].frame, &right = d.pending[1][0].frame;
        if (left.width != right.width || left.height != right.height || left.fourcc != right.fourcc) {
            d.increment("pairing_failures"); throw std::runtime_error("Camera stream modes disagree");
        }
        // choose_pair proved both comparability and a difference within tolerance.
        const auto delta = right.timestamp_ns - left.timestamp_ns;
        d.metrics["native_counter_equality"] = left.sequence == right.sequence ? "equal" : "different";
        d.metrics["left.native_sequence"] = std::to_string(left.sequence);
        d.metrics["right.native_sequence"] = std::to_string(right.sequence);
        d.metrics["native_sequence_offset"] = std::to_string(x1::native_sequence_offset(left.sequence, right.sequence));
        d.metrics["paired_v4l2_delta_ns"] = std::to_string(delta);
        d.metrics["v4l2_delta_ns"] = std::to_string(delta); // compatible alias; always the selected pair
        d.metrics["host_arrival_delta_ns"] = std::to_string(right.received_ns - left.received_ns);
        d.metrics["pairing_state"] = "paired";
        d.aligned = true;
        MantisAttributeV1 a[2]{};
        MantisObservationV1 frames[]{observation(d, 0, a[0]), observation(d, 1, a[1])};
        auto meta = Json(d.metrics).dump();
        auto header = frames[0]; header.packet.type_id = MANTIS_FRAMESET; header.packet.sequence = d.sequence;
        header.packet.attributes = nullptr; header.packet.attribute_count = 0; header.metadata_json = meta.c_str();
        MantisFrameSetV1 set{sizeof(set), 1, header, frames, 2};
        mantis::sdk::check(emit(ctx, &set));
        ++d.sequence; for (auto &queue : d.pending) queue.pop(); d.pending_depth(); emitted = true;
    });
    return status ? status : emitted ? 0 : 2;
}
int stop(void *p) noexcept {
    auto &d = *static_cast<Device *>(p); d.stop();
    try {
        for (const auto &[key, value] : d.metrics) if (key.ends_with("streamoff_error") || key == "setup_rollback_error") {
            if (!d.metrics.contains("error")) d.metrics["error"] = value;
            return 1;
        }
    } catch (...) { return 1; }
    return 0;
}
int diagnostics(void *p, MantisTextEmitV1 emit, void *ctx) noexcept {
    return mantis::sdk::boundary([&] {
        x1::Metadata info;
        if (p) info = static_cast<Device *>(p)->metrics;
        else {
            try { auto c = configuration(); (void)x1::assign(c.profile, c.backend->discover(c.profile)); }
            catch (const std::exception &e) { info["error"] = e.what(); }
        }
        auto json = Json(info).dump(); mantis::sdk::check(emit(ctx, json.c_str()));
    });
}
int initialize(const MantisHostV1 *host) { return mantis::sdk::compatible(host) ? 0 : 1; }
void shutdown() {}
const MantisAcquisitionV1 acquisition{sizeof(acquisition), 1, enumerate, open_device, destroy, start, next, stop, diagnostics};
const void *query(const char *id) { return id && !std::strcmp(id, MANTIS_ACQUISITION_V1) ? &acquisition : nullptr; }
const MantisPluginV1 plugin{sizeof(plugin), 1, "org.mantis.x1", "0.2.0", initialize, shutdown, query};
}
extern "C" MANTIS_EXPORT const MantisPluginV1 *mantis_plugin_entry(uint32_t abi) { return abi == 1 ? &plugin : nullptr; }
