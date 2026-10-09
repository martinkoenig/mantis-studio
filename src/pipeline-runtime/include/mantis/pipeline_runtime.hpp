#pragma once
#include <filesystem>
#include <mantis/compute.hpp>
#include <mantis/pipeline_api.hpp>
namespace mantis::pipeline {
struct ExecutionPlan {
    PipelineGraph graph;
    std::vector<size_t> order;
    std::vector<std::string> backends;
};
Result<PipelineRecipe> load_recipe(const std::filesystem::path &,
                                   const std::function<Node(const std::string &)> &);
Result<ExecutionPlan> compile(const PipelineRecipe &);
struct ExecutionResult {
    data::Published output;
    std::map<std::string, data::Published> outputs;
    std::vector<NodeTiming> timings;
};
Result<ExecutionResult> execute(const ExecutionPlan &, data::Published input, const CancellationToken &);
Result<void> execute_stream(const ExecutionPlan &, BoundedQueue<data::Published> &source,
                            const std::function<void(ExecutionResult)> &sink, std::stop_token);
} // namespace mantis::pipeline

namespace mantis::pipeline {
struct SemanticExecutionPlan {
    SemanticGraph graph;
    std::vector<size_t> order;
    std::vector<std::string> backends;
};
struct SemanticExecutionResult {
    uint64_t retained_payload_high_water{};
    data::SemanticPublished output;
    std::map<std::string, data::SemanticPublished> outputs;
    std::vector<NodeTiming> timings;
};
Result<SemanticExecutionPlan> compile(const SemanticGraph &);
Result<SemanticExecutionResult> execute(const SemanticExecutionPlan &, data::SemanticPublished,
                                        const CancellationToken & = {});
// One publication in flight. Both external queues must be LOSSLESS/blocking.
// Closes both queues on exit; errors/cancellation are explicit, never successful drops.
Result<void> execute_stream(const SemanticExecutionPlan &, BoundedQueue<data::SemanticPublished> &source,
                            BoundedQueue<SemanticExecutionResult> &sink, std::stop_token);
struct ObservationSummary {
    uint64_t samples{}, known_emitters{}, known_lines{}, unknown_emitters{}, unknown_lines{};
    data::ObservationDisposition disposition;
};
Result<ObservationSummary> summarize(const data::LaserObservation &);
SemanticNode observation_consumer(); // validates/inspects and forwards the exact immutable value
} // namespace mantis::pipeline

namespace mantis::pipeline {
// Explicit opt-in bridge for existing flat-packet nodes. Never accepts a composite/typed semantic value.
SemanticNode semantic_bridge(Node);
} // namespace mantis::pipeline
