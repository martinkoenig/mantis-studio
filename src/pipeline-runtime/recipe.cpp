#include <fstream>
#include <mantis/pipeline_runtime.hpp>
#include <nlohmann/json.hpp>
namespace mantis::pipeline {
Result<PipelineRecipe> load_recipe(const std::filesystem::path &path,
                                   const std::function<Node(const std::string &)> &resolve) {
    try {
        std::ifstream in(path);
        if (!in)
            fail(Status::not_found, "Recipe not found: " + path.string());
        auto json = nlohmann::json::parse(in);
        PipelineRecipe recipe;
        recipe.id = json.at("id");
        recipe.schema_version = json.at("schema_version");
        if (recipe.schema_version != 1)
            fail(Status::incompatible, "Unsupported recipe schema");
        std::map<std::string, size_t> nodes;
        for (const auto &entry : json.at("nodes")) {
            Node node;
            std::string id = entry.at("id");
            if (entry.contains("source_type")) {
                node.descriptor.outputs = {{"output", {entry.at("source_type"), entry.at("source_schema")}}};
            } else {
                node = resolve(entry.at("plugin"));
                auto version = entry.at("algorithm_version");
                SemanticVersion required{version[0], version[1], version[2]};
                if (required != node.descriptor.version)
                    fail(Status::incompatible, "Recipe algorithm version does not match plugin");
            }
            node.descriptor.id = id;
            if (!nodes.emplace(id, recipe.graph.nodes.size()).second)
                fail(Status::invalid_argument, "Duplicate recipe node");
            if (entry.contains("parameters") && !entry.at("parameters").empty())
                fail(Status::unsupported, "Reference plugins have no configurable parameters");
            recipe.graph.nodes.push_back(std::move(node));
        }
        for (const auto &edge : json.at("connections")) {
            Connection c;
            c.source = nodes.at(edge.at("from"));
            c.target = nodes.at(edge.at("to"));
            c.source_port = edge.value("source_port", size_t(0));
            c.target_port = edge.value("target_port", size_t(0));
            c.capacity = edge.at("capacity");
            std::string policy = edge.at("policy");
            if (policy == "BLOCK")
                c.policy = QueuePolicy::block;
            else if (policy == "LOSSLESS")
                c.policy = QueuePolicy::lossless;
            else if (policy == "DROP_OLDEST")
                c.policy = QueuePolicy::drop_oldest;
            else if (policy == "LATEST_ONLY")
                c.policy = QueuePolicy::latest_only;
            else if (policy == "DROP_NEWEST")
                c.policy = QueuePolicy::drop_newest;
            else
                fail(Status::invalid_argument, "Unknown recipe queue policy");
            recipe.graph.connections.push_back(c);
        }
        return recipe;
    } catch (const Failure &e) {
        return std::unexpected(e.error);
    } catch (const std::exception &e) {
        return std::unexpected(Error{Status::invalid_argument, e.what(), "recipe"});
    }
}
} // namespace mantis::pipeline
