#include <fstream>
#include <iostream>
#include <mantis/data_io.hpp>
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
            std::ofstream out(argv[3]);
            out << report.dump(2);
            if (!out)
                return 2;
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
        std::cerr << "plugin-host: " << e.what() << '\n';
        return 1;
    }
}
