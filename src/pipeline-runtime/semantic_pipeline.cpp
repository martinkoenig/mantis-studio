#include <cstring>
#include <limits>
#include <mantis/pipeline_runtime.hpp>
namespace mantis::pipeline {
namespace {
class CompilerMarker final : public NodeInstance {
    Result<data::Published> process(std::span<const data::Published>, const CancellationToken &) override {
        return std::unexpected(Error{Status::unsupported, "Compiler marker cannot execute", "pipeline"});
    }
};
using Instances = std::vector<std::unique_ptr<SemanticNodeInstance>>;
Instances instantiate(const SemanticExecutionPlan &p) {
    Instances out;
    for (const auto &n : p.graph.nodes) {
        auto instance = n.factory ? n.factory() : nullptr;
        if (n.factory && !instance)
            fail(Status::incompatible, "Null semantic node instance");
        out.push_back(std::move(instance));
    }
    return out;
}
// Charge complete backing extents once per Storage identity. Owner wrappers may conservatively
// overcharge aliases; they carry the original backing extent without copying any payload.
uint64_t retained_bytes(const std::vector<data::SemanticPublished> &outputs,
                        const data::SemanticPublished &input, const data::SemanticPublished &candidate) {
    std::map<const void *, uint64_t> storage;
    auto attributes = [&](const auto &values) {
        for (const auto &a : values)
            storage[a.buffer.identity()] = a.buffer.backing_size();
    };
    auto packet = [&](const data::Published &p) {
        attributes(p->attributes);
        for (const auto &f : p->frames)
            attributes(f->attributes);
    };
    auto inspect = [&](const data::SemanticPublished &value) {
        if (!value)
            return;
        std::visit(
            [&](const auto &v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, data::Published>)
                    packet(v);
                else if constexpr (std::is_same_v<T, data::AcquisitionBundle>) {
                    if (v.frameset)
                        packet(v.frameset);
                } else if constexpr (std::is_same_v<T, data::LaserObservation>)
                    attributes(v.attributes);
            },
            *value);
    };
    inspect(input);
    inspect(candidate);
    for (const auto &output : outputs)
        inspect(output);
    uint64_t bytes{};
    for (const auto &[identity, size] : storage) {
        if (size > std::numeric_limits<uint64_t>::max() - bytes)
            fail(Status::invalid_argument, "Semantic retained payload arithmetic overflow");
        bytes += size;
    }
    return bytes;
}
Result<SemanticExecutionResult> frame(const SemanticExecutionPlan &p, Instances &instances,
                                      data::SemanticPublished input, const CancellationToken &token) {
    try {
        if (!input)
            fail(Status::invalid_argument, "Null semantic input");
        auto valid = data::validate(*input);
        if (!valid)
            throw Failure(valid.error());
        SemanticExecutionResult result;
        std::vector<data::SemanticPublished> outputs(p.graph.nodes.size());
        std::vector<size_t> consumers(p.graph.nodes.size());
        size_t sources{};
        for (const auto &e : p.graph.connections)
            ++consumers[e.source];
        for (const auto &node : p.graph.nodes)
            sources += !node.factory;
        auto admit = [&](const data::SemanticPublished &candidate) {
            auto bytes = retained_bytes(outputs, input, candidate);
            if (!p.graph.retained_payload_limit_bytes || bytes > p.graph.retained_payload_limit_bytes)
                fail(Status::busy, "Semantic retained payload budget exhausted", "semantic-pipeline");
            result.retained_payload_high_water = std::max(result.retained_payload_high_water, bytes);
        };
        admit({});
        // The reference executor visits one complete publication in the shared compiled DAG order.
        // Edges carry immutable references; at most one value per edge, no additional pixel staging.
        for (auto n : p.order) {
            token.check();
            auto begin = std::chrono::steady_clock::now();
            const auto &node = p.graph.nodes[n];
            std::vector<data::SemanticPublished> inputs(node.descriptor.inputs.size());
            for (const auto &e : p.graph.connections)
                if (e.target == n) {
                    inputs[e.target_port] = outputs[e.source];
                    if (!inputs[e.target_port] || data::semantic_type(*inputs[e.target_port]) !=
                                                      node.descriptor.inputs[e.target_port].type)
                        fail(Status::incompatible, "Semantic input type/schema mismatch");
                }
            data::SemanticPublished candidate;
            if (node.factory) {
                auto r = instances[n]->process(inputs, token);
                if (!r)
                    throw Failure(r.error());
                candidate = std::move(*r);
            } else
                candidate = input;
            token.check();
            if (!candidate || data::semantic_type(*candidate) != node.descriptor.outputs[0].type)
                fail(Status::incompatible, "Semantic output type/schema mismatch");
            auto r = data::validate(*candidate);
            if (!r)
                throw Failure(r.error());
            admit(candidate); // includes consumed input and candidate coexistence before releasing last use
            inputs.clear();
            for (const auto &e : p.graph.connections)
                if (e.target == n && --consumers[e.source] == 0)
                    outputs[e.source].reset();
            outputs[n] = std::move(candidate);
            if (!node.factory && --sources == 0)
                input.reset();
            result.timings.push_back(
                {node.descriptor.id,
                 static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                           std::chrono::steady_clock::now() - begin)
                                           .count())});
            if (std::none_of(p.graph.connections.begin(), p.graph.connections.end(),
                             [n](const auto &e) { return e.source == n; }))
                result.outputs.emplace(node.descriptor.id, outputs[n]);
        }
        if (!p.order.empty())
            result.output = outputs[p.order.back()];
        return result;
    } catch (const Failure &e) {
        return std::unexpected(e.error);
    } catch (const std::exception &e) {
        return std::unexpected(Error{Status::plugin_failed, e.what(), "semantic-pipeline"});
    }
}
class Consumer final : public SemanticNodeInstance {
    Result<data::SemanticPublished> process(std::span<const data::SemanticPublished> inputs,
                                            const CancellationToken &token) override {
        try {
            token.check();
            if (inputs.size() != 1 || !inputs[0] ||
                !std::holds_alternative<data::LaserObservation>(*inputs[0]))
                fail(Status::incompatible, "Observation consumer requires LaserObservation");
            auto r = summarize(std::get<data::LaserObservation>(*inputs[0]));
            if (!r)
                throw Failure(r.error());
            return inputs[0]; // includes every unknown extension, no header/provenance replacement
        } catch (const Failure &e) {
            return std::unexpected(e.error);
        }
    }
};
} // namespace
Result<SemanticExecutionPlan> compile(const SemanticGraph &graph) {
    // Reuse the existing typed DAG compiler, schema/ports, backend planner and deterministic topological
    // order.
    if (!graph.retained_payload_limit_bytes ||
        graph.retained_payload_limit_bytes > 2ull * 1024 * 1024 * 1024 || graph.nodes.size() > 256 ||
        graph.connections.size() > 1024)
        return std::unexpected(
            Error{Status::invalid_argument, "Semantic DAG exceeds finite limits", "pipeline"});
    PipelineRecipe recipe;
    recipe.graph.connections = graph.connections;
    for (const auto &n : graph.nodes)
        recipe.graph.nodes.push_back({n.descriptor, n.factory
                                                        ? std::function<std::unique_ptr<NodeInstance>()>{[] {
                                                              return std::make_unique<CompilerMarker>();
                                                          }}
                                                        : nullptr});
    for (const auto &e : graph.connections)
        if (e.capacity > 64 || (e.policy != QueuePolicy::block && e.policy != QueuePolicy::lossless))
            return std::unexpected(Error{Status::invalid_argument,
                                         "Semantic edges require bounded lossless policy", "pipeline"});
    auto plan = compile(recipe);
    if (!plan)
        return std::unexpected(plan.error());
    return SemanticExecutionPlan{graph, plan->order, plan->backends};
}
Result<SemanticExecutionResult> execute(const SemanticExecutionPlan &p, data::SemanticPublished input,
                                        const CancellationToken &token) {
    try {
        auto instances = instantiate(p);
        return frame(p, instances, std::move(input), token);
    } catch (const Failure &e) {
        return std::unexpected(e.error);
    } catch (const std::exception &e) {
        return std::unexpected(Error{Status::plugin_failed, e.what(), "semantic-pipeline"});
    }
}
Result<void> execute_stream(const SemanticExecutionPlan &p, BoundedQueue<data::SemanticPublished> &source,
                            BoundedQueue<SemanticExecutionResult> &sink, std::stop_token stop) {
    struct Close {
        decltype(source) in;
        decltype(sink) out;
        ~Close() {
            in.close();
            out.close();
        }
    } close{source, sink};
    try {
        if ((source.policy() != QueuePolicy::block && source.policy() != QueuePolicy::lossless) ||
            (sink.policy() != QueuePolicy::block && sink.policy() != QueuePolicy::lossless))
            fail(Status::invalid_argument, "Semantic stream requires lossless queues");
        auto instances = instantiate(p);
        CancellationToken token;
        std::stop_callback callback(stop, [token] { token.cancel(); });
        while (auto input = source.pop(stop)) {
            auto r = frame(p, instances, std::move(*input), token);
            if (!r)
                return std::unexpected(r.error());
            if (!sink.push(std::move(*r), stop)) {
                token.check();
                fail(Status::io, "Semantic sink closed before lossless publication");
            }
        }
        token.check();
        return {};
    } catch (const Failure &e) {
        return std::unexpected(e.error);
    } catch (const std::exception &e) {
        return std::unexpected(Error{Status::plugin_failed, e.what(), "semantic-pipeline"});
    }
}
Result<ObservationSummary> summarize(const data::LaserObservation &v) {
    auto r = data::validate(v);
    if (!r)
        return std::unexpected(r.error());
    ObservationSummary out{v.sample_count, 0, 0, 0, 0, *v.disposition};
    auto mask = [&](std::string_view name) -> const data::Attribute * {
        for (const auto &a : v.attributes)
            if (a.descriptor.name == name)
                return &a;
        return nullptr;
    };
    auto count = [&](const data::Attribute *a) {
        uint64_t n = 0;
        if (a) {
            auto b = a->buffer.map_read();
            if (!b)
                throw Failure(b.error());
            for (uint64_t i = 0; i < v.sample_count; ++i)
                n += std::to_integer<uint8_t>((*b)[i * a->descriptor.stride[0]]) == 1;
        }
        return n;
    };
    try {
        out.known_emitters = count(mask(data::laser::emitter_valid));
        out.known_lines = count(mask(data::laser::line_valid));
        out.unknown_emitters = v.sample_count - out.known_emitters;
        out.unknown_lines = v.sample_count - out.known_lines;
        return out;
    } catch (const Failure &e) {
        return std::unexpected(e.error);
    }
}
SemanticNode observation_consumer() {
    NodeDescriptor d;
    d.id = "org.mantis.observation-summary";
    d.inputs = {{"observation", schema::laser_observation}};
    d.outputs = {{"observation", schema::laser_observation}};
    d.deterministic = true;
    return {std::move(d), [] { return std::make_unique<Consumer>(); }};
}
namespace {
class LegacyBridge final : public SemanticNodeInstance {
    std::unique_ptr<NodeInstance> node;

