#include "projected_commands.hpp"
#include <charconv>
#include <fstream>
#include <google/protobuf/util/json_util.h>
namespace {
uint32_t number(const std::string &s) {
    uint32_t n{};
    auto [end, code] = std::from_chars(s.data(), s.data() + s.size(), n);
    if (code != std::errc{} || end != s.data() + s.size() || !n)
        throw std::runtime_error("Positive uint32 required");
    return n;
}
} // namespace
mantis::wire::v1::Request projected_command(std::span<const std::string> args) {
    namespace w = mantis::wire::v1;
    w::Request q;
    auto require = [&](size_t n) {
        if (args.size() != n)
            throw std::runtime_error("Invalid projected command arguments; see --help");
    };
    if (args.empty() || args.size() > 32)
        throw std::runtime_error("Projected command argument bound");
    auto command = args[0];
    if (command == "devices") {
        require(1);
        q.mutable_projected_devices_list();
    } else if (command == "list") {
        require(1);
        q.mutable_projected_captures_list();
    } else if (command == "status") {
        require(2);
        q.mutable_projected_status()->set_id(args[1]);
    } else if (command == "bundle") {
        require(2);
        q.mutable_projected_bundle()->set_id(args[1]);
    } else if (command == "stop" || command == "cancel") {
        require(4);
        auto *s = q.mutable_projected_stop();
        s->set_capture_id(args[1]);
        s->set_expected_run_id(args[2]);
        s->set_expected_generation_id(args[3]);
        s->set_mode(command == "stop" ? w::PROJECTED_NORMAL_STOP : w::PROJECTED_CANCEL);
    } else if (command == "validate" || command == "start") {
        if (args.size() < 5 || args.size() % 2 != 1)
            throw std::runtime_error(
                "Projected validate/start PLUGIN PARENT --program FILE or --program-from-raw ID [options]");
        auto *r = command == "start" ? q.mutable_projected_start() : q.mutable_projected_validate();
        r->set_plugin_id(args[1]);
        r->set_parent_id(args[2]);
        bool program{};
        for (size_t i = 3; i < args.size(); i += 2) {
            const auto &key = args[i], &value = args[i + 1];
            if (key == "--program" || key == "--program-from-raw") {
                if (program)
                    throw std::runtime_error("Exactly one program source required");
                program = true;
                if (key == "--program-from-raw")
                    r->mutable_program()->set_raw_capture_artifact_id(value);
                else {
                    // Human input only. Typed strict JSON is converted by the daemon and persisted as
                    // MRUNHDR3.
                    std::ifstream input(value, std::ios::binary);
                    if (!input)
                        throw std::runtime_error("Cannot open program JSON");
                    std::string text;
                    text.resize(512 * 1024 + 1);
                    input.read(text.data(), text.size());
                    text.resize(input.gcount());
                    if (text.size() > 512 * 1024)
                        throw std::runtime_error("Program JSON exceeds 512 KiB");
                    google::protobuf::util::JsonParseOptions options;
                    options.ignore_unknown_fields = false;
                    auto result = google::protobuf::util::JsonStringToMessage(
                        text, r->mutable_program()->mutable_inline_program(), options);
                    if (!result.ok())
                        throw std::runtime_error(result.ToString());
                }
            } else if (key == "--request-id" && command == "start") {
                if (value.empty() || value.size() > 256)
                    throw std::runtime_error("Request ID must contain 1..256 bytes");
                if (!q.request_id().empty())
                    throw std::runtime_error("Duplicate request ID");
                q.set_request_id(value);
            } else if (key == "--queue-capacity")
                r->mutable_config()->set_queue_capacity(number(value));
            else if (key == "--operation-timeout-ms")
                r->mutable_config()->set_operation_timeout_ms(number(value));
            else if (key == "--abort-timeout-ms")
                r->mutable_config()->set_abort_timeout_ms(number(value));
            else if (key == "--cleanup-timeout-ms")
                r->mutable_config()->set_cleanup_timeout_ms(number(value));
            else if (key == "--publication-timeout-ms")
                r->mutable_config()->set_publication_timeout_ms(number(value));
            else if (key == "--correlation-entries")
                r->mutable_config()->set_max_correlation_entries(number(value));
            else
                throw std::runtime_error("Unknown projected option: " + key);
        }
        if (!program)
            throw std::runtime_error("Explicit projected program source required");
    } else
        throw std::runtime_error("Unknown projected command");
    return q;
}
