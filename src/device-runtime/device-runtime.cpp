#include <algorithm>
#include <mantis/device_runtime.hpp>
namespace mantis::device {
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
Session::Session(CaptureDescriptor descriptor, std::vector<ImageStream *> streams,
                 std::function<void(data::Published)> record, LogSink logger)
    : descriptor_(std::move(descriptor)), streams_(std::move(streams)), record_(std::move(record)),
      logger_(std::move(logger)) {
    size_t started = 0;
    try {
        for (auto *stream : streams_) {
            auto r = stream->start();
            if (!r)
                throw Failure(r.error());
            ++started;
        }
    } catch (...) {
        for (size_t i = 0; i < started; ++i)
            (void)streams_[i]->stop();
        throw;
    }
    writer_ = std::jthread([this](std::stop_token) {
        try {
            while (auto frame = recorder_.pop()) {
                record_(*frame);
                ++committed_;
                const auto &packet = **frame;
                for (const auto &a : packet.attributes) payload_bytes_ += a.buffer.size();
                for (const auto &child : packet.frames)
                    for (const auto &a : child->attributes) payload_bytes_ += a.buffer.size();
                std::lock_guard lock(mutex_);
                if (!first_)
                    first_ = *frame;
                first_ready_.notify_all();
            }
        } catch (const std::exception &e) {
            {
                std::lock_guard lock(mutex_);
                error_ = e.what();
            }
            active_ = false;
            recorder_.close();
            first_ready_.notify_all();
            if (logger_)
                logger_({"error", "capture", e.what()});
        }
    });
    producer_ = std::jthread([this](std::stop_token stop) {
        try {
            auto next = std::chrono::steady_clock::now();
            while (!stop.stop_requested() && active_) {
                for (auto *stream : streams_) {
                    auto f = stream->next();
                    if (!f) {
                        { std::lock_guard lock(mutex_); diagnostics_ = stream->diagnostics(); }
                        throw Failure(f.error());
                    }
                    {
                        std::lock_guard lock(mutex_); diagnostics_ = stream->diagnostics();
                    }
                    if (!*f) continue;
                    ++produced_;
                    preview_.push(*f);
                    if (!recorder_.push_for(*f, std::chrono::milliseconds(50), stop)) {
                        if (!stop.stop_requested() && active_) {
                            ++saturation_;
                            fail(Status::io, "LOSSLESS writer queue saturated; capture failed explicitly");
                        }
                        break;
                    }
                }
                if (!streams_.front()->source_paced()) {
                    next += std::chrono::milliseconds(33);
                    std::this_thread::sleep_until(next);
                }
            }
        } catch (const std::exception &e) {
            {
                std::lock_guard lock(mutex_);
                error_ = e.what();
            }
            if (logger_)
                logger_({"error", "capture", e.what()});
        }
        active_ = false;
        recorder_.close();
        first_ready_.notify_all();
    });
    std::unique_lock lock(mutex_);
    if (!first_ready_.wait_for(lock, std::chrono::seconds(5), [this] { return first_ || !error_.empty(); })) {
        lock.unlock();
        stop();
        fail(Status::io, "Device did not provide an initial frame");
    }
    if (!error_.empty()) {
        auto message = error_;
        lock.unlock();
        stop();
        fail(Status::io, message);
    }
}
Session::~Session() {
    stop();
}
void Session::stop() {
    active_ = false;
    producer_.request_stop();
    if (producer_.joinable())
        producer_.join();
    recorder_.close();
    if (writer_.joinable())
        writer_.join();
    if (!ended_.load()) ended_ = time::MonotonicTimestamp::now().nanoseconds;
    preview_.close();
    for (auto *stream : streams_)
        (void)stream->stop();
    streams_.clear();
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
    std::lock_guard lock(mutex_); return diagnostics_;
}
data::Published Session::preview() {
    auto packet = preview_.try_pop(); return packet ? *packet : data::Published{};
}
} // namespace mantis::device
