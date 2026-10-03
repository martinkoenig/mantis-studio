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
