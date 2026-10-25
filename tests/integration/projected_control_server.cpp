// Test-only loopback daemon using the production dispatcher and fake L2 plugin.
#include "../contract/projected-light/fixture.h"
#include <iostream>
#include <mantis/plugin_runtime.hpp>
#include <mantis/service_adapter.hpp>
int main(int argc, char **argv) {
    try {
        if (argc != 5)
            return 2;
        mantis::plugins::Loaded plugin(argv[1]);
        auto *control =
            static_cast<const TestProjectedControl *>(plugin.api()->query_interface(TEST_PROJECTED_CONTROL));
        control->fault(TEST_SERVICE_CAPTURE);
        mantis::services::Runtime runtime({argv[2], {}, argv[3], {}, {"org.example.projected-contract"}});
        auto socket = mantis::platform::Socket::listen(static_cast<uint16_t>(std::stoul(argv[4])));
        for (;;) {
            auto connection = socket.accept();
            mantis::wire::v1::Request q;
            mantis::protocol::receive(connection, q);
            auto response = mantis::protocol::dispatch(runtime, q);
            mantis::protocol::send(connection, response);
            if (q.has_shutdown())
                break;
        }
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
