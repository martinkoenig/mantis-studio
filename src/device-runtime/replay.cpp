#include <condition_variable>
#include <mantis/replay.hpp>
#include <mutex>
#include <utility>
namespace mantis::device {
namespace {
class RecordedSource final : public ImageStream {
    std::shared_ptr<const artifact::Store> store_;
    Id artifact_;
    Descriptor descriptor_;
    bool real_time_, running_{};
    std::unique_ptr<artifact::CaptureReader> reader_;
    data::Published pending_;
    std::optional<int64_t> first_time_;
    int64_t previous_time_{};
    std::chrono::steady_clock::time_point started_;
  public:
    RecordedSource(std::shared_ptr<const artifact::Store> store, Id id, bool realtime)
        : store_(std::move(store)), artifact_(std::move(id)), real_time_(realtime) {
        descriptor_.id = {"recorded:" + artifact_.value}; descriptor_.name = "Recorded Scanner";
        descriptor_.plugin_id = "org.mantis.recorded-source";
        descriptor_.capabilities = {std::string(frameset_stream)};
    }
    const Descriptor &descriptor() const override { return descriptor_; }
    bool finished() const override { return !running_; }
    bool source_paced() const override { return true; }
    Result<void> start() override {
        try {
            reader_ = std::make_unique<artifact::CaptureReader>(store_, artifact_);
            first_time_.reset(); pending_.reset(); running_ = true; started_ = std::chrono::steady_clock::now();
            return {};
        } catch (const Failure &e) { return std::unexpected(e.error); }
    }
    Result<void> stop() override { running_ = false; pending_.reset(); reader_.reset(); return {}; }
    Result<data::Published> next() override {
        try {
            if (!running_) fail(Status::cancelled, "Recorded source stopped");
            if (!pending_) pending_ = reader_->next();
            if (!pending_) { running_ = false; return data::Published{}; }
            if (real_time_) {
                auto time = pending_->header.received.nanoseconds;
                if (!first_time_) { first_time_ = time; previous_time_ = time; }
                if (time < previous_time_ || time - *first_time_ > int64_t(86400) * 1000000000)
                    fail(Status::corrupt, "Replay receive clock discontinuity");
                auto due = started_ + std::chrono::nanoseconds(time - *first_time_);
                if (due > std::chrono::steady_clock::now()) {
                    std::mutex mutex; std::condition_variable ready; std::unique_lock lock(mutex);
                    ready.wait_until(lock, std::min(due, std::chrono::steady_clock::now() + std::chrono::milliseconds(50)));
                    if (std::chrono::steady_clock::now() < due) return data::Published{};
                }
                previous_time_ = time;
            }
            return std::exchange(pending_, {});
        } catch (const Failure &e) { return std::unexpected(e.error); }
        catch (const std::exception &e) { return std::unexpected(Error{Status::io, e.what(), "replay"}); }
    }
};
}
std::unique_ptr<ImageStream> recorded_source(std::shared_ptr<const artifact::Store> s, Id id, bool realtime) {
    return std::make_unique<RecordedSource>(std::move(s), std::move(id), realtime);
}
} // namespace mantis::device
