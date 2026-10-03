#include <mantis/pipeline_runtime.hpp>
#include <queue>
#include <set>
namespace mantis::pipeline {
Result<ExecutionPlan> compile(const PipelineRecipe &recipe) {
    try {
        if (recipe.schema_version != 1 || recipe.graph.nodes.empty())
            fail(Status::invalid_argument, "Unsupported/empty recipe");
        ExecutionPlan plan;
        plan.graph = recipe.graph;
        auto count = plan.graph.nodes.size();
        std::vector<size_t> indegree(count);
        std::set<std::pair<size_t, size_t>> connected;
        std::set<std::string> ids;
        for (auto &node : plan.graph.nodes) {
            if (!ids.insert(node.descriptor.id).second)
                fail(Status::invalid_argument, "Duplicate node ID");
            auto backend = compute::select(node.descriptor.resources);
            if (!backend)
                throw Failure(backend.error());
            plan.backends.push_back(*backend);
            if (node.descriptor.outputs.size() != 1)
                fail(Status::unsupported, "v0.1 executor requires one output per node");
        }
        for (auto &e : plan.graph.connections) {
            if (e.source >= count || e.target >= count || e.capacity == 0)
                fail(Status::invalid_argument, "Invalid connection");
            auto &a = plan.graph.nodes[e.source].descriptor;
            auto &b = plan.graph.nodes[e.target].descriptor;
            if (e.source_port >= a.outputs.size() || e.target_port >= b.inputs.size())
                fail(Status::invalid_argument, "Invalid port index");
            if (a.outputs[e.source_port].type != b.inputs[e.target_port].type)
                fail(Status::incompatible, "Pipeline port types do not match");
            if (!connected.insert({e.target, e.target_port}).second)
                fail(Status::invalid_argument, "Input port has multiple producers");
            ++indegree[e.target];
        }
        size_t external_sources = 0;
        for (size_t n = 0; n < count; ++n) {
            if (!plan.graph.nodes[n].factory) {
                if (!plan.graph.nodes[n].descriptor.inputs.empty())
                    fail(Status::invalid_argument, "Only source nodes may omit their factory");
                ++external_sources;
            }
            for (size_t p = 0; p < plan.graph.nodes[n].descriptor.inputs.size(); ++p)
                if (!connected.contains({n, p}))
                    fail(Status::invalid_argument, "Unconnected input port");
        }
        if (external_sources != 1)
            fail(Status::unsupported, "v0.1 requires one external source node");
        std::queue<size_t> ready;
        for (size_t i = 0; i < count; ++i)
            if (!indegree[i])
                ready.push(i);
        while (!ready.empty()) {
            auto n = ready.front();
            ready.pop();
            plan.order.push_back(n);
            for (auto &e : plan.graph.connections)
                if (e.source == n && --indegree[e.target] == 0)
                    ready.push(e.target);
        }
        if (plan.order.size() != count)
            fail(Status::invalid_argument, "Data graph contains a cycle");
        return plan;
    } catch (const Failure &e) {
        return std::unexpected(e.error);
    }
}
namespace {
using Instances = std::vector<std::unique_ptr<NodeInstance>>;
Instances instantiate(const ExecutionPlan &plan) {
    Instances instances;
    for (const auto &node : plan.graph.nodes)
        instances.push_back(node.factory ? node.factory() : nullptr);
    return instances;
}
Result<ExecutionResult> execute_frame(const ExecutionPlan &plan, Instances &instances, data::Published input,
                                      const CancellationToken &token) {
    try {
        ExecutionResult result;
        std::vector<data::Published> outputs(plan.graph.nodes.size());
        std::vector<std::unique_ptr<BoundedQueue<data::Published>>> edges;
        for (auto &e : plan.graph.connections)
            edges.push_back(std::make_unique<BoundedQueue<data::Published>>(e.capacity, e.policy));
        for (auto n : plan.order) {
            token.check();
            const auto &node = plan.graph.nodes[n];
            auto begin = std::chrono::steady_clock::now();
            std::vector<data::Published> inputs(node.descriptor.inputs.size());
            for (size_t i = 0; i < plan.graph.connections.size(); ++i) {
                auto &e = plan.graph.connections[i];
                if (e.target == n) {
                    auto item = edges[i]->pop();
                    if (!item)
                        fail(Status::io, "Closed pipeline connection");
                    inputs[e.target_port] = *item;
                }
            }
            if (node.factory) {
                auto r = instances[n]->process(inputs, token);
                if (!r)
                    throw Failure(r.error());
                outputs[n] = *r;
            } else
                outputs[n] = input;
            if (!outputs[n] || outputs[n]->type != node.descriptor.outputs[0].type)
                fail(Status::incompatible, "Node produced a type different from its descriptor");
            for (size_t i = 0; i < plan.graph.connections.size(); ++i)
                if (plan.graph.connections[i].source == n)
                    edges[i]->push(outputs[n]);
            result.timings.push_back(
                {node.descriptor.id,
                 static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                           std::chrono::steady_clock::now() - begin)
                                           .count())});
            result.output = outputs[n];
            if (std::none_of(plan.graph.connections.begin(), plan.graph.connections.end(),
                             [n](const auto &edge) { return edge.source == n; }))
                result.outputs.emplace(node.descriptor.id, outputs[n]);
        }
        return result;
    } catch (const Failure &e) {
        return std::unexpected(e.error);
    } catch (const std::exception &e) {
        return std::unexpected(Error{Status::io, e.what(), "pipeline"});
    }
}
} // namespace
Result<ExecutionResult> execute(const ExecutionPlan &plan, data::Published input,
                                const CancellationToken &token) {
    try {
        auto instances = instantiate(plan);
        return execute_frame(plan, instances, std::move(input), token);
    } catch (const Failure &e) {
        return std::unexpected(e.error);
    } catch (const std::exception &e) {
        return std::unexpected(Error{Status::io, e.what(), "pipeline"});
    }
}
Result<void> execute_stream(const ExecutionPlan &plan, BoundedQueue<data::Published> &source,
                            const std::function<void(ExecutionResult)> &sink, std::stop_token stop) {
    try {
        auto instances = instantiate(plan);
        CancellationToken token;
        std::stop_callback callback(stop, [token] { token.cancel(); });
        while (auto input = source.pop(stop)) {
            auto r = execute_frame(plan, instances, *input, token);
            if (!r)
                return std::unexpected(r.error());
            sink(std::move(*r));
        }
        return {};
    } catch (const Failure &e) {
        return std::unexpected(e.error);
    } catch (const std::exception &e) {
        return std::unexpected(Error{Status::io, e.what(), "pipeline"});
    }
}
} // namespace mantis::pipeline
