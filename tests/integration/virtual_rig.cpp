#include "../../plugins/first-party/devices/virtual-rig/control.hpp"
#include "portable_environment.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <future>
#include <iostream>
#include <mantis/data_io.hpp>
#include <mantis/device_runtime.hpp>
#include <mantis/image_layout.hpp>
#include <mantis/plugin_runtime.hpp>
#include <mantis/replay.hpp>
#include <nlohmann/json.hpp>
#include <sstream>
#include <unordered_map>
using namespace mantis;
using Json = nlohmann::json;
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(std::string("Check failed: ") + #x + " at " +                           \
                                     std::to_string(__LINE__));                                              \
    } while (false)
namespace {
struct Workspace {
    std::filesystem::path root = std::filesystem::temp_directory_path() / Id::random().value;
    Workspace() {
        std::filesystem::create_directory(root);
    }
    ~Workspace() {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }
    void profile(const Json &j) const {
        std::ofstream out(root / "rig.json");
        out << j.dump();
        out.close();
        CHECK(out.good());
        CHECK(test::environment("MANTIS_VIRTUAL_RIG_PROFILE", (root / "rig.json").string().c_str()) == 0);
    }
};
Json profile(size_t cameras = 2, std::string format = "GREY", uint32_t width = 64, uint32_t height = 48) {
    Json j{{"version", 1},
           {"id", "contract"},
           {"mode", "trigger"},
           {"seed", 17},
           {"width", width},
           {"height", height},
           {"format", format},
           {"stride", format == "GREY" ? width : width / 4 * 5},
           {"capacity", 16},
           {"tolerance_ns", 20000000},
           {"calibration_id",
            "org.mantis.virtual-rig.pinhole-" + std::to_string(width) + "x" + std::to_string(height) + ".v1"},
           {"calibration_revision", 1},
           {"cameras", Json::array()},
           {"faults", Json::array()}};
    for (size_t i = 0; i < cameras; ++i) {
        const std::string role = i ? "right" : "left";
        j["cameras"].push_back({{"id", role + "-camera"},
                                {"role", role},
                                {"stream", role + "-image"},
                                {"clock", "org.test." + role},
                                {"rate_num", 30},
                                {"rate_den", 1},
                                {"phase_ns", 0},
                                {"latency_ns", 0},
                                {"offset_ns", 0},
                                {"drift_ppm", 0},
                                {"jitter_ns", 0},
                                {"delivery_ns", 0},
                                {"comparable", true}});
    }
    return j;
}
void fault(Json &p, size_t camera, uint64_t at, const char *kind, int64_t delay = 0) {
    p["faults"].push_back({{"camera", camera}, {"at", at}, {"kind", kind}, {"delay_ns", delay}});
}
// Exercise host ownership using the real runtime allocator; count every ABI ref.
const MantisHostV1 *base_host = plugins::host_api();
struct Allocation {
    unsigned refs;
    uint64_t bytes;
};
std::unordered_map<MantisBuffer *, Allocation> references;
uint64_t active_bytes{}, peak_bytes{};
size_t peak_buffers{};
bool fail_allocate{}, fail_map{}, fail_publish{}, short_map{};
MantisBuffer *allocate(uint64_t bytes, uint64_t alignment) {
    if (fail_allocate)
        return nullptr;
    auto *b = base_host->allocate(bytes, alignment);
    if (b) {
        try {
            references.emplace(b, Allocation{1, bytes});
        } catch (...) {
            base_host->release(b);
            throw;
        }
        active_bytes += bytes;
        peak_bytes = std::max(peak_bytes, active_bytes);
        peak_buffers = std::max(peak_buffers, references.size());
    }
    return b;
}
void retain(MantisBuffer *b) {
    base_host->retain(b);
    ++references.at(b).refs;
}
void release(MantisBuffer *b) {
    auto it = references.find(b);
    if (it != references.end() && !--it->second.refs) {
        active_bytes -= it->second.bytes;
        references.erase(it);
    }
    base_host->release(b);
}
int write_map(MantisBuffer *b, void **data, uint64_t *bytes) {
    if (fail_map)
        return 1;
    auto result = base_host->write_map(b, data, bytes);
    if (!result && short_map)
        *bytes = 1;
    return result;
}
int publish(MantisBuffer *b) {
    return fail_publish ? 1 : base_host->publish(b);
}
MantisHostV1 host{sizeof(host),        1,       allocate,      retain, release, write_map,
                  base_host->read_map, publish, base_host->log};
struct Frame {
    uint64_t sequence{}, trigger{}, revision{};
    int64_t native{}, received{};
    uint32_t quality{};
    std::string clock, calibration, coordinate, group, attribute;
    Json meta;
    std::array<uint64_t, 4> shape{}, stride{};
    uint32_t rank{};
    uint64_t size{};
    sdk::RetainedBuffer buffer;
    explicit Frame(const MantisObservationV1 &o)
        : sequence(o.packet.sequence), trigger(o.sync_trigger), revision(o.packet.calibration_revision),
          native(o.packet.device_time_ns), received(o.host_receive_ns), quality(o.sync_quality),
          clock(o.packet.clock_id), calibration(o.packet.calibration_id),
          coordinate(o.packet.coordinate_frame), group(o.sync_group), attribute(o.packet.attributes[0].name),
          meta(Json::parse(o.metadata_json)), rank(o.packet.attributes[0].rank),
          size(o.packet.attributes[0].bytes), buffer(&host, o.packet.attributes[0].buffer) {
        CHECK(sdk::compatible_table(&o) && sdk::compatible_table(&o.packet) &&
              sdk::compatible_table(o.packet.attributes));
        CHECK(o.packet.attribute_count == 1 && o.packet.attributes[0].offset == 0);
        if (meta.contains("simulated_trigger_id")) {
            CHECK(trigger == sequence);
            CHECK(meta.at("simulated_trigger_id") == std::to_string(sequence));
            CHECK(meta.at("simulated_trigger_generation") == meta.at("generation"));
            CHECK(group == "org.mantis.virtual-rig:contract/simulated-trigger/" +
                               meta.at("generation").get<std::string>());
        } else {
            CHECK(group.empty() && trigger == 0);
        }
        std::copy_n(o.packet.attributes[0].shape, 4, shape.begin());
        std::copy_n(o.packet.attributes[0].stride, 4, stride.begin());
    }
    std::vector<std::byte> bytes() const {
        const void *data{};
        uint64_t extent{};
        CHECK(host.read_map(buffer.get(), &data, &extent) == 0 && extent >= size);
        const auto *first = static_cast<const std::byte *>(data);
        return {first, first + size};
    }
};
struct Snapshot {
    uint64_t sequence{};
    uint32_t quality{};
    Json meta;
    std::vector<Frame> frames;
};
struct Receiver {
    std::unique_ptr<Snapshot> result;
    std::exception_ptr error;
    unsigned calls{};
    bool fail{}, throws{};
    std::string exception_message;
    static int emit(void *ctx, const MantisFrameSetV1 *set) {
        auto &r = *static_cast<Receiver *>(ctx);
        if (r.throws)
            throw std::runtime_error(r.exception_message.empty() ? "Injected callback exception"
                                                                 : r.exception_message.c_str());
        try {
            CHECK(++r.calls == 1);
            CHECK(sdk::compatible_table(set) && set->frame_count >= 1 && set->frame_count <= 2);
            CHECK(std::string(set->observation.packet.type_id) == MANTIS_FRAMESET);
            CHECK(set->observation.packet.attribute_count == 0);
            r.result = std::make_unique<Snapshot>();
            r.result->sequence = set->observation.packet.sequence;
            r.result->quality = set->observation.sync_quality;
            r.result->meta = Json::parse(set->observation.metadata_json);
            for (uint32_t i = 0; i < set->frame_count; ++i)
                r.result->frames.emplace_back(set->frames[i]);
            return r.fail ? 1 : 0;
        } catch (...) {
            r.error = std::current_exception();
            return 1;
        }
    }
};
struct Instance {
    const MantisAcquisitionV1 *api{};
    void *value{};
    ~Instance() {
        if (value) {
            api->stop(value);
            api->destroy(value);
        }
    }
};
struct Rig {
    Instance instance;
    const virtual_rig::ClockV1 *clock;
    Rig(plugins::Loaded &loaded, Workspace &w, const Json &p, bool manual = true)
        : instance{loaded.query<MantisAcquisitionV1>(MANTIS_ACQUISITION_V1), nullptr},
          clock(loaded.query<virtual_rig::ClockV1>(virtual_rig::clock_interface)) {
        w.profile(p);
        CHECK(instance.api->open(&host, "org.mantis.virtual-rig:contract", &instance.value) == 0);
        CHECK(instance.value);
        if (manual)
            CHECK(clock->use_virtual_time(instance.value) == 0);
        CHECK(instance.api->start(instance.value) == 0);
    }
    void advance(int64_t t) {
        CHECK(clock->advance(instance.value, t) == 0);
    }
    int next(Receiver &receiver, uint32_t timeout = 0) {
        auto rc = instance.api->next(instance.value, timeout, Receiver::emit, &receiver);
        if (receiver.error)
            std::rethrow_exception(receiver.error);
        return rc;
    }
    std::unique_ptr<Snapshot> take(unsigned call_budget = 4) {
        for (unsigned attempt = 0; attempt < call_budget; ++attempt) {
            Receiver r;
            auto rc = next(r);
            CHECK(rc == 0 || rc == 2);
            if (rc == 0) {
                CHECK(r.calls == 1 && r.result);
                return std::move(r.result);
            }
            CHECK(!r.result && !r.calls);
        }
        return {};
    }
    Json diagnostics() {
        std::string text;
        auto emit = [](void *ctx, const char *s) noexcept {
            return sdk::boundary([&] { *static_cast<std::string *>(ctx) = s; });
        };
        CHECK(instance.api->diagnostics(instance.value, emit, &text) == 0);
        return Json::parse(text);
    }
    void stop() {
        CHECK(instance.api->stop(instance.value) == 0);
    }
};
uint64_t number(const Json &j, const std::string &key) {
    return std::stoull(j.at(key).get<std::string>());
}
void accounting(const Json &m, size_t cameras) {
    for (size_t i = 0; i < cameras; ++i) {
        const std::string r = i ? "right." : "left.";
        CHECK(number(m, r + "exposures") ==
              number(m, r + "source_drops") + number(m, r + "source_overruns") + number(m, r + "published") +
                  number(m, r + "startup_unmatched") + number(m, r + "steady_unmatched") +
                  number(m, r + "shutdown_unmatched") + number(m, r + "source_depth") +
                  number(m, r + "pending_depth") + number(m, r + "inflight_depth"));
    }
}
void image_oracle(const Snapshot &s, const Json &p) {
    const auto width = p.at("width").get<uint32_t>(), height = p.at("height").get<uint32_t>();
    const auto row_stride = p.at("stride").get<uint32_t>();
    const bool packed = p.at("format") == "Y10P";
    const auto seed = p.at("seed").get<uint64_t>();
    CHECK(s.quality == 1 && s.frames.size() == p.at("cameras").size());
    std::array<uint32_t, 2> feature{};
    for (size_t camera = 0; camera < s.frames.size(); ++camera) {
        const auto &f = s.frames[camera];
        CHECK(f.quality == 1);
        CHECK(f.meta.at("component_id") == (camera ? "org.mantis.virtual-rig:contract/right-camera"
                                                   : "org.mantis.virtual-rig:contract/left-camera"));
        CHECK(f.meta.at("stream_id") == (camera ? "org.mantis.virtual-rig:contract/right-image"
                                                : "org.mantis.virtual-rig:contract/left-image"));
        CHECK(f.calibration == p.at("calibration_id").get<std::string>() && f.revision == 1);
        CHECK(f.coordinate == (camera ? "org.mantis.virtual-rig:contract/right-camera/optical"
                                      : "org.mantis.virtual-rig:contract/left-camera/optical"));
        CHECK(f.meta.at("physical_trigger_observation") == "unavailable");
        CHECK(f.meta.contains("simulated_trigger_id") == (p.at("mode") == "trigger"));
        CHECK(f.rank == (packed ? 1u : 2u));
        CHECK(f.shape[0] == (packed ? uint64_t(row_stride) * height : height));
        CHECK(f.stride[0] == (packed ? 1u : row_stride));
        CHECK(f.shape[1] == (packed ? 0u : width) && f.stride[1] == (packed ? 0u : 1u));
        CHECK(f.meta.at("fourcc") == p.at("format"));
        auto bytes = f.bytes();
        CHECK(bytes.size() == size_t(row_stride) * height);
        std::vector<std::byte> expected(bytes.size());
        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                // Independently project (X,Y,Z)=(40,0,10*width) mm through
                // fx=fy=width; right camera is translated +40mm along X.
                const auto u = width / 2 + 4 - uint32_t(camera) * 4;
                const bool target = x == u && y == height / 2;
                const auto value = y == 0 && x < 4 ? uint32_t((f.sequence >> (x * 8)) & 255) * 4
                                   : target
                                       ? 1023u
                                       : uint32_t((13ull * (x + camera * 4) + 7ull * y + seed % 251) % 251);
                auto offset = size_t(y) * row_stride;
                if (packed) {
                    offset += size_t(x / 4) * 5;
                    expected[offset + x % 4] = std::byte(value / 4);
                    expected[offset + 4] |= std::byte((value % 4) * (1u << (2 * (x % 4))));
                } else
                    expected[offset + x] = std::byte(value / 4);
                if (y == height / 2) {
                    auto observed =
                        packed ? (std::to_integer<unsigned>(bytes[offset + x % 4]) * 4 +
                                  ((std::to_integer<unsigned>(bytes[offset + 4]) >> (2 * (x % 4))) & 3))
                               : std::to_integer<unsigned>(bytes[offset + x]) * 4;
                    if (observed >= 1020)
                        feature[camera] = x;
                }
            }
        }
        CHECK(bytes == expected);
        CHECK(feature[camera] == width / 2 + 4 - camera * 4);
    }
    if (s.frames.size() == 2)
        CHECK(feature[0] - feature[1] == 4);
}
void contract(const std::filesystem::path &library) {
    Workspace w;
    auto loaded = std::make_unique<plugins::Loaded>(library);
    auto *api = loaded->query<MantisAcquisitionV1>(MANTIS_ACQUISITION_V1);
    CHECK(api->struct_size == sizeof(MantisAcquisitionV1) && api->abi_version == 1);
    CHECK(!loaded->api()->query_interface(MANTIS_DEVICE_V1));
    CHECK(!loaded->api()->query_interface(MANTIS_HARDWARE_TRIGGER_V1));
    CHECK(test::environment("MANTIS_VIRTUAL_RIG_PROFILE", nullptr) == 0);
    unsigned count{};
    sdk::enumerate(api, [&](const auto &) { ++count; });
    CHECK(count == 0);
    auto p = profile();
    w.profile(p);
    std::vector<std::string> ids;
    sdk::enumerate(api, [&](const MantisDiscoveredDeviceV1 &d) {
        ids.emplace_back(d.id);
        CHECK(d.capability_count == 1);
        CHECK(std::string(d.capabilities[0]) ==
              (ids.size() == 1 ? MANTIS_FRAMESET_STREAM_V1 : MANTIS_IMAGE_STREAM_V1));
        CHECK(std::string(d.parent_id) == (ids.size() == 1 ? "" : "org.mantis.virtual-rig:contract"));
        const auto meta = Json::parse(d.metadata_json);
        CHECK(meta.at("expected_disparity_px") == "4" && meta.at("baseline_mm") == "40");
    });
    CHECK(ids == std::vector<std::string>({"org.mantis.virtual-rig:contract",
                                           "org.mantis.virtual-rig:contract/left-camera",
                                           "org.mantis.virtual-rig:contract/right-camera"}));
    void *invalid = reinterpret_cast<void *>(uintptr_t{1});
    CHECK(api->open(&host, ids[1].c_str(), &invalid) != 0 && !invalid);
    auto availability_diagnostic = [&] {
        std::string text;
        auto emit = [](void *ctx, const char *s) noexcept {
            return sdk::boundary([&] { *static_cast<std::string *>(ctx) = s; });
        };
        CHECK(api->diagnostics(nullptr, emit, &text) == 0);
        return Json::parse(text).at("error").get<std::string>();
    };
    CHECK(availability_diagnostic() == "Selected rig parent is unavailable");
    auto invalid_host = host;
    invalid_host.struct_size = 8;
    CHECK(api->open(&invalid_host, ids[0].c_str(), &invalid) != 0 && !invalid);
    invalid_host = host;
    invalid_host.abi_version = 2;
    CHECK(api->open(&invalid_host, ids[0].c_str(), &invalid) != 0 && !invalid);
    invalid_host = host;
    invalid_host.allocate = nullptr;
    CHECK(api->open(&invalid_host, ids[0].c_str(), &invalid) != 0 && !invalid);
    CHECK(availability_diagnostic() == "Invalid acquisition open/host table");
    for (const auto &format : {"GREY", "Y10P"}) {
        for (size_t cameras : {1u, 2u}) {
            p = profile(cameras, format);
            p["stride"] = p["stride"].get<unsigned>() + 8;
            Rig r(*loaded, w, p);
            CHECK(api->start(r.instance.value) != 0);
            CHECK(api->open(&host, ids[0].c_str(), &invalid) != 0 && !invalid);
            auto first = r.take();
            CHECK(first && first->sequence == 0);
            image_oracle(*first, p);
            auto original = first->frames[0].bytes();
            r.advance(100000000);
            auto later = r.take();
            CHECK(later && later->sequence == 1);
            CHECK(later->frames[0].bytes() != original);
            CHECK(first->frames[0].bytes() == original);
            const auto gen = first->frames[0].meta.at("generation");
            std::vector<std::string> clocks;
            for (size_t i = 0; i < cameras; ++i) {
                clocks.push_back(first->frames[i].clock);
                CHECK(!clocks[i].empty() && later->frames[i].clock == clocks[i]);
                CHECK(first->frames[i].native == 0 && later->frames[i].native == 33333333);
                CHECK(first->frames[i].meta.at("native_clock_id") == p.at("cameras")[i].at("clock"));
                CHECK(clocks[i] != first->frames[i].meta.at("capture_event_clock").get<std::string>());
                CHECK(clocks[i] != first->frames[i].meta.at("host_receive_clock").get<std::string>());
            }
            r.stop();
            accounting(r.diagnostics(), cameras);
            CHECK(api->start(r.instance.value) == 0);
            auto restarted = r.take();
            CHECK(restarted && restarted->sequence == 0 && restarted->frames[0].sequence == 0);
            for (size_t i = 0; i < cameras; ++i) {
                CHECK(restarted->frames[i].clock != clocks[i]);
                CHECK(restarted->frames[i].native == 0);
                CHECK(restarted->frames[i].meta.at("native_clock_id") == p.at("cameras")[i].at("clock"));
                CHECK(restarted->frames[i].meta.at("component_id") ==
                      first->frames[i].meta.at("component_id"));
                CHECK(restarted->frames[i].meta.at("stream_id") == first->frames[i].meta.at("stream_id"));
                CHECK(restarted->frames[i].meta.at("host_receive_clock") !=
                      first->frames[i].meta.at("host_receive_clock"));
                CHECK(first->frames[i].clock == clocks[i] && later->frames[i].clock == clocks[i]);
                CHECK(first->frames[i].native == 0 && later->frames[i].native == 33333333);
            }
            CHECK(first->frames[0].bytes() == original);
            CHECK(restarted->frames[0].meta.at("generation") != gen);
            CHECK(restarted->frames[0].trigger == 0);
        }
    }
    // Exact real-size payload, not a sustained-rate benchmark.
    for (const auto *format : {"GREY", "Y10P"}) {
        p = profile(2, format, 1280, 720);
        Rig r(*loaded, w, p);
        auto s = r.take();
        CHECK(s && s->frames[0].size == (std::string_view(format) == "Y10P" ? 1152000u : 921600u));
        image_oracle(*s, p);
    }
    CHECK(references.empty());
    // Retained success and failed-emit images must outlive the DSO and instance.
    std::unique_ptr<Snapshot> retained, failed_emit;
    p = profile();
    {
        Rig r(*loaded, w, p);
        retained = r.take();
        r.advance(33333333);
        Receiver receiver;
        receiver.fail = true;
        CHECK(r.next(receiver) == 1 && receiver.calls == 1);
        failed_emit = std::move(receiver.result);
        CHECK(r.diagnostics().at("error") == "FrameSet emit failed");
        r.stop();
        CHECK(r.diagnostics().at("error") == "FrameSet emit failed");
        accounting(r.diagnostics(), 2);
    }
    loaded.reset();
    image_oracle(*retained, p);
    image_oracle(*failed_emit, p);
    const auto old_generation = retained->frames[0].meta.at("generation");
    loaded = std::make_unique<plugins::Loaded>(library);
    {
        Rig reopened(*loaded, w, p);
        auto s = reopened.take();
        CHECK(s && s->frames[0].meta.at("generation") != old_generation);
        CHECK(s->frames[0].clock != retained->frames[0].clock);
        CHECK(s->frames[0].native == 0 && retained->frames[0].native == 0);
    }
    retained.reset();
    failed_emit.reset();
    CHECK(references.empty());
    loaded = std::make_unique<plugins::Loaded>(library);
    api = loaded->query<MantisAcquisitionV1>(MANTIS_ACQUISITION_V1);
    // Allocation, mapping, publication and throwing callback failure unwind.
    for (unsigned failure = 0; failure < 5; ++failure) {
        Rig r(*loaded, w, profile());
        fail_allocate = failure == 0;
        fail_map = failure == 1;
        fail_publish = failure == 2;
        short_map = failure == 3;
        Receiver receiver;
        receiver.throws = failure == 4;
        const auto rc = r.next(receiver);
        fail_allocate = fail_map = fail_publish = short_map = false;
        CHECK(rc == 1 && references.empty());
        if (failure == 4)
            CHECK(r.diagnostics().at("error") == "Injected callback exception");
        r.stop();
        accounting(r.diagnostics(), 2);
        CHECK(api->start(r.instance.value) == 0);
        CHECK(r.take());
    }
    {
        Rig r(*loaded, w, profile());
        Receiver receiver;
        receiver.throws = true;
        receiver.exception_message = "Primary callback failure:";
        for (unsigned n = 0; n < 800; ++n)
            receiver.exception_message += "µ";
        CHECK(r.next(receiver) == 1 && references.empty());
        const auto primary = r.diagnostics().at("error").get<std::string>();
        CHECK(primary.starts_with("Primary callback failure:") && primary.size() <= 1024);
        r.stop();
        CHECK(r.diagnostics().at("error") == primary);
        CHECK(api->start(r.instance.value) == 0 && r.take());
    }
    std::cout << "ABI discovery, pixels, scene, retention and failure unwind passed\n";
}
void timing(const std::filesystem::path &library) {
    Workspace w;
    plugins::Loaded loaded(library);
    auto p = profile();
    p["cameras"][1]["latency_ns"] = 1000000;
    p["cameras"][1]["delivery_ns"] = 5000000;
    p["cameras"][1]["offset_ns"] = 7000;
    p["cameras"][1]["drift_ppm"] = 1000;
    {
        Rig r(loaded, w, p);
        r.advance(2000000);
        CHECK(!r.take()); // exposure already happened, delivery is still unavailable
        CHECK(number(r.diagnostics(), "right.exposures") == 1);
        r.advance(6000000);
        auto s = r.take();
        CHECK(s && s->quality == 1);
        CHECK(s->meta.at("modeled_exposure_delta_ns") == "1000000");
        const auto &left = s->frames[0], &right = s->frames[1];
        CHECK(left.native == 0 && right.native == 1008000);
        CHECK(left.received == 2000000 && right.received == 6000000);
        CHECK(right.meta.at("capture_event_ns") == "1000000" &&
              right.meta.at("availability_ns") == "6000000");
        CHECK(right.meta.at("processing_ns") == "6000000");
        CHECK(left.trigger == 0 && right.trigger == 0 && !left.group.empty() && left.group == right.group);
        CHECK(left.meta.at("simulated_trigger_generation") == right.meta.at("generation"));
        CHECK(right.meta.at("requested_trigger_ns") == "0");
    }
    p = profile();
    p["mode"] = "free";
    p["cameras"][1]["phase_ns"] = 1000000;
    p["cameras"][1]["offset_ns"] = 1000000;
    p["cameras"][1]["drift_ppm"] = 50000;
    {
        Rig r(loaded, w, p);
        r.advance(110000000);
        for (uint64_t n = 0; n < 3; ++n) {
            auto s = r.take();
            CHECK(s);
            const int64_t exposure = int64_t(n * 1000000000 / 30) + 1000000;
            CHECK(s->frames[0].sequence == n && s->frames[1].sequence == n);
            CHECK(s->frames[1].native == exposure + 1000000 + exposure * 50000 / 1000000);
            CHECK(s->frames[0].meta.at("native_clock_id") == "org.test.left" &&
                  s->frames[1].meta.at("native_clock_id") == "org.test.right");
            CHECK(s->frames[0].clock != s->frames[1].clock);
            CHECK(s->frames[0].group.empty() && s->frames[1].group.empty());
            CHECK(!s->frames[0].meta.contains("simulated_trigger_id"));
            CHECK(s->meta.at("modeled_exposure_delta_ns") == "1000000");
        }
        r.stop();
        accounting(r.diagnostics(), 2);
    }
    p = profile();
    p["mode"] = "free";
    p["cameras"][1]["phase_ns"] = 201000000;
    {
        Rig r(loaded, w, p);
        r.advance(300000000);
        auto s = r.take(20); // bounded ADR-026 startup alignment across public next calls
        CHECK(s && s->frames[0].sequence == 6 && s->frames[1].sequence == 0);
        CHECK(s->meta.at("modeled_exposure_delta_ns") == "1000000");
        CHECK(number(r.diagnostics(), "left.startup_unmatched") == 6);
        CHECK(number(r.diagnostics(), "right.startup_unmatched") == 0);
        const auto excluded = Json::parse(s->meta.at("pairing_exclusions").get<std::string>());
        CHECK(excluded.size() == 6);
        for (uint64_t n = 0; n < 6; ++n)
            CHECK(excluded.at(n).at("native_sequence") == n);
        r.stop();
        accounting(r.diagnostics(), 2);
    }
    // Independent 30.0 versus 29.97 FPS crosses a nearest-neighbor boundary.
    p = profile();
    p["mode"] = "free";
    p["cameras"][1]["rate_num"] = 2997;
    p["cameras"][1]["rate_den"] = 100;
    {
        Rig r(loaded, w, p);
        uint64_t pairs{};
        for (int64_t step = 0; step < 1200; ++step) {
            r.advance(step * 33333334);
            if (auto s = r.take()) {
                ++pairs;
                CHECK(s->frames[0].native == int64_t(s->frames[0].sequence * 1000000000 / 30));
                CHECK(s->frames[1].native == int64_t(s->frames[1].sequence * 100000000000 / 2997));
                CHECK(std::abs(s->frames[0].native - s->frames[1].native) <= 20000000);
            }
        }
        CHECK(pairs > 1100);
        const auto m = r.diagnostics();
        CHECK(number(m, "left.steady_unmatched") == 1 && number(m, "right.steady_unmatched") == 0);
        CHECK(number(m, "left.source_overruns") == 0 && number(m, "right.source_overruns") == 0);
        r.stop();
        accounting(r.diagnostics(), 2);
    }
    auto trace = [&](uint64_t seed) {
        auto configured = profile(1);
        configured["seed"] = seed;
        configured["cameras"][0]["jitter_ns"] = 1000;
        Rig r(loaded, w, configured);
        std::vector<int64_t> times;
        for (uint64_t n = 0; n < 12; ++n) {
            r.advance(int64_t(n * 1000000000 / 30 + 1000));
            auto s = r.take();
            CHECK(s && s->frames[0].sequence == n);
            const auto t = s->frames[0].native;
            CHECK(t >= int64_t(n * 1000000000 / 30) && t <= int64_t(n * 1000000000 / 30 + 1000));
            times.push_back(t);
        }
        return times;
    };
    const auto original = trace(17);
    CHECK(original == trace(17));
    CHECK(original != trace(18));
    // A delayed consumer cannot manufacture new exposures from deadline debt.
    p = profile();
    p["capacity"] = 2;
    {
        Rig r(loaded, w, p);
        r.advance(1000000000);
        auto before = r.diagnostics();
        CHECK(number(before, "left.exposures") == 31 && number(before, "right.exposures") == 31);
        CHECK(number(before, "left.source_overruns") == 29 && number(before, "right.source_overruns") == 29);
        CHECK(number(before, "left.source_high_water") == 2 &&
              number(before, "right.source_high_water") == 2);
        auto a = r.take(), b = r.take();
        CHECK(a && b && a->frames[0].sequence == 29 && b->frames[0].sequence == 30);
        CHECK(!r.take());
        auto after = r.diagnostics();
        CHECK(number(after, "left.exposures") == 31 && number(after, "publications") == 2);
        r.stop();
        accounting(r.diagnostics(), 2);
    }
    // Faults in the retained tail must not keep an obsolete sample from before
    // the skipped interval. Keep the last actual captures, not deadline slots.
    for (const auto *kind : {"drop", "missing-trigger"}) {
        auto configured = p;
        fault(configured, 0, 4, kind);
        configured["cameras"].erase(configured["cameras"].begin() + 1);
        Rig r(loaded, w, configured);
        r.advance(166666667);
        auto a = r.take(), b = r.take();
        CHECK(a && b && a->frames[0].sequence == 3 && b->frames[0].sequence == 5);
        CHECK(!r.take());
        CHECK(number(r.diagnostics(), "left.source_overruns") == 3);
        r.stop();
        accounting(r.diagnostics(), 1);
    }
    // Exposure clock advances while an actual compiled-plugin callback is held.
    {
        Rig r(loaded, w, p);
        struct Gate {
            std::promise<void> entered, release;
            std::shared_future<void> released = release.get_future().share();
            Receiver receiver;
            static int emit(void *ctx, const MantisFrameSetV1 *set) {
                auto &g = *static_cast<Gate *>(ctx);
                auto rc = Receiver::emit(&g.receiver, set);
                g.entered.set_value();
                g.released.wait();
                return rc;
            }
        } gate;
        auto entered = gate.entered.get_future();
        auto result = std::async(
            std::launch::async, [&] { return r.instance.api->next(r.instance.value, 0, Gate::emit, &gate); });
        struct Release {
            Gate &gate;
            bool done{};
            void open() {
                if (!done) {
                    done = true;
                    gate.release.set_value();
                }
            }
            ~Release() {
                open();
            }
        } release_gate{gate};
        CHECK(entered.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
        r.advance(1000000000);
        auto m = r.diagnostics();
        CHECK(number(m, "left.exposures") == 31 && number(m, "right.exposures") == 31);
        CHECK(number(m, "left.source_overruns") == 28 && number(m, "right.source_overruns") == 28);
        CHECK(number(m, "left.inflight_depth") == 1 && number(m, "right.inflight_depth") == 1);
        accounting(m, 2);
        release_gate.open();
        CHECK(result.get() == 0);
        CHECK(r.take() && r.take() && !r.take());
        r.stop();
        accounting(r.diagnostics(), 2);
    }
    // An extreme valid jump has the same finite source capacity and exact loss.
    {
        Rig r(loaded, w, p);
        r.advance(86400000000000);
        CHECK(number(r.diagnostics(), "left.exposures") == 2592001);
        CHECK(number(r.diagnostics(), "left.source_overruns") == 2591999);
        CHECK(r.take() && r.take() && !r.take());
        r.stop();
        accounting(r.diagnostics(), 2);
    }
    std::cout << "Deterministic clocks, skew, drift, pairing and finite source loss passed\n";
}
void generation_reload(const std::filesystem::path &library) {
    Workspace w;
    std::unique_ptr<Snapshot> retained;
    const auto first_library = w.root / ("first" + library.extension().string());
    const auto reopened_library = w.root / ("reopened" + library.extension().string());
    for (const auto &copy : {first_library, reopened_library})
        std::filesystem::copy_file(library, copy);
    {
        plugins::Loaded first(first_library);
        Rig r(first, w, profile());
        retained = r.take();
        CHECK(retained);
    }
    {
        plugins::Loaded reopened(reopened_library);
        Rig r(reopened, w, profile());
        auto fresh = r.take();
        CHECK(fresh && fresh->frames[0].meta.at("generation") != retained->frames[0].meta.at("generation"));
        CHECK(fresh->frames[0].clock != retained->frames[0].clock);
        CHECK(fresh->frames[0].native == 0 && retained->frames[0].native == 0);
        CHECK(fresh->frames[0].group != retained->frames[0].group);
        CHECK(fresh->frames[0].meta.at("capture_event_clock") !=
              retained->frames[0].meta.at("capture_event_clock"));
    }
    image_oracle(*retained, profile());
}
void negatives(const std::filesystem::path &library) {
    Workspace w;
    plugins::Loaded loaded(library);
    const auto *api = loaded.query<MantisAcquisitionV1>(MANTIS_ACQUISITION_V1);
    auto rejection = [&](const std::string &text) {
        std::ofstream(w.root / "rig.json") << text;
        CHECK(test::environment("MANTIS_VIRTUAL_RIG_PROFILE", (w.root / "rig.json").string().c_str()) == 0);
        unsigned count{};
        auto emit = [](void *ctx, const MantisDiscoveredDeviceV1 *) noexcept {
            ++*static_cast<unsigned *>(ctx);
            return 0;
        };
        CHECK(api->enumerate(emit, &count) != 0 && count == 0);
        void *value = reinterpret_cast<void *>(uintptr_t{1});
        CHECK(api->open(&host, "org.mantis.virtual-rig:contract", &value) != 0 && !value);
    };
    std::vector<Json> invalid;
    const auto good = profile();
    for (const auto &key :
         {"width", "height", "stride", "capacity", "seed", "tolerance_ns", "calibration_revision"}) {
        auto j = good;
        j[key] = -1;
        invalid.push_back(j);
        j[key] = UINT64_MAX;
        invalid.push_back(j);
        j[key] = 1.25;
        invalid.push_back(j);
    }
    for (const auto &key :
         {"rate_num", "rate_den", "phase_ns", "latency_ns", "delivery_ns", "drift_ppm", "jitter_ns"}) {
        auto j = good;
        j["cameras"][0][key] = UINT64_MAX;
        invalid.push_back(j);
    }
    auto j = good;
    j["format"] = "RGB";
    invalid.push_back(j);
    j = good;
    j["format"] = "Y10P";
    j["width"] = 63;
    invalid.push_back(j);
    j = good;
    j["calibration_id"] = "invented";
    invalid.push_back(j);
    j = good;
    j["stride"] = 63;
    invalid.push_back(j);
    j = good;
    j["cameras"] = Json::array();
    invalid.push_back(j);
    j = good;
    j["cameras"][1]["id"] = "left-camera";
    invalid.push_back(j);
    j = good;
    j["cameras"][1]["stream"] = "left-camera";
    invalid.push_back(j);
    j = good;
    j["cameras"][1]["clock"] = "org.test.left";
    invalid.push_back(j);
    j = good;
    j["cameras"][1]["role"] = "left";
    invalid.push_back(j);
    j = good;
    j["cameras"][0]["rate_num"] = 0;
    invalid.push_back(j);
    j = good;
    j["cameras"][0]["rate_num"] = 241;
    invalid.push_back(j);
    j = good;
    j["cameras"][0]["rate_num"] = "30";
    invalid.push_back(j);
    j = good;
    j["cameras"][0]["comparable"] = 1;
    invalid.push_back(j);
    j = good;
    j["mode"] = "free";
    j["tolerance_ns"] = 100;
    invalid.push_back(j);
    j = good;
    j["cameras"][1]["rate_num"] = 29;
    invalid.push_back(j);
    j = good;
    j["unknown"] = true;
    invalid.push_back(j);
    j = good;
    fault(j, 0, 1, "magic");
    invalid.push_back(j);
    j = good;
    fault(j, 0, 1, "delay", -1);
    invalid.push_back(j);
    j = good;
    fault(j, 0, 1, "drop");
    fault(j, 0, 1, "drop");
    invalid.push_back(j);
    for (const auto &bad : invalid)
        rejection(bad.dump());
    rejection("{");
    rejection(std::string(16385, ' '));
    rejection("{\"version\":1," + good.dump().substr(1));
    for (const auto &[kind, expected] : std::vector<std::pair<const char *, const char *>>{
             {"missing-trigger", "Missing shared trigger association"},
             {"duplicate-trigger", "Invalid per-exposure trigger association or generation"},
             {"stale-trigger", "Invalid per-exposure trigger association or generation"},
             {"generation-change", "Camera generation changed"},
             {"duplicate-counter", "Native counter"},
             {"reverse-counter", "Native counter"},
             {"reset-counter", "Native counter"},
             {"missing-time", "not comparable"},
             {"incomparable-time", "not comparable"},
             {"disconnect", "disconnected"}}) {
        auto p = profile();
        if (std::string_view(kind).find("trigger") == std::string_view::npos)
            p["mode"] = "free";
        fault(p, 1, 1, kind);
        Rig r(loaded, w, p);
        CHECK(r.take());
        bool failed{};
        for (int64_t n = 1; n <= 3 && !failed; ++n) {
            if (r.clock->advance(r.instance.value, n * 33333334) != 0) {
                failed = true;
                break;
            }
            for (unsigned work = 0; work < 4 && !failed; ++work) {
                Receiver receiver;
                failed = r.next(receiver) == 1;
            }
        }
        CHECK(failed);
        CHECK(r.diagnostics().at("error").get<std::string>().find(expected) != std::string::npos);
        r.stop();
        accounting(r.diagnostics(), 2);
        if (std::string_view(kind) == "missing-trigger")
            CHECK(number(r.diagnostics(), "right.missing_triggers") == 1);
        CHECK(r.instance.api->start(r.instance.value) == 0 && r.take());
    }
    // Large jumps stop at the first terminal event in global time/camera order,
    // accounting earlier exposures on both sources, including exact-time ties.
    for (size_t camera : {0u, 1u}) {
        auto p = profile();
        fault(p, camera, 3, "disconnect");
        Rig r(loaded, w, p);
        CHECK(r.clock->advance(r.instance.value, 1000000000) == 1);
        const auto m = r.diagnostics();
        CHECK(m.at("model_time_ns") == "100000000");
        CHECK(number(m, "left.exposures") == (camera ? 4u : 3u));
        CHECK(number(m, "right.exposures") == 3);
        CHECK(m.at("error") == "Simulated camera disconnected");
        accounting(m, 2);
    }
    // Actual configured trigger skew beyond tolerance must fail at the public path.
    {
        auto p = profile();
        p["cameras"][1]["latency_ns"] = 21000000;
        Rig r(loaded, w, p);
        r.advance(22000000);
        Receiver receiver;
        CHECK(r.next(receiver) == 1 && !receiver.result);
        CHECK(r.diagnostics().at("error") == "Camera timestamp delta exceeds profile pairing limit");
    }
    // Free-running bracketing rejects both candidates outside tolerance.
    {
        auto p = profile();
        p["mode"] = "free";
        p["tolerance_ns"] = 16666667;
        p["cameras"][1]["phase_ns"] = 18000000;
        fault(p, 0, 1, "drop");
        Rig r(loaded, w, p);
        r.advance(70000000);
        Receiver receiver;
        CHECK(r.next(receiver) == 2);
        CHECK(r.next(receiver) == 1 && !receiver.result);
        CHECK(r.diagnostics().at("error") == "Nearest camera timestamps exceed profile pairing limit");
        CHECK(number(r.diagnostics(), "left.source_drops") == 1);
        accounting(r.diagnostics(), 2);
    }
    {
        auto p = profile();
        p["mode"] = "free";
        p["cameras"][1]["comparable"] = false;
        Rig r(loaded, w, p);
        Receiver receiver;
        CHECK(r.next(receiver) == 1 && !receiver.result);
        CHECK(r.diagnostics().at("error") == "Camera timestamp clocks are not comparable");
    }
    // Delayed availability can reverse native delivery; it is never silently paired.
    {
        auto p = profile(1);
        p["mode"] = "free";
        fault(p, 0, 1, "delay", 100000000);
        Rig r(loaded, w, p);
        CHECK(r.take());
        r.advance(70000000);
        auto later = r.take();
        CHECK(later && later->frames[0].sequence == 2);
        r.advance(140000000);
        auto intervening = r.take();
        CHECK(intervening && intervening->frames[0].sequence == 3);
        Receiver receiver;
        CHECK(r.next(receiver) == 1);
        CHECK(r.diagnostics().at("error") == "Native counter repeated, reversed or reset");
        accounting(r.diagnostics(), 1);
    }
    // Source delivery stall and a known source drop remain separately accounted.
    {
        auto p = profile(1);
        p["mode"] = "free";
        p["capacity"] = 2;
        fault(p, 0, 1, "stall", 100000000);
        fault(p, 0, 3, "drop");
        Rig r(loaded, w, p);
        CHECK(r.take());
        r.advance(70000000);
        CHECK(!r.take());
        r.advance(150000000);
        CHECK(r.take());
        r.stop();
        auto m = r.diagnostics();
        CHECK(number(m, "left.source_drops") == 1 && number(m, "left.source_overruns") == 1);
        accounting(m, 1);
    }
    for (const auto t : std::array<int64_t, 3>{-1, 86400000000001, INT64_MAX}) {
        Rig r(loaded, w, profile());
        CHECK(r.clock->advance(r.instance.value, t) != 0);
        CHECK(r.diagnostics().at("error") == "Virtual clock must be monotone within the 24-hour generation");
    }
    // A single observation with missing native time stays explicitly unknown;
    // stereo cannot turn the same missing evidence into correspondence.
    {
        auto p = profile(1);
        p["mode"] = "free";
        fault(p, 0, 1, "missing-time");
        Rig r(loaded, w, p);
        CHECK(r.take());
        r.advance(33333334);
        auto s = r.take();
        CHECK(s && s->quality == 0 && s->frames[0].quality == 0);
        CHECK(s->frames[0].clock.empty() && s->frames[0].native == 0);
        CHECK(s->frames[0].meta.at("timestamp_valid") == "false");
        r.stop();
        accounting(r.diagnostics(), 1);
    }
    // Availability between exposure ticks must deliver without spinning until
    // the next ten-second exposure. Timeout bounds failure, not an exact clock oracle.
    {
        auto p = profile(1);
        p["cameras"][0]["rate_num"] = 1;
        p["cameras"][0]["rate_den"] = 10;
        p["cameras"][0]["delivery_ns"] = 1000000;
        Rig r(loaded, w, p, false);
        Receiver receiver;
        CHECK(r.next(receiver, 1000) == 0 && receiver.calls == 1);
        CHECK(receiver.result->frames[0].received >=
              std::stoll(receiver.result->frames[0].meta.at("reference_origin_host_ns").get<std::string>()) +
                  1000000);
        CHECK(receiver.result->frames[0].meta.at("host_receive_clock") == "std.chrono.steady_clock");
        CHECK(r.diagnostics().at("host_receive_clock") == "std.chrono.steady_clock");
    }
    // Observe real wait entry before cancellation; cleanup always releases it.
    {
        auto p = profile();
        for (auto &c : p["cameras"])
            c["latency_ns"] = 10000000000;
        Rig r(loaded, w, p, false);
        Receiver receiver;
        auto result = std::async(std::launch::async, [&] { return r.next(receiver, 1000); });
        struct Cancel {
            Rig &rig;
            ~Cancel() {
                rig.clock->cancel_wait(rig.instance.value);
            }
        } cancel{r};
        CHECK(r.clock->wait_for_waiter(r.instance.value, 1000) == 0);
        CHECK(r.clock->cancel_wait(r.instance.value) == 0);
        CHECK(result.get() == 1);
        CHECK(r.diagnostics().at("error") == "Acquisition wait cancelled");
        CHECK(number(r.diagnostics(), "next_waits") >= 1);
        r.stop();
    }
    CHECK(references.empty());
    std::cout
        << "Invalid profiles, source faults, trigger generations and observed wait cancellation passed\n";
}
std::string canonical(const data::Packet &p) {
    std::ostringstream bytes(std::ios::binary);
    data::write_packet(bytes, p);
    return bytes.str();
}
void runtime(const std::filesystem::path &library) {
    Workspace w;
    const auto plugin_dir = w.root / "plugins";
    std::filesystem::create_directory(plugin_dir);
    std::filesystem::copy_file(library, plugin_dir / library.filename());
    std::filesystem::copy_file(library.parent_path() / "virtual-rig.json", plugin_dir / "virtual-rig.json");
    auto store = std::make_shared<artifact::Store>(w.root / "Recording.mantis");
    for (unsigned scenario = 0; scenario < 3; ++scenario) {
        const bool packed = scenario == 1, writer_fault = scenario == 2;
        auto p = profile(2, packed ? "Y10P" : "GREY", packed ? 1280 : 640, packed ? 720 : 480);
        w.profile(p);
        plugins::Registry registry({}, w.root / "hosts", {});
        registry.discover(plugin_dir, {"org.mantis.virtual-rig"});
        auto streams = registry.devices();
        CHECK(streams.size() == 1 && streams[0]->source_paced() && streams[0]->components().size() == 2);
        auto raw = store->begin({"org.mantis.RawCapture", 2}, {});
        std::vector<data::Published> live;
        std::promise<void> ready;
        auto reached = ready.get_future();
        std::atomic_bool signalled{};
        auto signal = [&] {
            if (!signalled.exchange(true))
                ready.set_value();
        };
        auto session = std::make_unique<device::Session>(
            device::CaptureDescriptor{Id::random(), {streams[0]->descriptor().id}, {}},
            std::vector<device::ImageStream *>{streams[0].get()},
            [&](data::Published packet) {
                if (writer_fault && live.size() == 1)
                    throw std::runtime_error("Injected simulator recording failure");
                store->append(raw, *packet);
                live.push_back(std::move(packet));
                if (live.size() == 6)
                    signal();
            },
            [&](const LogRecord &r) {
                if (r.level == "error")
                    signal();
            });
        const auto completed = reached.wait_for(std::chrono::seconds(5));
        session->stop(); // bounded cleanup precedes every assertion about results
        CHECK(completed == std::future_status::ready);
        if (writer_fault) {
            CHECK(session->error().find("Injected simulator recording failure") != std::string::npos);
            CHECK(live.size() == 1);
        } else {
            if (!session->error().empty())
                throw std::runtime_error(session->error());
            CHECK(live.size() >= 6 && session->produced() == session->committed());
            CHECK(session->committed() == live.size());
            CHECK(session->metrics().dropped == 0 && session->saturation() == 0);
        }
        CHECK(streams[0]->diagnostics().at("pixel_copy_count") == "0");
        const auto counts = Json(streams[0]->diagnostics());
        accounting(counts, 2);
        for (const auto &packet : live) {
            CHECK(packet->frames.size() == 2 && packet->header.sync_quality == time::SyncQuality::software);
            CHECK(packet->header.metadata.at("physical_exposure_skew") == "unavailable");
            for (size_t i = 0; i < 2; ++i) {
                const auto &f = *packet->frames[i];
                const auto layout = data::image_layout(f);
                CHECK(layout.width == p.at("width").get<uint32_t>() &&
                      layout.height == p.at("height").get<uint32_t>());
                CHECK(layout.row_stride == p.at("stride").get<uint32_t>());
                CHECK(f.header.calibration.id.value == p.at("calibration_id").get<std::string>());
                CHECK(f.header.metadata.at("component_id") == streams[0]->components()[i].id.value);
                CHECK(f.header.timestamp.domain.id == live.front()->frames[i]->header.timestamp.domain.id);
                CHECK(f.header.metadata.at("native_clock_id") ==
                      p.at("cameras")[i].at("clock").get<std::string>());
                CHECK(f.header.metadata.at("host_receive_clock") == "std.chrono.steady_clock");
                CHECK(f.header.timestamp.nanoseconds == std::stoll(f.header.metadata.at("capture_event_ns")));
                CHECK(f.header.received.nanoseconds >=
                      std::stoll(f.header.metadata.at("reference_origin_host_ns")) +
                          std::stoll(f.header.metadata.at("availability_ns")));
            }
        }
        // A restart is a new capture: its sequence resets, so RawCapture requires
        // a separate schema-2 artifact. Retained history keeps its old clock ID.
        const auto historical = canonical(*live.front());
        const auto old_clock = live.front()->frames[0]->header.timestamp.domain.id;
        const auto old_native = live.front()->frames[0]->header.timestamp.nanoseconds;
        CHECK(streams[0]->start());
        auto restarted = streams[0]->next();
        const auto stopped = streams[0]->stop();
        CHECK(stopped);
        CHECK(restarted && *restarted && (*restarted)->header.sequence.value == 0);
        for (size_t i = 0; i < 2; ++i) {
            const auto &fresh = (*restarted)->frames[i]->header;
            const auto &original = live.front()->frames[i]->header;
            CHECK(fresh.timestamp.domain.id != original.timestamp.domain.id);
            CHECK(fresh.timestamp.nanoseconds == 0);
            CHECK(fresh.metadata.at("native_clock_id") == original.metadata.at("native_clock_id"));
            CHECK(fresh.metadata.at("component_id") == original.metadata.at("component_id"));
            CHECK(fresh.metadata.at("stream_id") == original.metadata.at("stream_id"));
        }
        CHECK(canonical(*live.front()) == historical);
        CHECK(live.front()->frames[0]->header.timestamp.domain.id == old_clock);
        CHECK(live.front()->frames[0]->header.timestamp.nanoseconds == old_native);
        auto recordings = std::vector{std::pair{raw, live}};
        if (!writer_fault) {
            CHECK(live.back()->frames[0]->header.timestamp.nanoseconds >
                  (*restarted)->frames[0]->header.timestamp.nanoseconds);
            auto second_raw = store->begin({"org.mantis.RawCapture", 2}, {});
            store->append(second_raw, **restarted);
            recordings.emplace_back(second_raw, std::vector<data::Published>{*restarted});
        }
        // Deliberately abandon unfinished schema-2 writers, execute recovery,
        // then replay each exactly twice; no process-kill durability claim.
        for (const auto &[artifact, packets] : recordings) {
            store->abandon(artifact);
            CHECK(store->recover(artifact).state == artifact::ArtifactState::finalized);
        }
        CHECK(test::environment("MANTIS_VIRTUAL_RIG_PROFILE", nullptr) == 0);
        for (unsigned pass = 0; pass < 2; ++pass) {
            for (const auto &[artifact, packets] : recordings) {
                auto replay = device::recorded_source(store, artifact, false);
                CHECK(replay->start());
                for (const auto &original : packets) {
                    auto value = replay->next();
                    CHECK(value && *value);
                    for (size_t i = 0; i < 2; ++i) {
                        const auto &actual = (*value)->frames[i]->header.timestamp;
                        const auto &expected = original->frames[i]->header.timestamp;
                        CHECK(actual.domain.id == expected.domain.id &&
                              actual.nanoseconds == expected.nanoseconds);
                    }
                    CHECK(canonical(**value) == canonical(*original));
                }
                auto end = replay->next();
                CHECK(end && !*end && replay->stop());
            }
        }
        if (!writer_fault) {
            CHECK(recordings.size() == 2);
            CHECK(recordings[0].second.front()->frames[0]->header.timestamp.domain.id !=
                  recordings[1].second.front()->frames[0]->header.timestamp.domain.id);
        }
        std::cout << (packed ? "1280x720 Y10P" : "640x480 GREY")
                  << (writer_fault ? " writer failure" : " recording") << ": " << live.size()
                  << " FrameSets; native epoch restart, recovery and two byte-exact replays passed\n";
    }
}
} // namespace
int main(int argc, char **argv) {
    try {
        CHECK(argc == 3);
        if (std::string_view(argv[1]) == "contract") {
            contract(argv[2]);
            timing(argv[2]);
            negatives(argv[2]);
            generation_reload(argv[2]);
            CHECK(active_bytes == 0);
            std::cout << "Host ABI peak: " << peak_buffers << " buffers / " << peak_bytes
                      << " bytes; no retained references\n";
        } else {
            CHECK(std::string_view(argv[1]) == "runtime");
            runtime(argv[2]);
        }
        CHECK(references.empty());
        CHECK(test::environment("MANTIS_VIRTUAL_RIG_PROFILE", nullptr) == 0);
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
