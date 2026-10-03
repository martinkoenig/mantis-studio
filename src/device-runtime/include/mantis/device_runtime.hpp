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
    pipeline::BoundedQueue<data::Published> recorder_{8, pipeline::QueuePolicy::lossless};
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
    pipeline::QueueMetrics metrics() const {
        return recorder_.metrics();
    }
};
} // namespace mantis::device
