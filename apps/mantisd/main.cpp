#include "product_paths.hpp"
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <mantis/service_adapter.hpp>
int main(int argc, char **argv) {
    try {
#ifndef _WIN32
        std::signal(SIGPIPE, SIG_IGN);
#endif
        const auto image = mantis::app::executable_path();
        const auto binary = image.parent_path();
        // Compatibility is confined to the exact configured build output. An installed
        // image never falls back to source/build assets, even when installed files vanish.
        const bool build_tree = binary == std::filesystem::path(mantis::app::build_bin);
        mantis::services::Configuration config{
            build_tree ? binary.parent_path() / "plugins" : binary / mantis::app::plugins_from_bin,
            build_tree ? binary / "mantis-plugin-host"
                       : binary / mantis::app::host_from_bin / "mantis-plugin-host",
            std::filesystem::current_path() / "Example.mantis",
            build_tree ? binary.parent_path() / "recipes" : binary / mantis::app::recipes_from_bin,
            {"org.mantis.virtual-scanner", "org.mantis.virtual-rig", "org.mantis.example-points",
             "org.mantis.ply", "org.mantis.x1"}};
#ifdef _WIN32
        config.plugin_host += ".exe";
#endif
        uint16_t port = 47321;
        std::string token;
        if (auto p = std::getenv("MANTIS_TOKEN"))
            token = p;
        if (auto p = std::getenv("MANTIS_PORT"))
            port = static_cast<uint16_t>(std::stoul(p));
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--help") {
                std::cout << "mantisd [--project PATH] [--plugins DIR] [--plugin-host PATH] [--port "
                             "PORT] [--recipes DIR] [--x1-role-state-dir PROVISIONED_DIR]\nSet MANTIS_TOKEN "
                             "(at least 16 "
                             "characters). Loopback only.\n";
                return 0;
            }
            if (i + 1 >= argc)
                throw std::runtime_error("Missing option value");
            std::string value = argv[++i];
            if (arg == "--project")
                config.project = value;
            else if (arg == "--plugins")
                config.plugins = value;
            else if (arg == "--recipes")
                config.recipes = value;
            else if (arg == "--x1-role-state-dir")
                config.x1_role_state_dir = value;
            else if (arg == "--plugin-host")
                config.plugin_host = value;
            else if (arg == "--port") {
                auto n = std::stoul(value);
                if (n == 0 || n > 65535)
                    throw std::runtime_error("Invalid port");
                port = static_cast<uint16_t>(n);
            } else
                throw std::runtime_error("Unknown option: " + arg);
        }
        if (token.size() < 16)
            throw std::runtime_error("MANTIS_TOKEN must contain at least 16 characters");
#ifdef __linux__
        // Only the explicit startup option grants authority. Inherited locator alone cannot opt in.
        if (config.x1_role_state_dir.empty())
            unsetenv("MANTIS_X1_ROLE_STATE_DIR");
        else if (setenv("MANTIS_X1_ROLE_STATE_DIR", config.x1_role_state_dir.c_str(), 1))
            throw std::runtime_error("Cannot establish X1 role startup locator");
#else
        if (!config.x1_role_state_dir.empty())
            throw std::runtime_error("X1 role configuration is supported only on Linux");
#endif
        mantis::services::Runtime runtime(std::move(config));
        auto server = mantis::platform::Socket::listen(port);
        std::cout << "mantisd ready on 127.0.0.1:" << port << std::endl;
        while (true) {
            // Listener failures are fatal and reach the outer diagnostic once;
            // retrying a broken listener here would produce an error busy loop.
            // Interrupted/temporarily non-ready accepts are handled by Socket.
            auto socket = server.accept();
            try {
                mantis::wire::v1::Request request;
                mantis::protocol::receive(socket, request);
                mantis::wire::v1::Response response;
                if (request.token() != token) {
                    response.set_protocol_version(1);
                    response.set_request_id(request.request_id());
                    response.mutable_error()->set_code(
                        static_cast<uint32_t>(mantis::Status::invalid_argument));
                    response.mutable_error()->set_message("Invalid local access token");
                } else
                    response = mantis::protocol::dispatch(runtime, request);
                mantis::protocol::send(socket, response);
                if (request.has_shutdown() && !response.has_error())
                    break;
            } catch (const std::exception &e) {
                std::cerr << "control: " << e.what() << '\n';
            }
        }
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "mantisd: " << e.what() << '\n';
        return 1;
    }
}
