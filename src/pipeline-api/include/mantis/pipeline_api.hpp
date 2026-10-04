#pragma once
#include <condition_variable>
#include <deque>
#include <mantis/data.hpp>
#include <mutex>
#include <stop_token>
namespace mantis::pipeline {
enum class QueuePolicy { block, drop_oldest, drop_newest, latest_only, lossless };
struct QueueMetrics {
    size_t occupancy{}, high_water{};
    uint64_t dropped{}, pushed{}, popped{};
};
template <class T> class BoundedQueue {
    mutable std::mutex mutex_;
    std::condition_variable_any readable_, writable_;
    std::deque<T> items_;
    size_t capacity_;
    QueuePolicy policy_;
    bool closed_{};
    QueueMetrics metrics_;

  public:
    BoundedQueue(size_t capacity, QueuePolicy policy)
        : capacity_(policy == QueuePolicy::latest_only ? 1 : capacity), policy_(policy) {
        if (capacity == 0)
            fail(Status::invalid_argument, "Queue capacity must be positive");
    }
    bool push(T value, std::stop_token stop = {}) {
        std::unique_lock lock(mutex_);
        if (closed_)
            return false;
        if (policy_ == QueuePolicy::block || policy_ == QueuePolicy::lossless) {
            if (!writable_.wait(lock, stop, [&] { return closed_ || items_.size() < capacity_; }) || closed_)
                return false;
        }
        if (items_.size() == capacity_) {
            ++metrics_.dropped;
            if (policy_ == QueuePolicy::drop_newest)
                return true;
            items_.pop_front();
        }
        items_.push_back(std::move(value));
        ++metrics_.pushed;
        metrics_.high_water = std::max(metrics_.high_water, items_.size());
        readable_.notify_one();
        return true;
    }
    std::optional<T> pop(std::stop_token stop = {}) {
        std::unique_lock lock(mutex_);
        if (!readable_.wait(lock, stop, [&] { return closed_ || !items_.empty(); }) || items_.empty())
            return {};
        auto v = std::move(items_.front());
        items_.pop_front();
        ++metrics_.popped;
        writable_.notify_one();
        return v;
    }
    bool push_for(T value, std::chrono::milliseconds timeout, std::stop_token stop = {}) {
        if (policy_ != QueuePolicy::lossless && policy_ != QueuePolicy::block)
            return push(std::move(value), stop);
        std::unique_lock lock(mutex_);
        if (!writable_.wait_for(lock, stop, timeout, [&] { return closed_ || items_.size() < capacity_; }) || closed_)
            return false;
        items_.push_back(std::move(value)); ++metrics_.pushed;
        metrics_.high_water = std::max(metrics_.high_water, items_.size()); readable_.notify_one(); return true;
    }
    std::optional<T> try_pop() {
        std::lock_guard lock(mutex_);
        if (items_.empty()) return {};
        auto value = std::move(items_.front()); items_.pop_front(); ++metrics_.popped; writable_.notify_one(); return value;
    }
    size_t capacity() const { return capacity_; }
    void close() {
        std::lock_guard lock(mutex_);
        closed_ = true;
        readable_.notify_all();
        writable_.notify_all();
    }
    QueueMetrics metrics() const {
        std::lock_guard lock(mutex_);
        auto m = metrics_;
        m.occupancy = items_.size();
        return m;
    }
};
struct InputPort {
    std::string name;
    schema::DataTypeId type;
};
using OutputPort = InputPort;
struct Parameter {
    std::string name, type, unit, default_value;
    bool runtime_mutable{};
};
using ParameterSchema = std::vector<Parameter>;
enum class ExecutionClass { realtime, interactive, background, offline };
enum class Location { any, local, device, remote };
struct Resources {
    std::vector<std::string> backends{"cpu"};
    uint64_t memory_bytes{};
    uint32_t cpu_threads{1};
    Location location{Location::local};
};
struct NodeDescriptor {
    std::string id, plugin_id, algorithm_id;
    SemanticVersion version{0, 1, 0};
    std::vector<InputPort> inputs;
    std::vector<OutputPort> outputs;
    ParameterSchema parameters;
    Resources resources;
    ExecutionClass execution_class{ExecutionClass::background};
    bool deterministic{true}, stateful{}, streaming{true}, batch{true};
};
class NodeInstance {
  public:
    virtual ~NodeInstance() = default;
    virtual Result<data::Published> process(std::span<const data::Published>, const CancellationToken &) = 0;
};
using NodeFactory = std::function<std::unique_ptr<NodeInstance>()>;
struct Node {
    NodeDescriptor descriptor;
    NodeFactory factory;
};
struct Connection {
    size_t source{}, source_port{}, target{}, target_port{};
    size_t capacity{4};
    QueuePolicy policy{QueuePolicy::block};
};
struct PipelineGraph {
    std::vector<Node> nodes;
    std::vector<Connection> connections;
};
struct PipelineRecipe {
    std::string id;
    uint32_t schema_version{1};
    PipelineGraph graph;
    data::Metadata parameters;
};
struct NodeTiming {
    std::string node;
    uint64_t nanoseconds{};
};
} // namespace mantis::pipeline
