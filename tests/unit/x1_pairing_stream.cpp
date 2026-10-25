#include "../../plugins/first-party/devices/x1/pairing.hpp"
#include <cstdlib>
#include <fstream>
#include <future>
#include <iostream>
#include <sstream>
#include <set>
#include <mantis/device_runtime.hpp>
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
        CHECK(argc == 2 || argc == 5);
        // CTest runs each independent scenario with the original finite deadline.
        // The aggregate invocation remains available for repeat/load investigation.
        unsigned selected{};
        auto select = [&](const char *format, const char *name, const char *mode) {
            if (argc == 5 && (std::string_view(argv[2]) != format ||
                std::string_view(argv[3]) != name || std::string_view(argv[4]) != mode)) return false;
            ++selected;
            return true;
        };
        auto root = std::filesystem::temp_directory_path() / Id::random().value;
        std::filesystem::create_directory(root);
        struct Cleanup { std::filesystem::path path; ~Cleanup() { std::filesystem::remove_all(path); } } cleanup{root};
        setenv("MANTIS_X1_PROFILE", (root / "profile.json").c_str(), 1);
        auto store = std::make_shared<artifact::Store>(root / "paired.mantis");
        constexpr int64_t period = 1000000000 / 120;
        for (const char *format : {"GREY", "Y10P"}) for (const Scenario scenario : {
            Scenario{"normal"}, Scenario{"close-timestamps"}, Scenario{"startup-left", false, true, 6, 0, 6},
            Scenario{"startup-right", false, true, 0, 6, -6}, Scenario{"unequal-origins", false, true, 0, 0, -100},
            Scenario{"phase-out-of-bound", false, false, 0, 0, 0, "Nearest camera timestamps", "pairing_failures"},
            Scenario{"startup-limit", false, false, 32, 0, 0, "unmatched-observation limit", "pairing_failures"},
            Scenario{"incomparable", false, false, 0, 0, 0, "not comparable", "pairing_failures"},
            Scenario{"unknown-clock", false, false, 0, 0, 0, "not comparable", "pairing_failures"},
            Scenario{"startup-left-gap", false, false, 6, 0, 6, "native sequence", "right.sequence_gaps"},
            Scenario{"startup-left-repeat", false, false, 6, 0, 6, "native sequence", "right.repeated_or_reversed_sequences"},
            Scenario{"startup-left-reverse", false, false, 6, 0, 6, "native sequence", "right.repeated_or_reversed_sequences"},
            Scenario{"startup-left-timestamp-jump", false, false, 6, 0, 6, "timestamp discontinuity", "timestamp_discontinuities"},
            Scenario{"startup-left-clock-change", false, false, 6, 0, 6, "clock discontinuity", "timestamp_discontinuities"},
            Scenario{"startup-left-steady-delta", false, true, 6, 0, 6},
            Scenario{"normal", true}, Scenario{"close-timestamps", true},
            Scenario{"startup-left", true, false, 0, 0, 0, "pairing limit", "pairing_failures"},
            Scenario{"unequal-origins", true, false, 0, 0, 0, "counters disagree", "pairing_failures"}
        }) {
            if (!select(format, scenario.name, scenario.hardware ? "hardware" : "software")) continue;
            std::cout << format << ' ' << scenario.name << " hardware=" << scenario.hardware << std::endl;
            nlohmann::json profile{{"format_version", 1}, {"measurement_cameras", {
                {"left", {{"sensor_identity", "ov9281 18-0060"}}}, {"right", {{"sensor_identity", "ov9281 20-0060"}}}}},
                {"mode", {{"width", 64}, {"height", 48}, {"fourcc", format}, {"fps", 120}}},
                {"hardware_sync_configured", scenario.hardware}, {"max_v4l2_delta_ns", scenario.hardware ? 4000000 : 5000000}, {"stall_timeout_ms", 1000},
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
                const bool shifted = std::string_view(scenario.name) == "startup-left-steady-delta" && n >= 4;
                CHECK(p->header.sequence.value == n && p->frames.size() == 2);
                const auto &meta = p->header.metadata;
                CHECK(meta.at("pairing_mode") == (scenario.hardware ? "native-sequence-and-timestamp" : "timestamp-nearest"));
                CHECK(meta.at("native_sequence_offset") == std::to_string(scenario.offset + (shifted ? 1 : 0)));
                CHECK(meta.at("sync_quality") == "software" && meta.at("exposure_skew") == "unavailable");
                CHECK(meta.at("startup_unmatched_left") == std::to_string(scenario.startup_left));
                CHECK(meta.at("startup_unmatched_right") == std::to_string(scenario.startup_right));
                CHECK(std::stoll(meta.at("paired_v4l2_delta_ns")) == p->frames[1]->header.timestamp.nanoseconds - p->frames[0]->header.timestamp.nanoseconds);
                CHECK(std::abs(std::stoll(meta.at("paired_v4l2_delta_ns"))) <= (scenario.hardware ? 4000000 : 5000000));
                for (size_t i = 0; i < 2; ++i) {
                    const auto &f = *p->frames[i];
                    auto native = n + (i ? scenario.startup_right : scenario.startup_left);
                    if (shifted && !i) ++native;
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
                    if (std::string_view(scenario.name) == "startup-left-steady-delta")
                        timestamp = int64_t(native) * period + (i ? 6 * period + 1800000 + (native >= 4 ? period / 2 : 0) : 0);
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
                if (std::string_view(scenario.name) == "phase-out-of-bound") {
                    CHECK(metrics.at("max_v4l2_delta_ns") == "5000000");
                    CHECK(metrics.at("pairing_candidate_timestamp_ns_left") == "12000000");
                    CHECK(metrics.at("pairing_candidate_timestamp_ns_right") == "18000000");
                    CHECK(metrics.at("pairing_lookahead_timestamp_ns_left") == "24000000");
                    CHECK(metrics.at("pairing_candidate_distance_ns") == "6000000");
                    CHECK(metrics.at("pairing_lookahead_distance_ns") == "6000000");
                    CHECK(metrics.at("pairing_nearest_candidate_distance_ns") == "6000000");
                    CHECK(metrics.at("pairing_candidates_bracketed") == "true");
                    CHECK(metrics.at("pairing_state") == "failed");
                    CHECK(metrics.at("pairing_failure_reason").find("Nearest camera timestamps") != std::string::npos);
                    for (const char *role : {"left", "right"}) {
                        auto prefix = std::string(role) + ".";
                        const auto interval = std::string_view(role) == "left" ? 12000000 : 18000000;
                        CHECK(metrics.at(prefix + "observed_period_ns") == std::to_string(interval));
                        CHECK(metrics.at(prefix + "observed_half_period_ns") == std::to_string(interval / 2));
                        CHECK(metrics.at(prefix + "observed_max_period_ns") == std::to_string(interval));
                        CHECK(metrics.at(prefix + "capture_errors") == "0" && metrics.at(prefix + "sequence_gaps") == "0");
                    }
                }
                if (std::string_view(scenario.name).starts_with("startup-left-") && scenario.startup_left) CHECK(live.size() >= 3);
                continue;
            }
            // Every successfully acquired observation is accounted for: published,
            // startup excluded, or explicitly retained as terminal lookahead.
            for (const char *role : {"left", "right"}) {
                CHECK(std::stoull(metrics.at(std::string(role) + ".frames")) == live.size() +
                    std::stoull(metrics.at(std::string("startup_unmatched_") + role)) +
                    std::stoull(metrics.at(std::string("steady_state_unmatched_") + role)) +
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
        // Real loaded plugin -> bounded recorder -> RawCapture -> exact replay.
        for (const char *format : {"GREY", "Y10P"}) for (const char *name : {
            "phase-4000", "phase-half", "phase-4300", "phase-drift-left", "phase-drift-right",
            "phase-drift-gap", "phase-limit", "phase-jitter-left", "phase-jitter-right"}) {
            if (!select(format, name, "recorder")) continue;
            const std::string_view scenario = name;
            const bool valid = scenario != "phase-drift-gap" && scenario != "phase-limit";
            nlohmann::json profile{{"format_version", 1}, {"measurement_cameras", {
                {"left", {{"sensor_identity", "ov9281 18-0060"}}}, {"right", {{"sensor_identity", "ov9281 20-0060"}}}}},
                {"mode", {{"width", 64}, {"height", 48}, {"fourcc", format}, {"fps", 120}}},
                {"max_v4l2_delta_ns", 5000000}, {"stall_timeout_ms", 1000}};
            { std::ofstream out(root / "profile.json"); out << profile; }
            setenv("MANTIS_X1_FAKE", name, 1);
            plugins::Registry registry(std::filesystem::path(argv[1]).parent_path() / "bin/mantis-plugin-host", root / "hosts", {});
            registry.discover(argv[1], {"org.mantis.x1"});
            auto devices = registry.devices(); CHECK(devices.size() == 1);
            auto id = store->begin({"org.mantis.RawCapture", 2}, {});
            std::vector<data::Published> live;
            std::promise<void> ready; auto reached = ready.get_future();
            std::atomic_bool signalled{};
            std::unique_ptr<device::Session> session;
            try {
                session = std::make_unique<device::Session>(device::CaptureDescriptor{Id::random(), {devices[0]->descriptor().id}, {}},
                    std::vector<device::ImageStream *>{devices[0].get()}, [&](data::Published packet) {
                        store->append(id, *packet); live.push_back(packet);
                        if (valid && live.size() == 96 && !signalled.exchange(true)) ready.set_value();
                    }, [&](const LogRecord &) { if (!valid && !signalled.exchange(true)) ready.set_value(); });
            } catch (const Failure &) {
                // Accelerated invalid fixtures may fail before the first writer
                // commit. Construction then stops/drains without returning a
                // handle; check the same exact failure from retained diagnostics.
                CHECK(!valid);
            }
            if (session) {
                CHECK(reached.wait_for(std::chrono::seconds(3)) == std::future_status::ready);
                session->stop();
                CHECK(session->metrics().dropped == 0 && session->saturation() == 0);
                CHECK(session->produced() == session->committed() && session->committed() == live.size());
            }
            auto metrics = session ? session->diagnostics() : devices[0]->diagnostics();
            CHECK(metrics.at("sync_quality") == "software" && metrics.at("exposure_skew") == "unavailable");
            CHECK(metrics.at("pairing_pending_saturation") == "0");
            for (const char *role : {"left", "right"}) CHECK(std::stoull(metrics.at(std::string("pending_high_water_") + role)) <= 2);
            if (!valid) {
                CHECK(!metrics.at("error").empty());
                if (session) CHECK(!session->error().empty());
                if (scenario == "phase-drift-gap") {
                    CHECK(std::stoull(metrics.at("steady_state_unmatched_left")) > 0);
                    CHECK(metrics.at("right.sequence_gaps") == "1");
                    CHECK(metrics.at("right.capture_errors") == "1");
                } else {
                    CHECK(metrics.at("steady_state_unmatched_left") == "2");
                    CHECK(metrics.at("error").find("unmatched-observation limit") != std::string::npos);
                }
                for (size_t n = 0; n < live.size(); ++n) CHECK(live[n]->header.sequence.value == n);
                for (const char *role : {"left", "right"}) {
                    // The deliberately discontinuous observation is rejected at
                    // receipt; all healthy observations still have exact accounting.
                    if (scenario == "phase-drift-gap" && std::string_view(role) == "right") continue;
                    CHECK(std::stoull(metrics.at(std::string(role) + ".frames")) == live.size() +
                        std::stoull(metrics.at(std::string("startup_unmatched_") + role)) +
                        std::stoull(metrics.at(std::string("steady_state_unmatched_") + role)) +
                        std::stoull(metrics.at(std::string("shutdown_unmatched_") + role)));
                }
                continue;
            }
            CHECK(session && session->error().empty() && metrics.at("pairing_failures") == "0");
            std::array<std::set<uint64_t>, 2> accounted;
            std::array<uint64_t, 2> exclusions{};
            auto timestamp = [&](size_t i, uint64_t native) {
                if (scenario == "phase-jitter-left" || scenario == "phase-jitter-right")
                    return int64_t(native) * 8384000 + (native == 2 &&
                        i == (scenario == "phase-jitter-right" ? 1u : 0u) ? 4000000 : 0);
                int64_t camera_period = scenario == "phase-4300" ? 8600000 : 8384000;
                int64_t phase = scenario == "phase-4300" ? 4300000 : scenario == "phase-4000" ? 4000000 : 4192000;
                if (scenario == "phase-drift-left") { if (i) camera_period += 210; phase -= 5000; }
                if (scenario == "phase-drift-right") { if (!i) camera_period += 210; phase += 5000; }
                return int64_t(native) * camera_period + (i ? phase : 0);
            };
            auto account_exclusions = [&](const nlohmann::json &items) {
                for (const auto &item : items) {
                    size_t i = item.at("role") == "right" ? 1 : 0;
                    CHECK(accounted[i].insert(item.at("native_sequence").get<uint64_t>()).second);
                    CHECK(item.at("phase") == "startup" || item.at("phase") == "steady-state");
                    CHECK(item.at("clock") == "org.mantis.fake.monotonic");
                    CHECK(item.at("timestamp_ns").get<int64_t>() == timestamp(i, item.at("native_sequence").get<uint64_t>()));
                    ++exclusions[i];
                }
            };
            for (size_t n = 0; n < live.size(); ++n) {
                const auto &packet = *live[n]; const auto &meta = packet.header.metadata;
                CHECK(packet.header.sequence.value == n && packet.frames.size() == 2);
                auto excluded = nlohmann::json::parse(meta.at("pairing_exclusions"));
                CHECK(excluded.size() <= (n ? x1::steady_state_discard_limit : x1::startup_discard_limit));
                account_exclusions(excluded);
                CHECK(meta.at("pairing_policy_version") == "2");
                CHECK(meta.at("sync_quality") == "software" && meta.at("exposure_skew") == "unavailable");
                const auto delta = packet.frames[1]->header.timestamp.nanoseconds - packet.frames[0]->header.timestamp.nanoseconds;
                CHECK(std::abs(delta) <= 5000000 && meta.at("paired_v4l2_delta_ns") == std::to_string(delta));
                if (scenario == "phase-half") CHECK(delta == 4192000);
                if (scenario == "phase-4300") CHECK(delta == 4300000);
                if (scenario == "phase-4000") CHECK(delta == 4000000);
                for (size_t i = 0; i < 2; ++i) {
                    const auto &frame = *packet.frames[i]; auto native = frame.header.sequence.value;
                    CHECK(frame.header.timestamp.nanoseconds == timestamp(i, native));
                    CHECK(accounted[i].insert(native).second);
                    CHECK(frame.header.sync_quality == time::SyncQuality::software);
                    auto bytes = frame.attributes[0].buffer.map_read(); CHECK(bytes);
                    for (size_t j = 0; j < bytes->size(); ++j)
                        CHECK((*bytes)[j] == static_cast<std::byte>((j + native * 7 + (i ? 97 : 0)) & 255));
                    const char *role = i ? "right" : "left";
                    CHECK(meta.at(std::string(role) + ".native_sequence") == std::to_string(native));
                    CHECK(exclusions[i] == std::stoull(meta.at(std::string("startup_unmatched_") + role)) +
                        std::stoull(meta.at(std::string("steady_state_unmatched_") + role)));
                }
            }
            // Exclusions made after the last published pair stay explicitly in
            // final diagnostics; retained pending observations are terminal tails.
            account_exclusions(nlohmann::json::parse(metrics.at("unpublished_pairing_exclusions")));
            for (size_t i = 0; i < 2; ++i) {
                const char *role = i ? "right" : "left";
                auto received = std::stoull(metrics.at(std::string(role) + ".frames"));
                auto tail = std::stoull(metrics.at(std::string("shutdown_unmatched_") + role));
                CHECK(received == live.size() + exclusions[i] + tail);
                CHECK(exclusions[i] == std::stoull(metrics.at(std::string("startup_unmatched_") + role)) +
                    std::stoull(metrics.at(std::string("steady_state_unmatched_") + role)));
                CHECK(accounted[i].size() == received - tail);
                for (uint64_t native = 0; native < received - tail; ++native) CHECK(accounted[i].contains(native));
                CHECK(metrics.at(std::string(role) + ".sequence_gaps") == "0" && metrics.at(std::string(role) + ".capture_errors") == "0");
            }
            if (scenario == "phase-drift-left") CHECK(std::stoull(metrics.at("steady_state_unmatched_left")) > 0);
            if (scenario == "phase-drift-right") CHECK(std::stoull(metrics.at("steady_state_unmatched_right")) > 0);
            if (scenario == "phase-jitter-left" || scenario == "phase-jitter-right") {
                const std::string role = scenario == "phase-jitter-left" ? "left." : "right.";
                CHECK(metrics.at(role + "observed_max_period_ns") == "12384000");
                CHECK(metrics.at(role + "observed_period_ns") == "8384000");
                CHECK(metrics.at(role + "observed_half_period_ns") == "4192000");
                CHECK(live[2]->header.metadata.at(role + "observed_half_period_ns") == "6192000");
                CHECK(metrics.at("max_v4l2_delta_ns") == "5000000");
            }
            CHECK(store->finalize(id).state == artifact::ArtifactState::finalized);
            unsetenv("MANTIS_X1_PROFILE"); unsetenv("MANTIS_X1_FAKE");
            for (unsigned pass = 0; pass < 2; ++pass) {
                auto replay = device::recorded_source(store, id, false); CHECK(replay->start());
                for (const auto &original : live) {
                    auto result = replay->next(); CHECK(result && *result);
                    CHECK(canonical(**result) == canonical(*original));
                }
                auto end = replay->next(); CHECK(end && !*end); CHECK(replay->stop());
            }
            setenv("MANTIS_X1_PROFILE", (root / "profile.json").c_str(), 1);
            std::cout << format << ' ' << name << " counted exclusions, zero recorder loss and exact replay passed\n";
        }
        CHECK(selected == (argc == 2 ? 56u : 1u));
        unsetenv("MANTIS_X1_PROFILE"); unsetenv("MANTIS_X1_FAKE");
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
