#include <google/protobuf/util/json_util.h>
#include <iostream>
#include <mantis/client.hpp>
#include "calibration_commands.hpp"
namespace {
void print(const google::protobuf::Message &message) {
    google::protobuf::util::JsonPrintOptions options;
    options.add_whitespace = true;
    options.preserve_proto_field_names = true;
    options.always_print_primitive_fields = true;
    std::string json;
    auto result = google::protobuf::util::MessageToJsonString(message, &json, options);
    if (!result.ok())
        throw std::runtime_error(result.ToString());
    std::cout << json << '\n';
}
void usage() {
    std::cout << "mantis-cli devices list | devices info DEVICE | captures list | snapshot | artifacts list | plugins list\n"
                 "mantis-cli project create|open PATH\n"
                 "mantis-cli capture start DEVICE | capture status|stop CAPTURE\n"
                 "mantis-cli replay verify|asap|realtime RAW_ARTIFACT\n"
                 "mantis-cli pipeline run CAPTURE [example|crash-test]\n"
                 "mantis-cli pipeline replay RAW_ARTIFACT\n"
                 "mantis-cli job wait|cancel JOB\n"
                 "mantis-cli artifact recover ARTIFACT\n"
                 "mantis-cli plugin enable|disable PLUGIN\n"
                 "mantis-cli export ARTIFACT OUTPUT.ply\n"
                 "mantis-cli workflow OUTPUT.ply\n"
                 "mantis-cli calibration list | info ARTIFACT | active DEVICE | activate DEVICE RIG | clear DEVICE\n"
                 "mantis-cli calibration target create checkerboard|charuco --squares-x N --squares-y N --square-mm X [options]\n"
                 "mantis-cli calibration dataset build TARGET RAW [RAW...] --role ROLE [--role ROLE...] --max-samples N [--series ID]\n"
                 "mantis-cli calibration camera solve DATASET ROLE --heldout N [--series ID]\n"
                 "mantis-cli calibration rig solve DATASET LEFT RIGHT --heldout N --rig-frame-id ID --rig-frame-name NAME [--series ID]\n"
                 "Target charuco: --marker-mm X --dictionary NAME --layout black_square_at_origin|white_square_at_origin_even_rows\n"
                 "Target measurement: --measured-width-mm X --measured-height-mm X; provenance: --measurement-provenance,\n"
                 "--width-uncertainty-mm X --height-uncertainty-mm X --instrument TEXT --note TEXT; revision: --series ID\n"
                 "Calibration jobs return result_id without waiting; use mantis-cli job wait JOB.\n"
                 "mantis-cli shutdown\nEnvironment: MANTIS_TOKEN, optional MANTIS_PORT (47321).\n";
}
} // namespace
int main(int argc, char **argv) {
    try {
        if (argc < 2 || std::string(argv[1]) == "--help") {
            usage();
            return 0;
        }
        mantis::client::Client client;
        mantis::wire::v1::Request request;
        std::string command = argv[1];
        auto arg = [&](int n) -> std::string {
            if (n >= argc)
                throw std::runtime_error("Missing argument; see --help");
            return argv[n];
        };
        if (command == "calibration") {
            std::vector<std::string> args;
            if (argc > 1302) throw std::runtime_error("Calibration argument count exceeds bound");
            for (int i = 2; i < argc; ++i) args.emplace_back(argv[i]);
            print(client.call(calibration_command(args))); return 0;
        }
        if (command == "snapshot") {
            print(client.snapshot());
            return 0;
        }
        if (command == "devices" && arg(2) == "list")
            request.mutable_devices_list();
        else if (command == "devices" && arg(2) == "info") request.mutable_devices_info()->set_id(arg(3));
        else if (command == "captures" && arg(2) == "list") request.mutable_captures_list();
        else if (command == "replay") {
            auto mode = arg(2);
            if (mode != "verify" && mode != "asap" && mode != "realtime") throw std::runtime_error("Unknown replay mode");
            auto job = client.replay(arg(3), mode == "realtime", mode == "verify");
            if (mode == "verify") { auto result = client.wait(job, std::chrono::hours(1)); std::cout << result.status() << '\n'; }
            else { request.mutable_snapshot(); auto response = client.call(request); response.set_result_id(job); print(response); }
            return 0;
        }
        else if (command == "artifacts" && arg(2) == "list")
            request.mutable_artifacts_list();
        else if (command == "plugins" && arg(2) == "list")
            request.mutable_plugins_list();
        else if (command == "capture") {
            if (arg(2) == "start") {
                request.mutable_capture_start()->add_devices(arg(3));
            } else if (arg(2) == "stop") {
                client.stop_capture(arg(3)); request.mutable_capture_status()->set_id(arg(3));
            }
            else if (arg(2) == "status") request.mutable_capture_status()->set_id(arg(3));
            else throw std::runtime_error("Unknown capture command");
        } else if (command == "pipeline") {
            auto mode = arg(2);
            if (mode == "run")
                request.mutable_pipeline_run()->set_capture_id(arg(3));
            else if (mode == "replay")
                request.mutable_pipeline_run()->set_raw_artifact_id(arg(3));
            else
                throw std::runtime_error("Unknown pipeline command");
            request.mutable_pipeline_run()->set_recipe(argc > 4 ? arg(4) : "example");
        } else if (command == "project") {
            auto mode = arg(2);
            if (mode != "open" && mode != "create")
                throw std::runtime_error("Unknown project command");
            request.mutable_project_open()->set_path(std::filesystem::absolute(arg(3)).string());
            request.mutable_project_open()->set_create(mode == "create");
        } else if (command == "job") {
            if (arg(2) == "wait") {
                print(client.wait(arg(3)));
                return 0;
            }
            if (arg(2) != "cancel")
                throw std::runtime_error("Unknown job command");
            request.mutable_job_cancel()->set_id(arg(3));
        } else if (command == "artifact" && arg(2) == "recover") {
            mantis::wire::v1::Response response;
            *response.add_artifacts() = client.recover_artifact(arg(3));
            print(response); return 0;
        }
        else if (command == "plugin") {
            auto mode = arg(2);
            if (mode != "enable" && mode != "disable")
                throw std::runtime_error("Unknown plugin command");
            request.mutable_plugin_enable()->set_id(arg(3));
            request.mutable_plugin_enable()->set_enabled(mode == "enable");
        } else if (command == "export") {
            print(client.wait(client.export_artifact(arg(2), arg(3))));
            return 0;
        } else if (command == "workflow") {
            auto path = arg(2);
            auto devices = client.devices();
            if (devices.empty())
                throw std::runtime_error("No device plugins available");
            auto capture = client.start_capture({devices.front().id()});
            try {
                auto processed = client.wait(client.run_pipeline(capture));
                client.stop_capture(capture);
                print(client.wait(client.export_artifact(processed.result_artifact(), path)));
            } catch (...) {
                try {
                    client.stop_capture(capture);
                } catch (...) {
                }
                throw;
            }
            return 0;
        } else if (command == "shutdown")
            request.mutable_shutdown();
        else {
            usage();
            return 2;
        }
        print(client.call(request));
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "mantis-cli: " << e.what() << '\n';
        return 1;
    }
}
