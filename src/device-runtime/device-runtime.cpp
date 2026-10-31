#include <algorithm>
#include <cstdio>
#include <exception>
#include <mantis/device_runtime.hpp>
#include <nlohmann/json.hpp>
namespace mantis::device {
namespace {
std::string bounded_text(std::string_view value, size_t limit) {
    auto size = std::min(value.size(), limit);
    while (size && size < value.size() && (static_cast<unsigned char>(value[size]) & 0xc0) == 0x80)
        --size;
    return std::string(value.substr(0, size));
}
} // namespace
std::vector<Descriptor> Runtime::list() const {
    std::vector<Descriptor> out;
    for (auto &p : streams_) {
        out.push_back(p->descriptor());
        for (auto &child : p->components()) out.push_back(child);
    }
    return out;
}
ImageStream &Runtime::find(const Id &id) {
    for (auto &s : streams_)
        if (s->descriptor().id == id)
            return *s;
    fail(Status::not_found, "Device not found");
}
int64_t Session::elapsed_ns() const {
    return time::MonotonicTimestamp::now().nanoseconds - started_.nanoseconds;
}
struct Session::WorkerMark {
    Session &owner;
    bool writer;
    WorkerMark(Session &session, bool is_writer) : owner(session), writer(is_writer) {
        std::lock_guard lock(owner.mutex_);
        if (writer) {
            owner.timing_.writer_started = owner.elapsed_ns();
            owner.timing_.writer_running = true;
        } else {
            owner.timing_.producer_started = owner.elapsed_ns();
            owner.timing_.producer_running = true;
        }
    }
    ~WorkerMark() {
        std::lock_guard lock(owner.mutex_);
        (writer ? owner.timing_.writer_running : owner.timing_.producer_running) = false;
    }
};
void Session::source_diagnostics(ImageStream &stream, bool acquired) noexcept {
    const auto observed = acquired ? elapsed_ns() : -1;
    std::optional<data::Metadata> snapshot;
    try {
        snapshot = stream.diagnostics();
    } catch (...) {
        // Diagnostics must not replace a source error or fail a healthy frame.
    }
    std::lock_guard lock(mutex_);
    if (snapshot)
        diagnostics_ = std::move(*snapshot);
    else
        ++timing_.diagnostic_errors;
    if (acquired) {
        ++produced_;
        if (timing_.first_acquired < 0)
            timing_.first_acquired = observed;
        timing_.last_acquired = observed;
    }
}
void Session::report_failure(bool final) noexcept {
    try {
        LogRecord record{"error", "capture", {}};
        if (final) {
            record.component = "capture.diagnostics";
            auto bounded = snapshot(false);
            for (auto &[key, value] : bounded)
                value = bounded_text(value, key == "recorder_error"             ? 1024
                                            : key == "recorder_secondary_error" ? 512
                                                                                : 128);
            size_t truncated{};
            {
                std::lock_guard lock(mutex_);
                // Preserve the established correspondence-failure evidence,
                // without exporting arbitrary source keys or unbounded strings.
                for (const auto *key : {"pairing_mode",
                                        "pairing_policy_version",
                                        "pairing_state",
                                        "max_v4l2_delta_ns",
                                        "pairing_failures",
                                        "pairing_pending_saturation",
                                        "pairing_pending_capacity_per_camera",
                                        "pairing_candidate_sequence_left",
                                        "pairing_candidate_sequence_right",
                                        "pairing_candidate_timestamp_ns_left",
                                        "pairing_candidate_timestamp_ns_right",
                                        "pairing_lookahead_timestamp_ns_left",
                                        "pairing_lookahead_timestamp_ns_right",
                                        "pairing_candidate_distance_ns",
                                        "pairing_lookahead_distance_ns",
                                        "pairing_nearest_candidate_distance_ns",
                                        "pairing_candidates_bracketed",
                                        "left.observed_period_ns",
                                        "right.observed_period_ns",
                                        "left.observed_max_period_ns",
                                        "right.observed_max_period_ns",
                                        "left.target_fps",
                                        "right.target_fps",
                                        "startup_unmatched_left",
                                        "startup_unmatched_right",
                                        "steady_state_unmatched_left",
                                        "steady_state_unmatched_right",
                                        "shutdown_unmatched_left",
                                        "shutdown_unmatched_right",
                                        "pending_high_water_left",
                                        "pending_high_water_right",
                                        "paired_v4l2_delta_ns",
                                        "host_arrival_delta_ns",
                                        "sync_quality",
                                        "exposure_skew"})
                    if (auto item = diagnostics_.find(key); item != diagnostics_.end()) {
                        bounded[key] = bounded_text(item->second, 32);
                        truncated += item->second.size() > 32;
                    }
            }
            bounded["recorder_source_evidence_scope"] = "pairing-scalar-allowlist";
            bounded["recorder_source_values_truncated"] = std::to_string(truncated);
            record.message =
                nlohmann::json(bounded).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
            if (record.message.size() > 16384) {
                std::string cause;
                {
                    std::lock_guard lock(mutex_);
                    cause = bounded_text(error_, 512);
                }
                record.message =
                    nlohmann::json({{"recorder_error", cause}, {"recorder_evidence_truncated", "1"}})
                        .dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
            }
        } else {
            std::lock_guard lock(mutex_);
            record.message = bounded_text(error_, 1024);
        }
        try {
            if (logger_) {
                logger_(record);
                return;
            }
        } catch (...) {
            // A throwing diagnostic sink must never escape a worker or cleanup.
        }
        // Also preserve constructor failures without a sink/returned Session.
        const auto escaped = final ? std::string{}
                                   : nlohmann::json(record.message)
                                         .dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        std::fprintf(stderr, "%s: %s\n", record.component.c_str(),
                     (final ? record.message : escaped).c_str());
    } catch (...) {
        // Preserve the original capture exception even if evidence allocation fails.
    }
}
bool Session::latch_error(std::string_view message) {
    if (error_.empty()) {
        error_ = message;
        timing_.first_error = elapsed_ns();
        return true;
    }
    if (error_ != message && secondary_error_.empty()) {
        secondary_error_ = bounded_text(message, 512);
        timing_.secondary_error = elapsed_ns();
        timing_.secondary_error_truncated = message.size() > 512;
    }
    return false;
}
void Session::failed(const char *message) {
    bool first_error{};
    {
        std::lock_guard lock(mutex_);
        first_error = latch_error(message);
    }
    active_ = false;
    recorder_.close();
    first_ready_.notify_all();
    if (first_error)
        report_failure(false);
}
Session::Session(CaptureDescriptor descriptor, std::vector<ImageStream *> streams,
                 std::function<void(data::Published)> record, LogSink logger)
    : descriptor_(std::move(descriptor)), streams_(std::move(streams)), record_(std::move(record)),
      logger_(std::move(logger)) {
    try {
        if (streams_.empty())
            fail(Status::invalid_argument, "Capture requires at least one stream");
        for (auto *stream : streams_) {
            ++attempted_streams_;
            auto result = stream->start();
            if (!result)
                throw Failure(result.error());
        }
        timing_.source_paced = streams_.front()->source_paced();
        writer_ = std::jthread([this](std::stop_token) {
            WorkerMark worker(*this, true);
            bool first_callback = true;
            try {
                while (auto frame = recorder_.pop()) {
                    const auto began = elapsed_ns();
                    if (first_callback) {
                        std::lock_guard lock(mutex_);
                        timing_.first_callback = began;
                    }
                    try {
                        record_(*frame);
                    } catch (...) {
                        std::lock_guard lock(mutex_);
                        const auto duration = elapsed_ns() - began;
                        if (first_callback)
                            timing_.first_append = duration;
                        timing_.max_append = std::max(timing_.max_append, duration);
                        throw;
                    }
                    const auto finished = elapsed_ns();
                    uint64_t payload{};
                    const auto &packet = **frame;
                    for (const auto &attribute : packet.attributes)
                        payload += attribute.buffer.size();
                    for (const auto &child : packet.frames)
                        for (const auto &attribute : child->attributes)
                            payload += attribute.buffer.size();
                    {
                        std::lock_guard lock(mutex_);
                        ++committed_;
                        payload_bytes_ += payload;
                        if (first_callback) {
                            timing_.first_append = finished - began;
                            timing_.first_commit = elapsed_ns();
                            first_callback = false;
                        }
                        timing_.max_append = std::max(timing_.max_append, finished - began);
                        if (!first_)
                            first_ = *frame;
                        first_ready_.notify_all();
                    }
                }
            } catch (const std::exception &error) {
                failed(error.what());
            } catch (...) {
                failed("Non-standard recorder exception");
            }
        });
        producer_ = std::jthread([this](std::stop_token stop) {
            WorkerMark worker(*this, false);
            try {
                auto next = std::chrono::steady_clock::now();
                while (!stop.stop_requested() && active_) {
                    for (auto *stream : streams_) {
                        auto frame = stream->next();
                        source_diagnostics(*stream, frame && static_cast<bool>(*frame));
                        if (!frame)
                            throw Failure(frame.error());
                        if (!*frame)
                            continue;
                        preview_.push(*frame);
                        // Finish recording an observation already acquired when stop
                        // arrives: cancelling it would silently lose the last FrameSet.
                        if (!recorder_.push_for(*frame, std::chrono::milliseconds(50))) {
                            {
                                std::lock_guard lock(mutex_);
                                if (!error_.empty())
                                    fail(Status::io, error_);
                            }
                            ++saturation_;
                            fail(Status::io, "LOSSLESS writer queue saturated; capture failed explicitly");
                        }
                    }
                    if (!streams_.front()->source_paced()) {
                        next += std::chrono::milliseconds(33);
                        std::this_thread::sleep_until(next);
                    }
                }
            } catch (const std::exception &error) {
                failed(error.what());
            } catch (...) {
                failed("Non-standard acquisition exception");
            }
            active_ = false;
            recorder_.close();
            first_ready_.notify_all();
        });
        std::unique_lock lock(mutex_);
        if (!first_ready_.wait_for(lock, std::chrono::seconds(5),
                                   [this] { return first_ || !error_.empty(); })) {
            lock.unlock();
            failed("Device did not provide an initial frame");
            fail(Status::io, "Device did not provide an initial frame");
        }
        if (!error_.empty())
            fail(Status::io, error_);
    } catch (...) {
        // Includes source/thread construction and first-commit failures. All
        // callbacks/members stay alive until both workers have retired.
        const auto original = std::current_exception();
        try {
            try {
                std::rethrow_exception(original);
            } catch (const std::exception &error) {
                failed(error.what());
            } catch (...) {
                failed("Non-standard capture startup exception");
            }
        } catch (...) {
            // Failure-state evidence allocation cannot replace the exception.
        }
        stop();
        std::rethrow_exception(original);
    }
}
Session::~Session() {
    stop();
}
void Session::stop() {
    {
        std::lock_guard lock(mutex_);
        if (timing_.cleanup_finished >= 0)
            return;
        timing_.cleanup_started = elapsed_ns();
    }
    active_ = false;
    producer_.request_stop();
    if (producer_.joinable())
        producer_.join();
    recorder_.close();
    if (writer_.joinable())
        writer_.join();
    if (!ended_.load())
        ended_ = time::MonotonicTimestamp::now().nanoseconds;
    preview_.close();
    for (size_t i = 0; i < attempted_streams_; ++i) {
        try {
            const auto result = streams_[i]->stop();
            if (!result) {
                std::lock_guard lock(mutex_);
                ++timing_.cleanup_errors;
                latch_error(result.error().message);
            }
        } catch (const std::exception &error) {
            std::lock_guard lock(mutex_);
            ++timing_.cleanup_errors;
            latch_error(error.what());
        } catch (...) {
            std::lock_guard lock(mutex_);
            ++timing_.cleanup_errors;
            latch_error("Non-standard source cleanup exception");
        }
        source_diagnostics(*streams_[i]);
    }
    streams_.clear();
    bool report{};
    {
        std::lock_guard lock(mutex_);
        timing_.cleanup_finished = elapsed_ns();
        report = !error_.empty() && !failure_reported_;
        failure_reported_ = failure_reported_ || report;
    }
    if (report)
        report_failure(true);
}
data::Published Session::first() const {
    std::lock_guard lock(mutex_);
    if (!first_)
        fail(Status::busy, "No captured frames available");
    return first_;
}
std::string Session::error() const {
    std::lock_guard lock(mutex_);
    return error_;
}
data::Metadata Session::diagnostics() const {
    return snapshot(true);
}
data::Metadata Session::snapshot(bool include_source) const {
    std::lock_guard lock(mutex_);
    auto out = include_source ? diagnostics_ : data::Metadata{};
    const auto queue = recorder_.metrics();
    auto field = [&](const char *key, auto value) {
        out[std::string("recorder_") + key] = std::to_string(value);
    };
    field("diagnostic_version", 1);
    field("snapshot_ns", elapsed_ns());
    field("writer_started_ns", timing_.writer_started);
    field("producer_started_ns", timing_.producer_started);
    field("first_acquired_ns", timing_.first_acquired);
    field("last_acquired_ns", timing_.last_acquired);
    field("first_enqueued_ns", queue.first_push_ns < 0 ? -1 : queue.first_push_ns - started_.nanoseconds);
    field("first_callback_ns", timing_.first_callback);
    field("first_commit_ns", timing_.first_commit);
    field("first_append_ns", timing_.first_append);
    field("first_error_ns", timing_.first_error);
    field("secondary_error_ns", timing_.secondary_error);
    field("max_append_ns", timing_.max_append);
    field("produced", produced_.load());
    field("committed", committed_.load());
    field("saturation", saturation_.load());
    field("queue_occupancy", queue.occupancy);
    field("queue_high_water", queue.high_water);
    field("queue_capacity", recorder_.capacity());
    field("queue_full_ns", queue.full_ns);
    field("failed_enqueue_wait_ns", queue.last_failed_wait_ns);
    field("writer_running", timing_.writer_running);
    field("producer_running", timing_.producer_running);
    field("source_paced", timing_.source_paced);
    field("cleanup_started_ns", timing_.cleanup_started);
    field("cleanup_finished_ns", timing_.cleanup_finished);
    field("cleanup_errors", timing_.cleanup_errors);
    field("diagnostic_errors", timing_.diagnostic_errors);
    const auto acquired_duration = timing_.last_acquired - timing_.first_acquired;
    field("observed_frames_per_second",
          timing_.first_acquired < 0 || acquired_duration <= 0
              ? -1.0
              : double(produced_.load() > 0 ? produced_.load() - 1 : 0) * 1e9 / double(acquired_duration));
    out["recorder_capture_id"] = descriptor_.id.value;
    out["recorder_snapshot_scope"] =
        timing_.cleanup_finished >= 0 ? "final-after-join" : "live-observational";
    out["recorder_error"] = bounded_text(error_, 1024);
    out["recorder_secondary_error"] = secondary_error_;
    field("secondary_error_truncated", timing_.secondary_error_truncated);
    field("error_truncated", error_.size() > 1024);
    return out;
}
data::Published Session::preview() {
    auto packet = preview_.try_pop();
    return packet ? *packet : data::Published{};
}
} // namespace mantis::device
