#include "../../plugins/first-party/devices/x1/pairing.hpp"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <mantis/plugin_runtime.hpp>
#include <mantis/replay.hpp>
#include <mantis/data_io.hpp>
#include <mantis/image_layout.hpp>
#include <nlohmann/json.hpp>
using namespace mantis;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string("Check failed: ") + #x); } while (false)
std::string canonical(const data::Packet &packet) {
    std::ostringstream out(std::ios::binary); data::write_packet(out, packet); return out.str();
}
struct Scenario {
    const char *name;
    bool hardware{}, valid{true};
    unsigned startup_left{}, startup_right{};
    int64_t offset{};
    const char *error{};
    const char *counter{};
};
int main(int argc, char **argv) {
    try {
        CHECK(argc == 2);
        auto root = std::filesystem::temp_directory_path() / Id::random().value;
        std::filesystem::create_directory(root);
        struct Cleanup { std::filesystem::path path; ~Cleanup() { std::filesystem::remove_all(path); } } cleanup{root};
        setenv("MANTIS_X1_PROFILE", (root / "profile.json").c_str(), 1);
        auto store = std::make_shared<artifact::Store>(root / "paired.mantis");
        constexpr int64_t period = 1000000000 / 120;
        for (const char *format : {"GREY", "Y10P"}) for (const Scenario scenario : {
            Scenario{"normal"}, Scenario{"close-timestamps"}, Scenario{"startup-left", false, true, 6, 0, 6},
            Scenario{"startup-right", false, true, 0, 6, -6}, Scenario{"unequal-origins", false, true, 0, 0, -100},
            Scenario{"lag", false, false, 0, 0, 0, "pairing limit", "pairing_failures"},
            Scenario{"startup-limit", false, false, 32, 0, 0, "unmatched-observation limit", "pairing_failures"},
            Scenario{"incomparable", false, false, 0, 0, 0, "not comparable", "pairing_failures"},
            Scenario{"unknown-clock", false, false, 0, 0, 0, "not comparable", "pairing_failures"},
            Scenario{"startup-left-gap", false, false, 6, 0, 6, "native sequence", "right.sequence_gaps"},
            Scenario{"startup-left-repeat", false, false, 6, 0, 6, "native sequence", "right.repeated_or_reversed_sequences"},
            Scenario{"startup-left-reverse", false, false, 6, 0, 6, "native sequence", "right.repeated_or_reversed_sequences"},
            Scenario{"startup-left-timestamp-jump", false, false, 6, 0, 6, "timestamp discontinuity", "timestamp_discontinuities"},
            Scenario{"startup-left-clock-change", false, false, 6, 0, 6, "clock discontinuity", "timestamp_discontinuities"},
            Scenario{"startup-left-steady-delta", false, false, 6, 0, 6, "pairing limit", "pairing_failures"},
            Scenario{"normal", true}, Scenario{"close-timestamps", true},
            Scenario{"startup-left", true, false, 0, 0, 0, "pairing limit", "pairing_failures"},
            Scenario{"unequal-origins", true, false, 0, 0, 0, "counters disagree", "pairing_failures"}
        }) {
            std::cout << format << ' ' << scenario.name << " hardware=" << scenario.hardware << std::endl;
            nlohmann::json profile{{"format_version", 1}, {"measurement_cameras", {
                {"left", {{"sensor_identity", "ov9281 18-0060"}}}, {"right", {{"sensor_identity", "ov9281 20-0060"}}}}},
                {"mode", {{"width", 64}, {"height", 48}, {"fourcc", format}, {"fps", 120}}},
                {"hardware_sync_configured", scenario.hardware}, {"max_v4l2_delta_ns", 4000000}, {"stall_timeout_ms", 1000},
                {"calibration_id", "pairing.calibration"}, {"calibration_revision", 9}};
            { std::ofstream out(root / "profile.json"); out << profile; }
            setenv("MANTIS_X1_FAKE", scenario.name, 1);
            plugins::Registry registry(std::filesystem::path(argv[1]).parent_path() / "bin/mantis-plugin-host", root / "hosts", {});
            registry.discover(argv[1], {"org.mantis.x1"});
            auto devices = registry.devices(); CHECK(devices.size() == 1);
            auto &stream = *devices[0]; CHECK(stream.start());
            std::vector<data::Published> live;
            bool failed{};
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            while (live.size() < 8) {
                CHECK(std::chrono::steady_clock::now() < deadline);
                auto result = stream.next();
                if (!result) { failed = true; break; }
                if (!*result) continue;
                auto p = *result; auto n = live.size();
                CHECK(p->header.sequence.value == n && p->frames.size() == 2);
                const auto &meta = p->header.metadata;
                CHECK(meta.at("pairing_mode") == (scenario.hardware ? "native-sequence-and-timestamp" : "timestamp-nearest"));
                CHECK(meta.at("native_sequence_offset") == std::to_string(scenario.offset));
                CHECK(meta.at("sync_quality") == "software" && meta.at("exposure_skew") == "unavailable");
                CHECK(meta.at("startup_unmatched_left") == std::to_string(scenario.startup_left));
                CHECK(meta.at("startup_unmatched_right") == std::to_string(scenario.startup_right));
                CHECK(std::stoll(meta.at("paired_v4l2_delta_ns")) == p->frames[1]->header.timestamp.nanoseconds - p->frames[0]->header.timestamp.nanoseconds);
                CHECK(std::abs(std::stoll(meta.at("paired_v4l2_delta_ns"))) <= 4000000);
                for (size_t i = 0; i < 2; ++i) {
                    const auto &f = *p->frames[i];
                    auto native = n + (i ? scenario.startup_right : scenario.startup_left);
                    if (std::string_view(scenario.name) == "unequal-origins" && i) native += 100;
                    CHECK(f.header.sequence.value == native);
                    CHECK(meta.at(i ? "right.native_sequence" : "left.native_sequence") == std::to_string(native));
                    CHECK(f.header.metadata.at("role") == (i ? "right" : "left"));
                    CHECK(f.header.calibration.id.value == "pairing.calibration" && f.header.calibration.revision == 9);
                    CHECK(f.header.sync.trigger == n && f.header.sync_quality == time::SyncQuality::software);
                    int64_t timestamp = int64_t(n) * period;
                    if (scenario.startup_left || scenario.startup_right) timestamp += 6 * period;
                    if ((scenario.startup_left && i) || (scenario.startup_right && !i) ||
                        (std::string_view(scenario.name) == "close-timestamps" && i)) timestamp += 1800000;
                    CHECK(f.header.timestamp.nanoseconds == timestamp);
                    CHECK(f.header.timestamp.domain.id.value == "org.mantis.fake.monotonic");
                    CHECK(f.header.received.nanoseconds == timestamp + 1000 + (i ? 100 : 0));
                    auto bytes = f.attributes[0].buffer.map_read(); CHECK(bytes);
                    const bool packed = std::string_view(format) == "Y10P";
                    CHECK(bytes->size() == (packed ? 3840u : 3072u));
                    CHECK(data::image_layout(f).packing == (packed ? data::ImagePacking::mipi_raw10 : data::ImagePacking::raw8));
                    for (size_t j = 0; j < bytes->size(); ++j)
                        CHECK((*bytes)[j] == static_cast<std::byte>((j + native * 7 + (i ? 97 : 0)) & 255));
                }
                live.push_back(p);
            }
            CHECK(failed != scenario.valid); CHECK(stream.stop());
            auto metrics = stream.diagnostics();
            CHECK(std::stoull(metrics.at("pending_high_water_left")) <= x1::pairing_capacity);
            CHECK(std::stoull(metrics.at("pending_high_water_right")) <= x1::pairing_capacity);
            CHECK(metrics.at("pairing_pending_saturation") == "0");
            CHECK(metrics.at("startup_unmatched_left") == std::to_string(scenario.startup_left));
            CHECK(metrics.at("startup_unmatched_right") == std::to_string(scenario.startup_right));
            if (!scenario.valid) {
                std::cout << "Expected failure: " << metrics.at("error") << '\n';
                CHECK(metrics.at("error").find(scenario.error) != std::string::npos);
                CHECK(std::stoull(metrics.at(scenario.counter)) > 0);
                if (std::string_view(scenario.name).starts_with("startup-left-") && scenario.startup_left) CHECK(live.size() >= 3);
                continue;
            }
            // Every successfully acquired observation is accounted for: published,
            // startup excluded, or explicitly retained as terminal lookahead.
            for (const char *role : {"left", "right"}) {
                CHECK(std::stoull(metrics.at(std::string(role) + ".frames")) == live.size() +
                    std::stoull(metrics.at(std::string("startup_unmatched_") + role)) +
                    std::stoull(metrics.at(std::string("shutdown_unmatched_") + role)));
                CHECK(metrics.at(std::string(role) + ".sequence_gaps") == "0");
                CHECK(metrics.at(std::string(role) + ".capture_errors") == "0");
            }
            CHECK(metrics.at("pairing_failures") == "0");
            auto id = store->begin({"org.mantis.RawCapture", 2}, {});
            for (const auto &p : live) store->append(id, *p);
            CHECK(store->finalize(id).state == artifact::ArtifactState::finalized);
            for (unsigned pass = 0; pass < 2; ++pass) {
                auto replay = device::recorded_source(store, id, false); CHECK(replay->start());
                for (const auto &original : live) {
                    auto result = replay->next(); CHECK(result && *result);
                    CHECK(canonical(**result) == canonical(*original));
                }
                auto end = replay->next(); CHECK(end && !*end); CHECK(replay->stop());
            }
        }
        unsetenv("MANTIS_X1_PROFILE"); unsetenv("MANTIS_X1_FAKE");
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
