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
        auto artifact = store_->get(artifact_);
        if (artifact.type.name != "org.mantis.RawCapture" ||
            (artifact.type.schema_version != 1 && artifact.type.schema_version != 2))
            fail(Status::incompatible, "Image-only recorded_source requires RawCapture 1/2");
        descriptor_.id = {"recorded:" + artifact_.value}; descriptor_.name = "Recorded Scanner";
        descriptor_.plugin_id = "org.mantis.recorded-source";
        descriptor_.capabilities = {std::string(store_->get(artifact_).type.schema_version == 2 ? frameset_stream : image_stream)};
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

namespace mantis::device {
struct BundleReplay::Impl {
    artifact::BundleCaptureReader reader;
    bool paced;
    Clock now;
    CancellationToken cancellation;
    std::atomic_bool ended{false};
    std::mutex call, mutex;
    std::condition_variable wake;
    std::optional<data::AcquisitionBundle> pending;
    std::optional<int64_t> first;
    int64_t previous{};
    std::chrono::steady_clock::time_point started;
    Impl(std::shared_ptr<const artifact::Store> store, Id id, bool p, Clock c)
        : reader(std::move(store), std::move(id)), paced(p),
          now(c ? std::move(c) : Clock{[] { return std::chrono::steady_clock::now(); }}), started(now()) {}
};
BundleReplay::BundleReplay(std::shared_ptr<const artifact::Store> s, Id id, bool p, Clock c)
    : impl_(std::make_unique<Impl>(std::move(s), std::move(id), p, std::move(c))) {}
BundleReplay::~BundleReplay() = default;
const data::ProjectedCaptureHeader &BundleReplay::header() const { return impl_->reader.header(); }
const std::optional<data::ProjectedRunOutcome> &BundleReplay::final_outcome() const {
    return impl_->reader.final_outcome();
}
bool BundleReplay::finished() const { return impl_->ended.load(); }
void BundleReplay::stop() {
    impl_->cancellation.cancel();
    impl_->ended = true;
    impl_->wake.notify_all();
}
void BundleReplay::notify_clock_advanced() { impl_->wake.notify_all(); }
Result<std::optional<data::AcquisitionBundle>> BundleReplay::next(uint32_t timeout_ms) {
    try {
        if (timeout_ms > 60000)
            fail(Status::invalid_argument, "Bundle replay timeout exceeds 60 seconds");
        auto &r = *impl_;
        std::unique_lock call(r.call, std::try_to_lock);
        if (!call.owns_lock())
            fail(Status::busy, "Bundle replay next already pending");
        std::unique_lock lock(r.mutex);
        r.cancellation.check();
        if (r.ended)
            return std::optional<data::AcquisitionBundle>{};
        if (!r.pending)
            r.pending = r.reader.next(r.cancellation);
        if (!r.pending) {
            r.ended = true;
            return std::optional<data::AcquisitionBundle>{};
        }
        if (r.paced) {
            auto time = r.pending->published.time.nanoseconds;
            if (!r.first) {
                r.first = time;
                r.previous = time;
                r.started = r.now();
            }
            // Unsigned subtraction avoids signed overflow for extreme stored timestamps.
            const auto delta = static_cast<uint64_t>(time) - static_cast<uint64_t>(*r.first);
            if (time < r.previous || delta > uint64_t(86400) * 1000000000)
                fail(Status::corrupt, "Bundle publication pacing discontinuity");
            auto offset = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::nanoseconds(static_cast<int64_t>(delta)));
            if (r.started.time_since_epoch() > std::chrono::steady_clock::duration::max() - offset)
                fail(Status::corrupt, "Bundle replay deadline overflow");
            auto due = r.started + offset;
            auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
            for (;;) {
                auto now = r.now();
                if (now < r.started)
                    fail(Status::corrupt, "Replay clock regressed");
                if (now >= due)
                    break;
                r.cancellation.check();
                auto wall = std::chrono::steady_clock::now();
                if (wall >= deadline)
                    return std::optional<data::AcquisitionBundle>{};
                auto wait = std::min({std::chrono::duration_cast<std::chrono::nanoseconds>(deadline - wall),
                                      due - now, std::chrono::nanoseconds{50000000}});
                r.wake.wait_for(lock, wait); // stop/fake-clock advance wakes immediately
            }
            r.previous = time;
        }
        r.cancellation.check();
        return std::exchange(r.pending, {});
    } catch (const Failure &e) {
        return std::unexpected(e.error);
    } catch (const std::exception &e) {
        return std::unexpected(Error{Status::io, e.what(), "bundle-replay"});
    }
}
} // namespace mantis::device