  public:
    explicit LegacyBridge(std::unique_ptr<NodeInstance> value) : node(std::move(value)) {
        if (!node)
            fail(Status::incompatible, "Null legacy node instance");
    }
    Result<data::SemanticPublished> process(std::span<const data::SemanticPublished> inputs,
                                            const CancellationToken &token) override {
        try {
            token.check();
            std::vector<data::Published> packets;
            for (const auto &input : inputs) {
                auto p = input ? std::get_if<data::Published>(input.get()) : nullptr;
                if (!p || !*p || !(*p)->frames.empty() || (*p)->type == schema::frameset)
                    fail(Status::incompatible, "Legacy bridge accepts flat packets only");
                packets.push_back(*p);
            }
            auto result = node->process(packets, token);
            if (!result)
                throw Failure(result.error());
            if (!*result || !(*result)->frames.empty())
                fail(Status::incompatible, "Legacy bridge output must be flat");
            return data::publish(data::SemanticPacket{*result});
        } catch (const Failure &e) {
            return std::unexpected(e.error);
        } catch (const std::exception &e) {
            return std::unexpected(Error{Status::plugin_failed, e.what(), "legacy-bridge"});
        }
    }
};
} // namespace
SemanticNode semantic_bridge(Node node) {
    if (!node.factory)
        fail(Status::invalid_argument, "Legacy bridge requires an executable node");
    for (const auto *ports : {&node.descriptor.inputs, &node.descriptor.outputs})
        for (const auto &p : *ports)
            for (const auto &composite :
                 {schema::frameset, schema::acquisition_bundle, schema::acquisition_evidence,
                  schema::trigger_event, schema::laser_observation})
                if (p.type.name == composite.name)
                    fail(Status::incompatible, "Legacy bridge cannot flatten typed semantic data");
    return {node.descriptor,
            [factory = std::move(node.factory)] { return std::make_unique<LegacyBridge>(factory()); }};
}
} // namespace mantis::pipeline
