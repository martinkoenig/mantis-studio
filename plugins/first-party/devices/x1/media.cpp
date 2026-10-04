#include "media.hpp"
#include <algorithm>
#include <set>
#include <stdexcept>
#include <system_error>
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
        for (auto endpoint : {links.front().source, links.front().sink}) {
            const auto &entity = endpoint == links.front().source ? route.entities[i - 1] : route.entities[i];
            auto pad = std::find_if(entity.pads.begin(), entity.pads.end(), [&](const Pad &v) { return v.index == endpoint.pad; });
            if (pad == entity.pads.end() || (endpoint == links.front().source ? !pad->source : !pad->sink))
                throw std::runtime_error(role + " invalid route pad " + entity.name + ":" + std::to_string(endpoint.pad));
        }
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
namespace {
bool same_link(const Link &a, const Link &b) { return a.source == b.source && a.sink == b.sink; }
std::string link_name(const Graph &g, const Link &l) {
    auto name = [&](Endpoint e) {
        auto it = std::find_if(g.entities.begin(), g.entities.end(), [&](const Entity &v) { return v.id == e.entity; });
        if (it == g.entities.end()) throw std::runtime_error("Unknown link entity");
        return it->name + ":" + std::to_string(e.pad);
    };
    return name(l.source) + " -> " + name(l.sink);
}
class Transaction final : public Setup {
    MediaIo &io_;
    struct SavedPad { Entity entity; uint32_t pad; PadFormat format; std::string context; };
    struct SavedControl { Entity entity; int32_t value; std::string context; };
    struct SavedLink { std::string media; Link link; std::string context; };
    std::vector<SavedPad> pads_;
    std::vector<SavedControl> controls_;
    std::vector<SavedLink> links_;
    Metadata diagnostics_;
    bool active_{}, committed_{};
  public:
    explicit Transaction(MediaIo &io) : io_(io) {}
    ~Transaction() override { try { rollback(); } catch (...) {} }
    void commit() override { committed_ = true; }
    Metadata diagnostics() const override { return diagnostics_; }
    void rollback() override {
        if (!active_ || committed_) return;
        std::string failures;
        auto restore = [&](const std::string &context, auto fn) {
            try { fn(); } catch (const std::exception &e) { failures += context + ": " + e.what() + "; "; }
        };
        for (const auto &pad : pads_) restore(pad.context, [&] {
            io_.set_format(pad.entity, pad.pad, pad.format);
            if (io_.get_format(pad.entity, pad.pad) != pad.format) throw std::runtime_error("format rollback read-back mismatch");
        });
        for (const auto &control : controls_) restore(control.context, [&] {
            if (io_.set_vblank(control.entity, control.value) != control.value || io_.get_vblank(control.entity) != control.value)
                throw std::runtime_error("VBLANK rollback read-back mismatch");
        });
        // Remove newly enabled competing routes before restoring old enabled routes.
        for (bool enabled : {false, true}) for (const auto &saved : links_) if (saved.link.enabled == enabled)
            restore(saved.context, [&] {
                auto g = io_.graph(saved.media);
                auto it = std::find_if(g.links.begin(), g.links.end(), [&](const Link &l) { return same_link(l, saved.link); });
                if (it == g.links.end()) throw std::runtime_error("link disappeared during rollback");
                if (it->enabled != enabled) io_.link(saved.media, *it, enabled);
                g = io_.graph(saved.media);
                it = std::find_if(g.links.begin(), g.links.end(), [&](const Link &l) { return same_link(l, saved.link); });
                if (it == g.links.end() || it->enabled != enabled) throw std::runtime_error("link rollback read-back mismatch");
            });
        active_ = false;
        if (!failures.empty()) throw std::runtime_error("Media setup rollback incomplete: " + failures);
    }
    void configure(const Profile &p, const std::array<CameraInfo, 2> &cameras) {
        std::array<Graph, 2> graphs;
        std::array<Route, 2> routes;
        const auto desired_code = io_.format_code(p.media_bus_code);
        std::set<std::pair<std::string, Endpoint>> owned;
        // Inspect both routes and snapshot ALL selected pads before any S_FMT,
        // since a sink-format change can also propagate to a source pad.
        for (size_t i = 0; i < 2; ++i) {
            auto context = camera_context(cameras[i]);
            try {
                graphs[i] = io_.graph(cameras[i].media);
                if (graphs[i].bus != cameras[i].bus) throw std::runtime_error("Media bus changed since discovery");
                routes[i] = select_route(graphs[i], p.routes[i], cameras[i].role);
                if (routes[i].entities.back().node != cameras[i].video) throw std::runtime_error("Capture node changed since discovery");
                std::set<Endpoint> selected;
                for (const auto &l : routes[i].links) { selected.insert(l.source); selected.insert(l.sink); }
                for (const auto &entity : routes[i].entities) {
                    if (entity.capture) continue;
                    if (entity.node.empty()) throw std::runtime_error("Missing subdevice node for " + entity.name);
                    for (const auto &pad : entity.pads) if (selected.contains({entity.id, pad.index})) {
                        if (!owned.insert({graphs[i].path, {entity.id, pad.index}}).second)
                            throw std::runtime_error("Measurement routes share a subdevice pad");
                        auto prefix = context + " " + entity.name + ":" + std::to_string(pad.index) + " (" + entity.node + ")";
                        try {
                            auto codes = io_.codes(entity, pad.index);
                            if (!codes.empty() && std::find(codes.begin(), codes.end(), desired_code) == codes.end())
                                throw std::runtime_error("Requested media-bus code unavailable");
                            pads_.push_back({entity, pad.index, io_.get_format(entity, pad.index), prefix});
                        } catch (const std::exception &e) { throw std::runtime_error(prefix + ": " + e.what()); }
                    }
                }
                if (p.vertical_blanking) controls_.push_back({routes[i].entities.front(), io_.get_vblank(routes[i].entities.front()), context});
                for (const auto &l : graphs[i].links) {
                    bool relevant = std::any_of(routes[i].links.begin(), routes[i].links.end(), [&](const Link &required) {
                        return l.source == required.source || l.sink == required.sink;
                    });
                    if (relevant && !l.immutable && std::none_of(links_.begin(), links_.end(), [&](const SavedLink &saved) {
                            return saved.media == graphs[i].path && same_link(saved.link, l); }))
                        links_.push_back({graphs[i].path, l, context + " " + link_name(graphs[i], l)});
                }
            } catch (const std::exception &e) { throw std::runtime_error(context + " inspect setup: " + e.what()); }
        }
        active_ = true;
        auto set_link = [&](const Graph &g, const Link &l, bool enabled, const std::string &context) {
            try {
                if (l.immutable && l.enabled != enabled) throw std::runtime_error("Immutable link conflicts with requested route");
                if (l.enabled != enabled) io_.link(g.path, l, enabled);
                auto check = io_.graph(g.path);
                auto it = std::find_if(check.links.begin(), check.links.end(), [&](const Link &v) { return same_link(l, v); });
                if (it == check.links.end() || it->enabled != enabled) throw std::runtime_error("Media-link read-back mismatch");
            } catch (const std::exception &e) { throw std::runtime_error(context + " link " + link_name(g, l) + ": " + e.what()); }
        };
        for (size_t i = 0; i < 2; ++i) {
            const auto context = camera_context(cameras[i]);
            for (const auto &required : routes[i].links) {
                auto g = io_.graph(graphs[i].path);
                for (const auto &other : g.links) if (other.enabled && other.sink == required.sink && !same_link(other, required)) {
                    if (!p.disable_conflicting_links || other.immutable)
                        throw std::runtime_error(context + " conflicting incoming link " + link_name(g, other));
                    set_link(g, other, false, context);
                }
                // Do not disable unrelated fan-out merely because it shares a source.
                // A driver EBUSY is required before considering source conflicts.
                auto current = std::find_if(g.links.begin(), g.links.end(), [&](const Link &l) { return same_link(l, required); });
                if (current == g.links.end()) throw std::runtime_error(context + " required link disappeared");
                if (!current->enabled) {
                    try { io_.link(g.path, *current, true); }
                    catch (const std::system_error &e) {
                        if (e.code().value() != EBUSY || !p.disable_conflicting_links)
                            throw std::runtime_error(context + " link " + link_name(g, required) + ": " + e.what());
                        for (const auto &other : g.links) if (other.enabled && other.source == required.source && !same_link(other, required))
                            set_link(g, other, false, context);
                        try { io_.link(g.path, *current, true); }
                        catch (const std::exception &retry) { throw std::runtime_error(context + " link " + link_name(g, required) + ": " + retry.what()); }
                    } catch (const std::exception &e) { throw std::runtime_error(context + " link " + link_name(g, required) + ": " + e.what()); }
                }
                auto verified = required; verified.enabled = true; set_link(g, verified, true, context);
            }
        }
        for (const auto &saved : pads_) {
            auto desired = saved.format; desired.code = desired_code; desired.width = p.mode.width; desired.height = p.mode.height;
            try {
                auto actual = io_.set_format(saved.entity, saved.pad, desired);
                if (actual.code != desired.code || actual.width != desired.width || actual.height != desired.height)
                    throw std::runtime_error("Subdevice S_FMT adjusted requested format");
            } catch (const std::exception &e) { throw std::runtime_error(saved.context + " S_FMT: " + e.what()); }
        }
        for (size_t i = 0; i < controls_.size(); ++i) {
            const auto &control = controls_[i];
            try {
                const auto actual = io_.set_vblank(control.entity, *p.vertical_blanking);
                const auto readback = io_.get_vblank(control.entity);
                if (actual != *p.vertical_blanking || readback != *p.vertical_blanking) throw std::runtime_error("VBLANK read-back mismatch");
                const std::string role = i ? "right_" : "left_";
                diagnostics_[role + "requested_vblank"] = std::to_string(*p.vertical_blanking);
                diagnostics_[role + "readback_vblank"] = std::to_string(readback);
            } catch (const std::exception &e) { throw std::runtime_error(control.context + " VBLANK: " + e.what()); }
        }
        for (const auto &pad : pads_) {
            try {
                auto actual = io_.get_format(pad.entity, pad.pad);
                if (actual.code != desired_code || actual.width != p.mode.width || actual.height != p.mode.height)
                    throw std::runtime_error("Final pad-format verification mismatch");
            } catch (const std::exception &e) { throw std::runtime_error(pad.context + " G_FMT verification: " + e.what()); }
        }
        for (size_t i = 0; i < 2; ++i) {
            const auto context = camera_context(cameras[i]);
            auto graph = io_.graph(graphs[i].path);
            auto route = select_route(graph, p.routes[i], cameras[i].role);
            for (const auto &required : route.links) {
                if (!required.enabled) throw std::runtime_error(context + " final route disabled: " + link_name(graph, required));
                for (const auto &other : graph.links) if (other.enabled && other.sink == required.sink && !same_link(other, required))
                    throw std::runtime_error(context + " final route conflict: " + link_name(graph, other));
            }
        }
        for (size_t i = 0; i < 2; ++i) {
            auto context = camera_context(cameras[i]);
            try {
                auto timing = io_.timing(routes[i].entities.front());
                for (const auto &[key, value] : timing) diagnostics_[(i ? "right_" : "left_") + key] = value;
            } catch (const std::exception &e) { throw std::runtime_error(context + " timing: " + e.what()); }
        }
        diagnostics_["media_setup"] = "plugin-owned selected routes verified";
    }
};
} // namespace
std::unique_ptr<Setup> configure_media(MediaIo &io, const Profile &profile, const std::array<CameraInfo, 2> &cameras) {
    if (profile.format_version == 1) return {};
    auto setup = std::make_unique<Transaction>(io);
    try { setup->configure(profile, cameras); }
    catch (const std::exception &e) {
        std::string error = e.what();
        try { setup->rollback(); } catch (const std::exception &rollback) { error += "; " + std::string(rollback.what()); }
        throw std::runtime_error(error);
    }
    return setup;
}
} // namespace x1
