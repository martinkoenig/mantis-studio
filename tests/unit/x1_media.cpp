#include "../../plugins/first-party/devices/x1/media.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <nlohmann/json.hpp>
using namespace x1;
#define CHECK(x) do { if (!(x)) throw std::runtime_error("Check failed: " #x); } while (false)
template<class F> void rejects(F fn) { bool failed{}; try { fn(); } catch (const std::exception &) { failed = true; } CHECK(failed); }
int main() {
    try {
        Graph graph{"fixture", "bus", {{1, "sensor", "sensor-node", false, {{0, true, false}}},
            {2, "bridge", "bridge-node", false, {{0, false, true}, {1, true, false}}},
            {3, "video", "video-node", true, {{0, false, true}}}},
            {{{1, 0}, {2, 0}, true, true}, {{2, 1}, {3, 0}, false, false}}};
        auto routes = possible_routes(graph, graph.entities[0], false); CHECK(routes.size() == 1);
        CHECK(possible_routes(graph, graph.entities[0], true).empty());
        auto selected = select_route(graph, routes[0], "LEFT"); CHECK(selected.links.size() == 2);
        CHECK(!selected.links[1].enabled); // discovery does not require mutable links enabled
        auto missing = graph; missing.entities.erase(missing.entities.begin() + 1);
        rejects([&] { (void)select_route(missing, routes[0], "LEFT"); });
        auto ambiguous = graph; auto duplicate = graph.entities[1]; duplicate.id = 4; ambiguous.entities.push_back(duplicate);
        rejects([&] { (void)select_route(ambiguous, routes[0], "RIGHT"); });
        auto link_ambiguous = graph; link_ambiguous.links.push_back({{2, 2}, {3, 0}, false, false});
        rejects([&] { (void)select_route(link_ambiguous, routes[0], "RIGHT"); });
        auto file = std::filesystem::temp_directory_path() / ("mantis-profile-" + std::to_string(monotonic_ns()) + ".json");
        struct Cleanup { std::filesystem::path file; ~Cleanup() { std::filesystem::remove(file); } } cleanup{file};
        nlohmann::json profile{{"format_version", 2}, {"measurement_cameras", {
            {"left", {{"sensor_identity", "sensor-left"}, {"route", {"sensor-left", "bridge-left", "video-left"}}}},
            {"right", {{"sensor_identity", "sensor-right"}, {"route", {"sensor-right", "bridge-right", "video-right"}}}}}},
            {"mode", {{"width", 1280}, {"height", 720}, {"fourcc", "Y10P"}, {"fps", 120}, {"media_bus_code", "Y10_1X10"}, {"vertical_blanking", 196}}},
            {"runtime_setup", {{"ownership", "selected-routes"}, {"disable_conflicting_links", true}}}};
        auto load = [&] { { std::ofstream out(file); out << profile; } return load_profile(file.string()); };
        auto parsed = load(); CHECK(parsed.vertical_blanking == 196 && parsed.mode.height == 720 && parsed.format_version == 2);
        std::array<CameraInfo, 2> cameras{{{"sensor-left", "bus", "left-node", {"Y10P"}, "LEFT", "fixture-media", {}},
            {"sensor-right", "bus", "right-node", {"Y10P"}, "RIGHT", "fixture-media", {}}}};
        auto before = [&](MediaIo &io) {
            std::string snapshot;
            auto g = io.graph("fixture-media");
            for (const auto &l : g.links) snapshot += std::to_string(l.enabled);
            for (const auto &e : g.entities) if (e.id < 100 && !e.capture) for (const auto &pad : e.pads) {
                auto f = io.get_format(e, pad.index);
                snapshot += ":" + std::to_string(f.width) + ":" + std::to_string(f.height) + ":" + std::to_string(f.code);
                if (e.pads.size() == 1) snapshot += ":" + std::to_string(io.get_vblank(e));
            }
            return snapshot;
        };
        for (const auto *scenario : {"normal", "conflict", "source-conflict", "fanout"}) {
            auto io = fake_media(parsed, cameras, scenario); auto initial = before(*io);
            auto setup = configure_media(*io, parsed, cameras);
            auto g = io->graph("fixture-media");
            CHECK(g.links[0].enabled && g.links[1].enabled && g.links[2].enabled && g.links[3].enabled);
            if (std::string(scenario) == "fanout") CHECK(g.links[5].enabled);
            CHECK(g.links[4].enabled); // unrelated RGB route retained
            CHECK(io->get_vblank(g.entities.front()) == 196);
            CHECK(setup->diagnostics().at("left_readback_vblank") == "196");
            CHECK(setup->diagnostics().at("right_sensor_driver_interval") == "unavailable");
            setup->rollback(); CHECK(before(*io) == initial);
        }
        for (const auto *scenario : {"immutable-conflict", "missing-route", "ambiguous-route", "unsupported-mbus", "setup-left", "setup-right", "format-readback-mismatch", "vblank-readback-mismatch", "link-readback-mismatch", "pad-readback-failure", "late-route-mismatch"}) {
            auto io = fake_media(parsed, cameras, scenario); auto initial = before(*io);
            rejects([&] { (void)configure_media(*io, parsed, cameras); });
            CHECK(before(*io) == initial);
        }
        auto rollback_io = fake_media(parsed, cameras, "rollback-failure");
        std::string failure;
        try { (void)configure_media(*rollback_io, parsed, cameras); } catch (const std::exception &e) { failure = e.what(); }
        CHECK(failure.find("rollback incomplete") != std::string::npos);
        CHECK(failure.find("LEFT") != std::string::npos && failure.find("RIGHT") != std::string::npos);
        CHECK(rollback_io->graph("fixture-media").links[4].enabled);
        auto io = fake_media(parsed, cameras, "conflict"); auto initial = before(*io);
        parsed.disable_conflicting_links = false;
        rejects([&] { (void)configure_media(*io, parsed, cameras); }); CHECK(before(*io) == initial);
        parsed.disable_conflicting_links = true;
        auto setup = configure_media(*io, parsed, cameras); auto configured = before(*io);
        setup->commit(); setup->rollback(); CHECK(before(*io) == configured);
        profile["format_version"] = 1; rejects(load);
        profile["format_version"] = 2; profile["mode"]["media_bus_code"] = "unsupported"; rejects(load);
        std::cout << "Disabled-link route discovery and explicit ambiguity checks passed\n";
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
