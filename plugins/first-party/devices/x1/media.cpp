#include "media.hpp"
#include <algorithm>
#include <set>
#include <stdexcept>
namespace x1 {
Route select_route(const Graph &graph, const std::vector<std::string> &names, const std::string &role) {
    Route route; route.role = role;
    for (const auto &name : names) {
        std::vector<Entity> matches;
        for (const auto &entity : graph.entities) if (entity.name == name) matches.push_back(entity);
        if (matches.size() != 1) throw std::runtime_error(role + " route entity " + name +
            (matches.empty() ? " missing" : " ambiguous"));
        route.entities.push_back(matches.front());
    }
    if (route.entities.empty() || !route.entities.back().capture)
        throw std::runtime_error(role + " route does not end at a capture entity");
    for (size_t i = 1; i < route.entities.size(); ++i) {
        std::vector<Link> links;
        for (const auto &link : graph.links)
            if (link.source.entity == route.entities[i - 1].id && link.sink.entity == route.entities[i].id) links.push_back(link);
        if (links.size() != 1) throw std::runtime_error(role + " route link " + names[i - 1] + " -> " + names[i] +
            (links.empty() ? " missing" : " ambiguous pads"));
        route.links.push_back(links.front());
    }
    return route;
}
std::vector<std::vector<std::string>> possible_routes(const Graph &graph, const Entity &sensor, bool enabled_only) {
    std::vector<std::vector<std::string>> result;
    struct Path { std::vector<uint32_t> ids; std::vector<std::string> names; };
    std::vector<Path> pending{{{sensor.id}, {sensor.name}}};
    size_t visited{};
    while (!pending.empty()) {
        if (++visited > 4096) throw std::runtime_error("Media route search exceeds bound; use explicit route profile");
        auto path = std::move(pending.back()); pending.pop_back();
        auto current = std::find_if(graph.entities.begin(), graph.entities.end(), [&](const auto &e) { return e.id == path.ids.back(); });
        if (current == graph.entities.end()) throw std::runtime_error("Broken media graph entity reference");
        if (current->capture) { result.push_back(std::move(path.names)); continue; }
        if (path.ids.size() >= 16) continue;
        for (const auto &link : graph.links) {
            if (link.source.entity != current->id || (enabled_only && !link.enabled) ||
                std::find(path.ids.begin(), path.ids.end(), link.sink.entity) != path.ids.end()) continue;
            auto sink = std::find_if(graph.entities.begin(), graph.entities.end(), [&](const auto &e) { return e.id == link.sink.entity; });
            if (sink == graph.entities.end()) throw std::runtime_error("Broken media graph link reference");
            auto next = path; next.ids.push_back(sink->id); next.names.push_back(sink->name); pending.push_back(std::move(next));
        }
    }
    return result;
}
} // namespace x1
