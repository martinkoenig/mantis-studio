#include "virtual-rig/control.hpp"
#include "virtual-rig/model.hpp"
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mantis/sdk.hpp>
#include <mutex>
#include <nlohmann/json.hpp>
#include <set>
#include <thread>
namespace virtual_rig {
namespace {
using Json = nlohmann::json;
using Metadata = std::map<std::string, std::string>;
using Steady = std::chrono::steady_clock;
std::mutex ownership_mutex;
std::set<std::string> owners;
uint64_t generation_counter{}; // protected by ownership_mutex
std::mutex availability_mutex;
std::array<char, 1025> availability_error{};
void copy_error(std::array<char, 1025> &out, const char *message) noexcept {
    if (!message || !*message)
        message = "Acquisition failed without a diagnostic";
    size_t size{};
    while (size < out.size() - 1 && message[size])
        ++size;
    while (size && (static_cast<unsigned char>(message[size]) & 0xc0) == 0x80)
        --size;
    std::copy_n(message, size, out.data());
    out[size] = '\0';
}
template <class F> int available(F &&fn) noexcept {
    try {
        {
            std::lock_guard lock(availability_mutex);
            availability_error[0] = '\0';
        }
        fn();
        return 0;
    } catch (const std::exception &e) {
        std::lock_guard lock(availability_mutex);
        copy_error(availability_error, e.what());
        return 1;
    } catch (...) {
        std::lock_guard lock(availability_mutex);
        copy_error(availability_error, "Unknown acquisition availability error");
        return 1;
    }
}
int64_t monotonic_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Steady::now().time_since_epoch()).count();
}
uint64_t next_generation() {
    std::lock_guard lock(ownership_mutex);
    const auto stamp = monotonic_ns();
    if (stamp < 0 || generation_counter == UINT64_MAX)
        throw std::runtime_error("Generation identifier exhausted");
    // Simulation time may restart at zero; generation identity must not. The
    // host steady epoch survives DSO unload/reload and daemon process restart.
    generation_counter = std::max(uint64_t(stamp), generation_counter + 1);
    return generation_counter;
}
struct Device {
    const MantisHostV1 *host;
    Profile profile;
    std::unique_ptr<Model> model;
    std::mutex mutex;
    std::condition_variable changed;
    std::jthread producer;
    Steady::time_point origin;
    bool owned{}, running{}, virtual_time{}, cancelled{}, waiting{};
    std::array<char, 1025> error{};
    uint64_t waits{}, allocations{}, materialized_bytes{};
    Device(const MantisHostV1 *h, Profile p) : host(h), profile(std::move(p)) {}
    void fail_locked(const char *message) noexcept {
        if (!error[0])
            copy_error(error, message);
        running = false;
        if (model)
            model->retire();
        changed.notify_all();
    }
    void stop() noexcept {
        producer.request_stop();
        {
            std::lock_guard lock(mutex);
            running = false;
            if (model) {
                model->retire();
                model->retire_inflight();
            }
            changed.notify_all();
        }
        if (producer.joinable())
            producer.join();
    }
    ~Device() {
        stop();
        if (owned) {
            std::lock_guard lock(ownership_mutex);
            owners.erase(profile.id);
        }
    }
    int64_t host_now() const {
        return virtual_time ? model->now : monotonic_ns();
    }
    std::string host_clock() const {
        return virtual_time ? profile.id + "/host/" + std::to_string(model ? model->generation : 0)
                            : "std.chrono.steady_clock";
    }
    void produce(std::stop_token token) noexcept {
        std::unique_lock lock(mutex);
        try {
            while (!token.stop_requested() && running) {
                const auto elapsed =
                    std::chrono::duration_cast<std::chrono::nanoseconds>(Steady::now() - origin).count();
                model->advance(elapsed);
                changed.notify_all();
                changed.wait_until(lock, origin + std::chrono::nanoseconds(model->next_exposure()),
                                   [&] { return token.stop_requested() || !running; });
            }
        } catch (const std::exception &e) {
            fail_locked(e.what());
        } catch (...) {
            fail_locked("Unknown exposure producer failure");
        }
    }
    Metadata diagnostics() const {
        Metadata m{
            {"producer_version", "1.0.0"},
            {"sync_quality", "software"},
            {"physical_exposure_skew", "unavailable"},
            {"provenance", "simulation-only"},
            {"source_buffer_policy", "finite-drop-oldest-captured-sample"},
            {"source_capacity_per_camera", std::to_string(profile.capacity)},
            {"pairing_capacity_per_camera", "2"},
            {"pixel_copy_count", "0"},
            {"host_pixel_allocations", std::to_string(allocations)},
            {"materialized_pixel_bytes", std::to_string(materialized_bytes)},
            {"next_waits", std::to_string(waits)},
            {"error", error.data()},
            {"pairing_mode", profile.triggered ? "simulated-trigger-association" : "timestamp-nearest"},
            {"host_receive_clock", host_clock()}};
        if (!model)
            return m;
        m["generation"] = std::to_string(model->generation);
        m["publications"] = std::to_string(model->publications);
        m["model_time_ns"] = std::to_string(model->now);
        Json excluded = Json::array();
        for (const auto &[i, e] : model->exclusions)
            excluded.push_back({{"camera", profile.cameras[i].id},
                                {"native_sequence", e.native_sequence},
                                {"exposure_ns", e.exposure},
                                {"generation", e.generation}});
        m["pairing_exclusions"] = excluded.dump();
        for (size_t i = 0; i < profile.cameras.size(); ++i) {
            const auto prefix = profile.cameras[i].role + ".";
            const auto &s = model->stats[i];
            for (const auto &[key, value] :
                 Metadata{{"exposures", std::to_string(s.exposures)},
                          {"source_drops", std::to_string(s.source_drops)},
                          {"missing_triggers", std::to_string(s.missing_triggers)},
                          {"source_overruns", std::to_string(s.overruns)},
                          {"received", std::to_string(s.received)},
                          {"published", std::to_string(s.published)},
                          {"startup_unmatched", std::to_string(s.startup_unmatched)},
                          {"steady_unmatched", std::to_string(s.steady_unmatched)},
                          {"shutdown_unmatched", std::to_string(s.shutdown_unmatched)},
                          {"source_depth", std::to_string(model->source[i].size())},
                          {"pending_depth", std::to_string(model->pending[i].size())},
                          {"inflight_depth", model->inflight[i] ? "1" : "0"},
                          {"source_high_water", std::to_string(s.high_water)}})
                m[prefix + key] = value;
        }
        return m;
    }
};
Metadata scene(const Profile &p) {
    if (p.scene)
        return {{"calibration_id", p.calibration},
                {"calibration_revision", std::to_string(p.revision)},
                {"org.mantis.virtual-rig.scene", p.scene->snapshot},
                {"org.mantis.virtual-rig.projection", "calibrated-ray-scene-v1"},
                {"org.mantis.virtual-rig.scene_frame", p.id + "/body"},
                {"provenance", "simulation-only"}};
    return {{"calibration_id", p.calibration},
            {"calibration_revision", "1"},
            {"calibration_model", "immutable-analytic-pinhole-v1"},
            {"fx_px", std::to_string(p.width)},
            {"fy_px", std::to_string(p.width)},
            {"cx_px", std::to_string(p.width / 2)},
            {"cy_px", std::to_string(p.height / 2)},
            {"baseline_mm", "40"},
            {"scene_plane_z_mm", std::to_string(p.width * 10)},
            {"feature_world_x_mm", "40"},
            {"feature_world_y_mm", "0"},
            {"expected_disparity_px", "4"},
            {"provenance", "simulation-only"}};
}
int enumerate(MantisDiscoverEmitV1 emit, void *ctx) noexcept {
    return available([&] {
        if (!emit)
            throw std::invalid_argument("Missing discovery callback");
        if (!std::getenv("MANTIS_VIRTUAL_RIG_PROFILE"))
            return;
        const auto p = load_profile();
        auto info = scene(p);
        info["profile"] = p.json;
        info["clock_mapping"] = "explicit-analytic-native-to-simulation; not a physical clock estimate";
        auto json = Json(info).dump();
        const char *caps[]{MANTIS_FRAMESET_STREAM_V1};
        MantisDiscoveredDeviceV1 d{sizeof(d), 1, p.id.c_str(), "", "Virtual acquisition rig",
                                   caps,      1, json.c_str()};
        mantis::sdk::check(emit(ctx, &d));
        for (const auto &c : p.cameras) {
            auto id = p.id + "/" + c.id;
            auto metadata = scene(p);
            metadata.insert({{"role", c.role},
                             {"stream_id", p.id + "/" + c.stream},
                             {"clock_id", c.clock},
                             {"width", std::to_string(p.width)},
                             {"height", std::to_string(p.height)},
                             {"format", p.format},
                             {"stride", std::to_string(p.stride)},
                             {"rate_num", std::to_string(c.rate_num)},
                             {"rate_den", std::to_string(c.rate_den)}});
            auto child_json = Json(metadata).dump();
            const char *child_caps[]{MANTIS_IMAGE_STREAM_V1};
            d = {sizeof(d), 1, id.c_str(), p.id.c_str(), c.role.c_str(), child_caps, 1, child_json.c_str()};
            mantis::sdk::check(emit(ctx, &d));
        }
    });
}
int open(const MantisHostV1 *host, const char *id, void **out) noexcept {
    if (out)
        *out = nullptr;
    return available([&] {
        if (!mantis::sdk::compatible(host) || !id || !out || !host->allocate || !host->release ||
            !host->retain || !host->write_map || !host->read_map || !host->publish)
            throw std::invalid_argument("Invalid acquisition open/host table");
        auto p = load_profile();
        if (p.id != id)
            throw std::invalid_argument("Selected rig parent is unavailable");
        auto d = std::make_unique<Device>(host, std::move(p));
        {
            std::lock_guard lock(ownership_mutex);
            if (!owners.insert(id).second)
                throw std::runtime_error("Rig parent is already owned");
            d->owned = true;
        }
        *out = d.release();
    });
}
int start(void *value) noexcept {
    if (!value)
        return 1;
    auto &d = *static_cast<Device *>(value);
    try {
        // start while running is an error, never a silent generation reset.
        {
            std::lock_guard lock(d.mutex);
            if (d.running)
                return 1;
        }
        d.stop();
        std::lock_guard lock(d.mutex);
        d.error[0] = '\0';
        d.cancelled = false;
        d.waits = d.allocations = d.materialized_bytes = 0;
        d.model = std::make_unique<Model>(d.profile, next_generation());
        d.origin = Steady::now();
        d.model->advance(0);
        d.running = true;
        if (!d.virtual_time)
            d.producer = std::jthread([&d](std::stop_token t) { d.produce(t); });
        return 0;
    } catch (const std::exception &e) {
        std::lock_guard lock(d.mutex);
        d.fail_locked(e.what());
        return 1;
    } catch (...) {
        std::lock_guard lock(d.mutex);
        d.fail_locked("Unknown start failure");
        return 1;
    }
}
// One shared planar texture and point: fx=width, B=40 mm, Z=10*width mm,
// X=40 mm => u_left=cx+4, u_right=cx. All output samples derive from this scene.
uint16_t intensity(const Profile &p, const Event &event, size_t camera, uint32_t x, uint32_t y) {
    if (p.scene)
        return scene_intensity(*p.scene, p.seed, event.index, event.exposure, camera, x, y);
    // Four diagnostic pixels witness the captured exposure identity. They are
    // outside the analytic scene correspondence, and change on later captures.
    if (y == 0 && x < 4)
        return uint16_t(((event.index >> (x * 8)) & 255) * 4);
    auto scene_x = x + uint32_t(camera) * 4;
    if (scene_x == p.width / 2 + 4 && y == p.height / 2)
        return 1023;
    return uint16_t((uint64_t(scene_x) * 13 + uint64_t(y) * 7 + p.seed % 251) % 251);
}
void pixels(const Profile &p, const Event &event, size_t camera, mantis::sdk::Buffer &buffer) {
    auto bytes = buffer.writable();
    if (!bytes.data() || bytes.size() < p.bytes)
        throw std::runtime_error("Host allocation mapping is smaller than the image");
    std::fill_n(bytes.begin(), size_t(p.bytes), std::byte{});
    const bool packed = p.format == "Y10P";
    for (uint32_t y = 0; y < p.height; ++y) {
        auto row = bytes.data() + size_t(y) * p.stride;
        for (uint32_t x = 0; x < p.width; ++x) {
            const auto v = intensity(p, event, camera, x, y);
            if (!packed)
                row[x] = std::byte(v >> 2);
            else {
                const auto offset = x / 4 * 5;
                row[offset + x % 4] = std::byte(v >> 2);
                row[offset + 4] |= std::byte((v & 3) << (2 * (x % 4)));
            }
        }
    }
    buffer.publish();
}
int next(void *value, uint32_t timeout, MantisFrameSetEmitV1 emit, void *ctx) noexcept {
    if (!value)
        return 1;
    auto &d = *static_cast<Device *>(value);
    std::unique_lock lock(d.mutex);
    uint64_t allocated{}, materialized{};
    try {
        if (!d.running || !emit || timeout > 1000)
            throw std::invalid_argument("Invalid next call or stopped stream");
        const auto deadline = Steady::now() + std::chrono::milliseconds(timeout);
        auto &m = *d.model;
        unsigned reads{};
        bool paired{};
        // Two input reads per call, ADR-026 exclusion budgets survive calls.
        for (unsigned step = 0; step < 8; ++step) {
            if (d.cancelled)
                throw std::runtime_error("Acquisition wait cancelled");
            if (!d.running)
                throw std::runtime_error(d.error[0] ? d.error.data() : "Source stopped");
            x1::PairDecision decision{x1::PairAction::left};
            if (d.profile.cameras.size() == 1) {
                if (!m.pending[0].empty())
                    decision.action = x1::PairAction::pair;
            } else {
                std::array<std::array<x1::PairingObservation, 2>, 2> views{};
                for (size_t i = 0; i < 2; ++i)
                    for (size_t j = 0; j < m.pending[i].size(); ++j) {
                        const auto &e = m.pending[i][j];
                        views[i][j] = {e.native_sequence, e.exposure,
                                       e.timestamp_valid && e.comparable ? "org.mantis.simulation.reference"
                                                                         : ""};
                    }
                if (d.profile.triggered && !m.pending[0].empty() && !m.pending[1].empty() &&
                    m.pending[0][0].trigger != m.pending[1][0].trigger)
                    throw std::runtime_error("Missing shared trigger association");
                decision = x1::choose_pair({views[0].data(), m.pending[0].size()},
                                           {views[1].data(), m.pending[1].size()}, d.profile.triggered,
                                           d.profile.tolerance);
            }
            if (decision.action == x1::PairAction::fail)
                throw std::runtime_error(decision.error);
            if (decision.action == x1::PairAction::pair) {
                paired = true;
                break;
            }
            if (decision.action == x1::PairAction::discard_left ||
                decision.action == x1::PairAction::discard_right) {
                m.exclude(decision.action == x1::PairAction::discard_right ? 1 : 0);
                continue;
            }
            if (reads == 2)
                return 2;
            const size_t i = decision.action == x1::PairAction::right ? 1 : 0;
            if (m.pending[i].size() == x1::pairing_capacity)
                throw std::runtime_error("Pairing queue saturated");
            const auto reference_now =
                d.virtual_time
                    ? m.now
                    : std::chrono::duration_cast<std::chrono::nanoseconds>(Steady::now() - d.origin).count();
            if (m.receive(i, d.host_now(), reference_now)) {
                ++reads;
                continue;
            }
            if (d.virtual_time || Steady::now() >= deadline)
                return 2;
            // Availability and capture time remain separate. A delivery delay
            // waits for its own deadline, without forcing the producer to run.
            auto wake = deadline;
            for (const auto &e : m.source[i])
                wake = std::min(wake, d.origin + std::chrono::nanoseconds(e.available));
            ++d.waits;
            d.waiting = true;
            d.changed.notify_all();
            d.changed.wait_until(lock, wake);
            d.waiting = false;
            d.changed.notify_all();
        }
        if (!paired)
            return 2;
        std::array<Event, 2> selected;
        for (size_t i = 0; i < d.profile.cameras.size(); ++i) {
            selected[i] = m.pending[i][0];
            m.inflight[i] = selected[i];
            m.pending[i].pop();
        }
        auto set_info = d.diagnostics();
        set_info["modeled_exposure_delta_ns"] =
            d.profile.cameras.size() == 2 ? std::to_string(selected[1].exposure - selected[0].exposure)
                                          : "unavailable";
        const auto processing = d.host_now();
        const auto generation = m.generation, publication = m.publications;
        const auto reference_clock = d.profile.id + "/reference/" + std::to_string(generation);
        const auto reference_origin =
            d.virtual_time
                ? 0
                : std::chrono::duration_cast<std::chrono::nanoseconds>(d.origin.time_since_epoch()).count();
        // The producer can keep capturing into its finite source queues while
        // SDK allocation, rendering or a consumer callback is delayed. Selected
        // samples have separate ownership and remain accounted on every exit.
        lock.unlock();
        std::array<std::optional<mantis::sdk::Buffer>, 2> buffers;
        std::array<MantisAttributeV1, 2> attributes{};
        std::array<MantisObservationV1, 2> frames{};
        std::array<std::string, 2> metadata, coordinates, native_clocks;
        auto group =
            d.profile.triggered ? d.profile.id + "/simulated-trigger/" + std::to_string(generation) : "";
        for (size_t i = 0; i < d.profile.cameras.size(); ++i) {
            const auto &c = d.profile.cameras[i];
            const auto &e = selected[i];
            buffers[i].emplace(d.host, d.profile.bytes);
            ++allocated;
            pixels(d.profile, e, i, *buffers[i]);
            materialized += d.profile.bytes;
            auto &a = attributes[i];
            a = {sizeof(a),
                 1,
                 "org.mantis.pixels",
                 "intensity",
                 1,
                 2,
                 {d.profile.height, d.profile.width},
                 {d.profile.stride, 1},
                 buffers[i]->get(),
                 0,
                 d.profile.bytes};
            if (d.profile.format == "Y10P") {
                a.name = "org.mantis.image.packed_bytes";
                a.unit = "byte";
                a.rank = 1;
                a.shape[0] = d.profile.bytes;
                a.shape[1] = 0;
                a.stride[0] = 1;
                a.stride[1] = 0;
            }
            Metadata info{
                {"component_id", d.profile.id + "/" + c.id},
                {"stream_id", d.profile.id + "/" + c.stream},
                {"role", c.role},
                {"fourcc", d.profile.format},
                {"native_clock_id", c.clock},
                {"generation", std::to_string(e.generation)},
                {"capture_event_ns", std::to_string(e.exposure)},
                {"availability_ns", std::to_string(e.available)},
                {"capture_event_clock", reference_clock},
                {"availability_clock", reference_clock},
                {"reference_origin_host_ns", std::to_string(reference_origin)},
                {"processing_ns", std::to_string(processing)},
                {"host_receive_clock", d.host_clock()},
                {"timestamp_valid", e.timestamp_valid ? "true" : "false"},
                {"native_offset_ns", std::to_string(c.offset)},
                {"native_drift_ppm", std::to_string(c.drift_ppm)},
                {"clock_mapping", e.comparable ? "analytic-simulation-reference-v1" : "unavailable"},
                {"physical_trigger_observation", "unavailable"},
                {"org.mantis.image.width", std::to_string(d.profile.width)},
                {"org.mantis.image.height", std::to_string(d.profile.height)},
                {"org.mantis.image.row_stride_bytes", std::to_string(d.profile.stride)}};
            if (d.profile.scene) {
                const auto &o = d.profile.scene->cameras[i];
                info.insert({{"org.mantis.virtual-rig.scene", d.profile.scene->snapshot},
                             {"org.mantis.virtual-rig.projection", "calibrated-ray-scene-v1"},
                             {"org.mantis.virtual-rig.camera_index", std::to_string(i)},
                             {"org.mantis.virtual-rig.scene_frame", d.profile.id + "/body"},
                             {"org.mantis.virtual-rig.intensity", "synthetic-linear-dn-midpoint-v1"},
                             {"org.mantis.virtual-rig.row_time_ns", std::to_string(o.row_time_ns)},
                             {"org.mantis.virtual-rig.integration_ns", std::to_string(o.integration_ns)},
                             {"org.mantis.virtual-rig.seed", std::to_string(d.profile.seed)}});
            }
            if (d.profile.triggered)
                info.insert({{"simulated_trigger_id", std::to_string(e.trigger)},
                             {"simulated_trigger_generation", std::to_string(e.trigger_generation)},
                             {"requested_trigger_ns", std::to_string(e.requested)},
                             {"trigger_evidence", "modeled-event; no physical observation"}});
            if (d.profile.format == "Y10P")
                info.insert({{"org.mantis.image.layout", "mipi-raw10-v1"},
                             {"org.mantis.image.bits_per_sample", "10"}});
            metadata[i] = Json(info).dump();
            coordinates[i] = d.profile.id + "/" + c.id + "/optical";
            // Native timestamps restart with the acquisition model. The configured
            // clock is provenance; only this epoch-scoped identity is continuous.
            native_clocks[i] = c.clock + "/epoch/" + std::to_string(e.generation);
            MantisPacketV1 packet{sizeof(packet),
                                  1,
                                  MANTIS_IMAGE,
                                  1,
                                  e.native_sequence,
                                  e.timestamp_valid ? e.native : 0,
                                  e.timestamp_valid ? native_clocks[i].c_str() : "",
                                  d.profile.calibration.c_str(),
                                  d.profile.revision,
                                  coordinates[i].c_str(),
                                  &a,
                                  1};
            frames[i] = {sizeof(MantisObservationV1),
                         1,
                         packet,
                         e.received,
                         group.c_str(),
                         d.profile.triggered ? e.trigger : 0,
                         e.timestamp_valid && e.comparable ? 1u : 0u,
                         metadata[i].c_str()};
        }
        auto json = Json(set_info).dump();
        auto header = frames[0];
        header.packet.type_id = MANTIS_FRAMESET;
        header.packet.sequence = publication;
        const auto body = d.profile.id + "/body";
        header.packet.coordinate_frame = body.c_str();
        header.packet.attributes = nullptr;
        header.packet.attribute_count = 0;
        header.metadata_json = json.c_str();
        MantisFrameSetV1 set{sizeof(set), 1, header, frames.data(), uint32_t(d.profile.cameras.size())};
        if (emit(ctx, &set))
            throw std::runtime_error("FrameSet emit failed");
        lock.lock();
        d.allocations += allocated;
        d.materialized_bytes += materialized;
        allocated = materialized = 0;
        for (size_t i = 0; i < d.profile.cameras.size(); ++i) {
            m.inflight[i].reset();
            ++m.stats[i].published;
        }
        ++m.publications;
        m.exclusions.clear();
        m.exclusions_since_pair = 0;
        return 0;
    } catch (const std::exception &e) {
        if (!lock.owns_lock())
            lock.lock();
        d.allocations += allocated;
        d.materialized_bytes += materialized;
        if (d.model)
            d.model->retire_inflight();
        d.fail_locked(e.what());
        return 1;
    } catch (...) {
        if (!lock.owns_lock())
            lock.lock();
        d.allocations += allocated;
        d.materialized_bytes += materialized;
        if (d.model)
            d.model->retire_inflight();
        d.fail_locked("Unknown acquisition callback failure");
        return 1;
    }
}
int stop(void *value) noexcept {
    if (!value)
        return 1;
    static_cast<Device *>(value)->stop();
    return 0;
}
void destroy(void *value) noexcept {
    delete static_cast<Device *>(value);
}
int diagnostics(void *value, MantisTextEmitV1 emit, void *ctx) noexcept {
    return mantis::sdk::boundary([&] {
        if (!emit)
            throw std::invalid_argument("Missing diagnostic callback");
        Metadata m;
        if (value) {
            auto &d = *static_cast<Device *>(value);
            std::lock_guard lock(d.mutex);
            m = d.diagnostics();
        } else {
            try {
                (void)load_profile();
                std::lock_guard lock(availability_mutex);
                if (availability_error[0])
                    m["error"] = availability_error.data();
            } catch (const std::exception &e) {
                m["error"] = e.what();
            }
        }
        auto json = Json(m).dump();
        mantis::sdk::check(emit(ctx, json.c_str()));
    });
}
int virtual_time(void *value) noexcept {
    if (!value)
        return 1;
    auto &d = *static_cast<Device *>(value);
    std::lock_guard lock(d.mutex);
    if (d.running)
        return 1;
    d.virtual_time = true;
    return 0;
}
int advance(void *value, int64_t target) noexcept {
    if (!value)
        return 1;
    auto &d = *static_cast<Device *>(value);
    std::lock_guard lock(d.mutex);
    if (!d.virtual_time || !d.running)
        return 1;
    try {
        d.model->advance(target);
        return 0;
    } catch (const std::exception &e) {
        d.fail_locked(e.what());
        return 1;
    } catch (...) {
        d.fail_locked("Unknown virtual clock failure");
        return 1;
    }
}
int wait_for_waiter(void *value, uint32_t timeout) noexcept {
    if (!value || timeout > 1000)
        return 1;
    auto &d = *static_cast<Device *>(value);
    std::unique_lock lock(d.mutex);
    return d.changed.wait_for(lock, std::chrono::milliseconds(timeout), [&] { return d.waiting; }) ? 0 : 1;
}
int cancel_wait(void *value) noexcept {
    if (!value)
        return 1;
    auto &d = *static_cast<Device *>(value);
    std::lock_guard lock(d.mutex);
    d.cancelled = true;
    d.changed.notify_all();
    return 0;
}
int initialize(const MantisHostV1 *h) noexcept {
    return mantis::sdk::compatible(h) ? 0 : 1;
}
void shutdown() noexcept {}
const MantisAcquisitionV1 acquisition{sizeof(acquisition), 1, enumerate, open, destroy, start, next, stop,
                                      diagnostics};
const ClockV1 clock_control{sizeof(clock_control), 1, virtual_time, advance, wait_for_waiter, cancel_wait};
const void *query(const char *id) noexcept {
    if (!id)
        return nullptr;
    if (std::strcmp(id, MANTIS_ACQUISITION_V1) == 0)
        return &acquisition;
    return std::strcmp(id, clock_interface) == 0 ? &clock_control : nullptr;
}
const MantisPluginV1 plugin{sizeof(plugin), 1,    "org.mantis.virtual-rig", "1.0.0", initialize,
                            shutdown,       query};
} // namespace
} // namespace virtual_rig
extern "C" MANTIS_EXPORT const MantisPluginV1 *mantis_plugin_entry(uint32_t version) {
    return version == 1 ? &virtual_rig::plugin : nullptr;
}
