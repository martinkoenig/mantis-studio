#include "../../src/artifact-store/segments.hpp"
#include "../storage/faults.hpp"
#include <cerrno>
#include <condition_variable>
#include <cstdlib>
#include <fstream>
#include <future>
#include <iostream>
#include <mantis/data_io.hpp>
#include <mantis/device_runtime.hpp>
#include <mantis/image_layout.hpp>
#include <mantis/replay.hpp>
#include <nlohmann/json.hpp>
#include <optional>
#include <sstream>

namespace {
using namespace mantis;
using namespace std::chrono_literals;
namespace st = artifact::storage_test;
namespace segments = artifact::segments;
#define CHECK(...)                                                                                           \
    do {                                                                                                     \
        if (!(__VA_ARGS__))                                                                                  \
            throw std::runtime_error("X1 Store failure: " #__VA_ARGS__);                                     \
    } while (false)
constexpr std::string_view store_cause = "RawCapture sequential write failed (disk full?)";
constexpr std::string_view cleanup_cause = "fixture: secondary X1 stop failure after real stop";
constexpr int64_t period = 1000000000 / 120;

// All waits are finite, including assertion-unwind paths. These gates only
// control the test adapter/callback; pairing and storage use production sources.
class Gate {
    std::mutex mutex_;
    std::condition_variable cv_;
    bool open_{};

  public:
    void open() {
        std::lock_guard lock(mutex_);
        open_ = true;
        cv_.notify_all();
    }
    bool wait() {
        std::unique_lock lock(mutex_);
        return cv_.wait_for(lock, 3s, [&] { return open_; });
    }
};
class Trace final : public device::ImageStream {
    device::ImageStream &source_;
    size_t limit_;
    bool cleanup_fault_;

  public:
    Gate release;
    std::vector<data::Published> acquired; // Read only after producer joins.
    Trace(device::ImageStream &source, size_t limit, bool cleanup_fault)
        : source_(source), limit_(limit), cleanup_fault_(cleanup_fault) {}
    const device::Descriptor &descriptor() const override {
        return source_.descriptor();
    }
    Result<void> start() override {
        return source_.start();
    }
    Result<data::Published> next() override {
        if (acquired.size() == limit_) {
            if (!release.wait())
                return std::unexpected(Error{Status::io, "fixture: trace release deadline", "fixture"});
            return data::Published{};
        }
        auto result = source_.next();
        if (result && *result)
            acquired.push_back(*result);
        return result;
    }
    Result<void> stop() override {
        auto result = source_.stop();
        if (!result || !cleanup_fault_)
            return result;
        return std::unexpected(Error{Status::io, std::string(cleanup_cause), "fixture"});
    }
    bool source_paced() const override {
        return source_.source_paced();
    }
    data::Metadata diagnostics() const override {
        return source_.diagnostics();
    }
};
std::string encoded(const data::Packet &packet) {
    std::ostringstream out(std::ios::binary);
    data::write_packet(out, packet);
    return out.str();
}
// Independent native fixture oracle: startup-left excludes LEFT 0..5, then
// pairs LEFT n+6 with RIGHT n. Neither packet serialization nor replay defines
// these expected facts.
void native_prefix(const data::Packet &packet, size_t n) {
    CHECK(packet.type == schema::frameset && packet.frames.size() == 2);
    CHECK(packet.header.sequence.value == n);
    const auto &meta = packet.header.metadata;
    CHECK(meta.at("pairing_mode") == "timestamp-nearest");
    CHECK(meta.at("pairing_policy_version") == "2");
    CHECK(meta.at("native_sequence_offset") == "6");
    CHECK(meta.at("startup_unmatched_left") == "6" && meta.at("startup_unmatched_right") == "0");
    CHECK(meta.at("steady_state_unmatched_left") == "0" && meta.at("steady_state_unmatched_right") == "0");
    CHECK(meta.at("paired_v4l2_delta_ns") == "1800000");
    CHECK(meta.at("sync_quality") == "software" && meta.at("exposure_skew") == "unavailable");
    const auto exclusions = nlohmann::json::parse(meta.at("pairing_exclusions"));
    CHECK(exclusions.size() == (n == 0 ? 6u : 0u));
    for (size_t i = 0; i < exclusions.size(); ++i) {
        const auto &excluded = exclusions[i];
        CHECK(excluded.at("role") == "left" && excluded.at("phase") == "startup");
        CHECK(excluded.at("native_sequence") == i);
        CHECK(excluded.at("timestamp_ns") == int64_t(i) * period);
        CHECK(excluded.at("clock") == "org.mantis.fake.monotonic");
    }
    for (size_t i = 0; i < 2; ++i) {
        const auto &frame = *packet.frames[i];
        const auto native = n + (i ? 0 : 6);
        const auto timestamp = int64_t(n + 6) * period + (i ? 1800000 : 0);
        const char *role = i ? "right" : "left";
        CHECK(frame.header.sequence.value == native);
        CHECK(meta.at(std::string(role) + ".native_sequence") == std::to_string(native));
        CHECK(frame.header.metadata.at("role") == role);
        CHECK(frame.header.timestamp.nanoseconds == timestamp);
        CHECK(frame.header.timestamp.domain.id.value == "org.mantis.fake.monotonic");
        CHECK(frame.header.received.nanoseconds == timestamp + 1000 + (i ? 100 : 0));
        CHECK(frame.header.calibration.id.value == "pairing.store-fault.calibration");
        CHECK(frame.header.calibration.revision == 9);
        CHECK(frame.header.sync.trigger == n && frame.header.sync_quality == time::SyncQuality::software);
        CHECK(data::image_layout(frame).packing == data::ImagePacking::raw8);
        auto bytes = frame.attributes.at(0).buffer.map_read();
        CHECK(bytes && bytes->size() == 3072);
        for (size_t j = 0; j < bytes->size(); ++j)
            CHECK((*bytes)[j] == static_cast<std::byte>((j + native * 7 + (i ? 97 : 0)) & 255));
    }
}
void replay_twice(const std::shared_ptr<artifact::Store> &store, const Id &id,
                  const std::vector<data::Published> &prefix) {
    for (unsigned pass = 0; pass < 2; ++pass) {
        auto replay = device::recorded_source(store, id, false);
        CHECK(replay->start());
        for (size_t n = 0; n < prefix.size(); ++n) {
            auto result = replay->next();
            CHECK(result && *result);
            native_prefix(**result, n);
            CHECK(encoded(**result) == encoded(*prefix[n]));
        }
        auto end = replay->next();
        CHECK(end && !*end);
        CHECK(replay->stop());
    }
}
void rejects_replay(const std::shared_ptr<artifact::Store> &store, const Id &id) {
    bool rejected{};
    try {
        artifact::CaptureReader reader(store, id);
    } catch (const Failure &failure) {
        CHECK(failure.error.code == Status::incompatible);
        CHECK(failure.error.message == "CaptureReader requires finalized supported RawCapture");
        rejected = true;
    }
    CHECK(rejected);
}
} // namespace

void x1_store_failure(const std::filesystem::path &plugins, const std::filesystem::path &root) {
    using namespace mantis;
    const auto profile_path = root / "profile.json";
    nlohmann::json profile{{"format_version", 1},
                           {"measurement_cameras",
                            {{"left", {{"sensor_identity", "ov9281 18-0060"}}},
                             {"right", {{"sensor_identity", "ov9281 20-0060"}}}}},
                           {"mode", {{"width", 64}, {"height", 48}, {"fourcc", "GREY"}, {"fps", 120}}},
                           {"max_v4l2_delta_ns", 5000000},
                           {"stall_timeout_ms", 1000},
                           {"calibration_id", "pairing.store-fault.calibration"},
                           {"calibration_revision", 9}};
    {
        std::ofstream out(profile_path);
        out << profile;
        CHECK(out.good());
    }
    CHECK(setenv("MANTIS_X1_PROFILE", profile_path.c_str(), 1) == 0);
    CHECK(setenv("MANTIS_X1_FAKE", "startup-left", 1) == 0);
    CHECK(setenv("MANTIS_X1_FAKE_PACE", "1", 1) == 0);
    struct Environment {
        ~Environment() {
            unsetenv("MANTIS_X1_PROFILE");
            unsetenv("MANTIS_X1_FAKE");
            unsetenv("MANTIS_X1_FAKE_PACE");
        }
    } environment;
    plugins::Registry registry(plugins.parent_path() / "bin/mantis-plugin-host", root / "hosts", {});
    registry.discover(plugins, {"org.mantis.x1"});
    auto devices = registry.devices();
    CHECK(devices.size() == 1);
    const auto identity = devices[0]->descriptor().id;
    CHECK(devices[0]->descriptor().plugin_id == "org.mantis.x1");
    std::string producer_version;
    for (const auto &plugin : registry.statuses())
        if (plugin.manifest.id == "org.mantis.x1")
            producer_version = plugin.manifest.version;
    SemanticVersion version{};
    char first{}, second{};
    std::istringstream parsed(producer_version);
    CHECK(parsed >> version.major >> first >> version.minor >> second >> version.patch);
    CHECK(first == '.' && second == '.');
    auto components = nlohmann::json::array();
    for (const auto &child : devices[0]->components())
        components.push_back({{"id", child.id.value},
                              {"name", child.name},
                              {"capabilities", child.capabilities},
                              {"metadata", child.metadata}});
    CHECK(components.size() == 2);
    auto store = std::make_shared<artifact::Store>(root / "paired.mantis");
    std::optional<Id> failed_id;
    std::vector<data::Published> proven_prefix;
    for (bool inject : {true, false}) {
        const std::string marker = inject ? "failed-prefix" : "next-capture";
        const auto capture = Id::random();
        artifact::Provenance provenance;
        provenance.producer = devices[0]->descriptor().plugin_id;
        provenance.version = version;
        provenance.parameters = devices[0]->descriptor().metadata;
        provenance.parameters["producer_plugin_version"] = producer_version;
        provenance.parameters["logical_device_id"] = identity.value;
        provenance.parameters["capture_id"] = capture.value;
        provenance.parameters["format_version"] = "2";
        provenance.parameters["components"] = components.dump();
        provenance.parameters["capture_marker"] = marker;
        auto id = store->begin({"org.mantis.RawCapture", 2}, provenance);
        Trace source(*devices[0], inject ? 3 : 2, inject);
        Gate writer_release;
        std::promise<void> checkpoint, error_seen;
        auto reached = checkpoint.get_future();
        auto failed = error_seen.get_future();
        std::vector<data::Published> committed;
        data::Published attempted;
        std::vector<LogRecord> logs;
        std::mutex log_mutex;
        std::optional<Error> store_error;
        std::optional<st::Arm> fault;
        std::unique_ptr<device::Session> session;
        // Destruction releases callbacks/producer BEFORE Session joins. Fault,
        // gates, packets, sink and adapter all outlive those joined workers.
        struct Unblock {
            Gate &writer, &producer;
            ~Unblock() {
                writer.open();
                producer.open();
            }
        } unblock{writer_release, source.release};
        session = std::make_unique<device::Session>(
            device::CaptureDescriptor{capture, {identity}, {}}, std::vector<device::ImageStream *>{&source},
            [&](data::Published packet) {
                if (inject && committed.size() == 2) {
                    attempted = packet;
                    checkpoint.set_value();
                    CHECK(writer_release.wait());
                }
                try {
                    store->append(id, *packet); // Actual short ostream write -> Store Failure.
                } catch (const Failure &failure) {
                    store_error = failure.error;
                    throw;
                }
                committed.push_back(packet);
                if (!inject && committed.size() == 2)
                    checkpoint.set_value();
            },
            [&](const LogRecord &record) {
                std::lock_guard lock(log_mutex);
                logs.push_back(record);
                if (record.component == "capture")
                    error_seen.set_value();
            });
        CHECK(reached.wait_for(3s) == std::future_status::ready);
        const auto part = store->root() / "objects" / id.value / "0.segment.part";
        uintmax_t prefix_bytes{}, partial_bytes{};
        if (inject) {
            CHECK(session->committed() == 2);
            CHECK(committed.size() == 2);
            // append flushes complete records, but does NOT fsync this active
            // segment. Independently check the OS-visible prefix, not durability.
            auto scan = segments::scan(part);
            CHECK(scan.records.size() == 2 && !scan.incomplete && scan.corruption.empty());
            for (size_t n = 0; n < 2; ++n) {
                native_prefix(*committed[n], n);
                CHECK(encoded(*segments::packet(scan, n)) == encoded(*committed[n]));
            }
            prefix_bytes = std::filesystem::file_size(part);
            CHECK(scan.complete_bytes == prefix_bytes);
            CHECK(store->get(id).state == artifact::ArtifactState::open && store->get(id).chunks == 0);
            rejects_replay(store, id);
            // Full header and payload, only four of the eight footer bytes.
            // This fails the actual segment ostream completion check.
            CHECK(attempted && attempted->header.sequence.value == 2);
            partial_bytes = 32 + encoded(*attempted).size() + 4;
            fault.emplace(st::Plan{store->root(), id.value, "record-write", st::Action::short_write, 0,
                                   partial_bytes, ENOSPC});
            writer_release.open();
            CHECK(failed.wait_for(3s) == std::future_status::ready);
        }
        const auto stopping = std::chrono::steady_clock::now();
        source.release.open();
        session->stop();
        CHECK(std::chrono::steady_clock::now() - stopping < 3s);
        const auto diag = session->diagnostics();
        CHECK(diag.at("recorder_snapshot_scope") == "final-after-join");
        CHECK(diag.at("recorder_producer_running") == "0" && diag.at("recorder_writer_running") == "0");
        CHECK(diag.at("recorder_queue_occupancy") == "0");
        CHECK(std::stoull(diag.at("recorder_queue_high_water")) <= session->queue_capacity());
        CHECK(std::stoll(diag.at("recorder_cleanup_finished_ns")) >= 0);
        CHECK(session->produced() == (inject ? 3u : 2u));
        CHECK(session->committed() == 2 && committed.size() == 2);
        CHECK(source.acquired.size() == session->produced());
        CHECK(session->saturation() == 0 && session->metrics().dropped == 0);
        CHECK(session->metrics().pushed == session->produced());
        CHECK(session->metrics().popped == session->produced());
        CHECK(store->raw_append_diagnostics(id).at("raw_first_append_complete") == "1");
        CHECK(diag.at("pairing_failures") == "0" && diag.at("pairing_pending_saturation") == "0");
        for (size_t n = 0; n < source.acquired.size(); ++n)
            native_prefix(*source.acquired[n], n);
        for (const char *role : {"left", "right"}) {
            const auto r = std::string(role);
            CHECK(std::stoull(diag.at(r + ".frames")) ==
                  source.acquired.size() + std::stoull(diag.at("startup_unmatched_" + r)) +
                      std::stoull(diag.at("steady_state_unmatched_" + r)) +
                      std::stoull(diag.at("shutdown_unmatched_" + r)));
            CHECK(diag.at(r + ".sequence_gaps") == "0" && diag.at(r + ".capture_errors") == "0");
        }
        auto artifact = store->get(id);
        CHECK(artifact.provenance.producer == "org.mantis.x1");
        CHECK(artifact.provenance.version == version);
        CHECK(artifact.provenance.parameters.at("producer_plugin_version") == producer_version);
        CHECK(artifact.provenance.parameters.at("logical_device_id") == identity.value);
        CHECK(artifact.provenance.parameters.at("capture_id") == capture.value);
        CHECK(artifact.provenance.parameters.at("format_version") == "2");
        CHECK(artifact.provenance.parameters.at("components") == components.dump());
        CHECK(artifact.provenance.parameters.at("capture_marker") == marker);
        const auto observations =
            nlohmann::json::parse(artifact.provenance.parameters.at("initial_observations"));
        CHECK(observations.size() == 2);
        for (size_t i = 0; i < 2; ++i) {
            CHECK(observations[i].at("metadata").at("role") == (i ? "right" : "left"));
            CHECK(observations[i].at("clock_domain").at("id") == "org.mantis.fake.monotonic");
            CHECK(observations[i].at("calibration").at("id") == "pairing.store-fault.calibration");
            CHECK(observations[i].at("calibration").at("revision") == 9);
        }
        if (inject) {
            CHECK(fault->metrics().hits == 1 && fault->metrics().short_results == 1);
            CHECK(fault->metrics().written == partial_bytes && fault->metrics().error == ENOSPC);
            CHECK(store_error && store_error->code == Status::io && store_error->component == "raw-writer");
            CHECK(store_error->message == store_cause);
            CHECK(session->error() == store_cause);
            CHECK(diag.at("recorder_error") == store_cause);
            CHECK(diag.at("recorder_secondary_error") == cleanup_cause);
            CHECK(diag.at("recorder_cleanup_errors") == "1");
            CHECK(session->produced() - session->committed() == 1); // Exactly the attempted partial record.
            CHECK(logs.size() == 2 && logs[0].component == "capture");
            CHECK(logs[0].message == store_cause && logs[1].component == "capture.diagnostics");
            const auto final = nlohmann::json::parse(logs[1].message);
            CHECK(final.at("recorder_error") == store_cause);
            CHECK(final.at("recorder_secondary_error") == cleanup_cause);
            CHECK(artifact.state == artifact::ArtifactState::open && artifact.chunks == 0);
            store->abandon(id);
            CHECK(store->get(id).state == artifact::ArtifactState::recoverable);
            rejects_replay(store, id);
            {
                const auto scan = segments::scan(part);
                CHECK(scan.records.size() == 2 && scan.incomplete && scan.corruption.empty());
                CHECK(scan.complete_bytes == prefix_bytes);
                CHECK(std::filesystem::file_size(part) == prefix_bytes + partial_bytes);
                for (size_t n = 0; n < 2; ++n)
                    CHECK(encoded(*segments::packet(scan, n)) == encoded(*committed[n]));
            }
            // Retire all users of the plan before disarming; reopen metadata
            // before explicit supported recovery, never fabricate finalization.
            session.reset();
            fault.reset();
            store.reset();
            store = std::make_shared<artifact::Store>(root / "paired.mantis");
            CHECK(store->get(id).state == artifact::ArtifactState::recoverable);
            rejects_replay(store, id);
            CHECK(store->recover(id).state == artifact::ArtifactState::finalized);
            CHECK(!std::filesystem::exists(part));
            CHECK(std::filesystem::file_size(store->object_path(id, 0)) == prefix_bytes);
            CHECK(store->record_count(id) == 2);
            failed_id = id;
            proven_prefix = committed;
        } else {
            CHECK(session->error().empty() && !store_error && logs.empty());
            CHECK(diag.at("recorder_secondary_error").empty() && diag.at("recorder_cleanup_errors") == "0");
            CHECK(devices[0]->descriptor().id == identity);
            CHECK(store->finalize(id).state == artifact::ArtifactState::finalized);
            CHECK(failed_id && id != *failed_id && session->descriptor().id == capture);
            CHECK(store->get(*failed_id).provenance.parameters.at("capture_marker") == "failed-prefix");
            replay_twice(store, *failed_id, proven_prefix);
        }
        CHECK(store->get(id).provenance.parameters == artifact.provenance.parameters);
        CHECK(store->get(id).provenance.version == artifact.provenance.version);
        replay_twice(store, id, committed);
        std::cout << "X1 Store " << marker << ": " << nlohmann::json(diag).dump() << '\n';
    }
}
