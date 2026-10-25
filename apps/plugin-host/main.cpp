#include <fstream>
#include <iostream>
#include <mantis/data_io.hpp>
#include <mantis/laser_observation_io.hpp>
#include <mantis/plugin_runtime.hpp>
#include <nlohmann/json.hpp>
#ifndef _WIN32
#include <sys/resource.h>
#endif
int main(int argc, char **argv) {
#ifndef _WIN32
    rlimit limit{0, 0};
    setrlimit(RLIMIT_CORE, &limit);
#endif
    try {
        if (argc < 4)
            mantis::fail(mantis::Status::invalid_argument,
                         "Usage: mantis-plugin-host probe|process|export LIB INPUT_OR_REPORT [OUTPUT]");
        mantis::plugins::Loaded plugin(argv[2]);
        std::string operation = argv[1];
        if (operation == "probe") {
            nlohmann::json report{{"id", plugin.api()->id}, {"version", plugin.api()->version}};
            auto processor = plugin.api()->query_interface(MANTIS_PROCESSOR_V1);
            if (processor) {
                auto node = mantis::plugins::describe_node(plugin);
                report["node"] = node.id;
                report["input"] = node.inputs[0].type.name;
                report["output"] = node.outputs[0].type.name;
                report["input_schema"] = node.inputs[0].type.version;
                report["output_schema"] = node.outputs[0].type.version;
                report["deterministic"] = node.deterministic;
                report["backend"] = node.resources.backends.front();
            }
            if (plugin.api()->query_interface(MANTIS_PROCESSOR_V2)) {
                auto node = mantis::plugins::describe_semantic_node(plugin);
                report["semantic_node"] = {
                    {"node", node.id},
                    {"input", node.inputs[0].type.name},
                    {"input_schema", node.inputs[0].type.version},
                    {"output", node.outputs[0].type.name},
                    {"output_schema", node.outputs[0].type.version},
                    {"deterministic", node.deterministic},
                    {"backend", node.resources.backends.front()},
                    {"algorithm_version", {node.version.major, node.version.minor, node.version.patch}}};
            }
            std::ofstream out(argv[3]);
            out << report.dump(2);
            if (!out)
                return 2;
            return 0;
        }
        if (operation == "process-semantic") {
            if (argc != 6 && argc != 7)
                return 2;
            auto input = mantis::data::read_semantic_packet(argv[3]);
            auto output =
                mantis::plugins::process_semantic(plugin, input, static_cast<uint32_t>(std::stoul(argv[5])));
            // Parent uses a unique invocation directory and accepts only a completed checked result.
            auto part = std::filesystem::path(argv[4]);
            part += ".part";
            mantis::data::write_semantic_packet(part, *output);
            std::filesystem::rename(part, argv[4]);
            return 0;
        }
        if (argc != 5)
            return 2;
        auto input = mantis::data::read_packet(argv[3]);
        if (operation == "process")
            mantis::data::write_packet(argv[4], *mantis::plugins::process(plugin, *input));
        else if (operation == "export")
            mantis::plugins::export_data(plugin, *input, argv[4]);
        else
            return 2;
        return 0;
    } catch (const std::exception &e) {
        if (argc == 7 && std::string_view(argv[1]) == "process-semantic") {
            // Bounded supplemental diagnostics only; semantic output remains the checked typed format.
            std::ofstream diagnostic(argv[6], std::ios::binary);
            diagnostic << std::string_view(e.what()).substr(0, 2048);
        }
        std::cerr << "plugin-host: " << e.what() << '\n';
        return 1;
    }
}
