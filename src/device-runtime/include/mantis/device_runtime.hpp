#pragma once
#include <mantis/plugin_runtime.hpp>
#include <thread>
namespace mantis::device {
class Runtime {
    std::vector<std::unique_ptr<ImageStream>> streams_;

  public:
    explicit Runtime(plugins::Registry &plugins) : streams_(plugins.devices()) {}
    std::vector<Descriptor> list() const;
    ImageStream &find(const Id &);
};
// Session owns capture independently of any client. Recording callback is lossless and backpressured.
class Session {
    CaptureDescriptor descriptor_;
    std::vector<ImageStream *> streams_;
    pipeline::BoundedQueue<data::Published> recorder_{32, pipeline::QueuePolicy::lossless};
    pipeline::BoundedQueue<data::Published> preview_{1, pipeline::QueuePolicy::latest_only};
    std::atomic_int64_t ended_{};
    std::atomic_uint64_t produced_{}, committed_{}, saturation_{}, payload_bytes_{};
    time::MonotonicTimestamp started_{time::MonotonicTimestamp::now()};
    data::Metadata diagnostics_;
    mutable std::mutex mutex_;
    data::Published first_;
    std::string error_;
    std::condition_variable first_ready_;
    std::jthread writer_, producer_;
    std::function<void(data::Published)> record_;
    LogSink logger_;
    std::atomic_bool active_{true};

  public:
    Session(CaptureDescriptor, std::vector<ImageStream *>, std::function<void(data::Published)>, LogSink);
    ~Session();
    Session(const Session &) = delete;
    void stop();
    data::Published first() const;
    const CaptureDescriptor &descriptor() const {
        return descriptor_;
    }
    bool active() const {
        return active_.load();
    }
    std::string error() const;
    data::Metadata diagnostics() const;
    data::Published preview();
    uint64_t committed() const { return committed_.load(); }
    uint64_t produced() const { return produced_.load(); }
    uint64_t saturation() const { return saturation_.load(); }
    uint64_t payload_bytes() const { return payload_bytes_.load(); }
    size_t queue_capacity() const { return recorder_.capacity(); }
    pipeline::QueueMetrics preview_metrics() const { return preview_.metrics(); }
    double duration() const { return double((ended_.load() ? ended_.load() : time::MonotonicTimestamp::now().nanoseconds) - started_.nanoseconds) / 1e9; }
    pipeline::QueueMetrics metrics() const {
        return recorder_.metrics();
    }
};
} // namespace mantis::device
