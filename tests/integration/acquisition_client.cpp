#include <iostream>
#include <mantis/client.hpp>
#include <thread>
int main() {
    try {
        mantis::client::Client client;
        std::string device;
        for (const auto &d : client.devices()) if (d.plugin_id() == "org.mantis.x1" && d.parent().empty()) device = d.id();
        if (device.empty()) throw std::runtime_error("Fake X1 unavailable");
        auto capture = client.start_capture({device});
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        mantis::data::Published preview;
        while (!(preview = client.preview(capture))) {
            if (std::chrono::steady_clock::now() >= deadline) throw std::runtime_error("C++ preview timeout");
            std::this_thread::yield();
        }
        if (preview->frames.size() != 2 || preview->frames[0]->header.metadata.at("role") != "left" ||
            preview->frames[1]->header.metadata.at("role") != "right") throw std::runtime_error("Invalid dual preview");
        auto status = client.capture_status(capture);
        client.stop_capture(capture);
        auto verified = client.wait(client.replay(status.raw_artifact(), false, true));
        if (verified.status().find("PASS") == std::string::npos) throw std::runtime_error("C++ verification failed");
        std::cout << "C++ acquisition client passed\n"; return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
