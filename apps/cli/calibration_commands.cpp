#include "calibration_commands.hpp"
#include <charconv>
#include <cmath>
#include <map>
#include <set>
namespace {
[[noreturn]] void bad(const std::string &message) { throw std::runtime_error(message); }
uint32_t integer(const std::string &v) {
    uint32_t out{}; auto parsed = std::from_chars(v.data(), v.data() + v.size(), out);
    if (parsed.ec != std::errc{} || parsed.ptr != v.data() + v.size() || !out)
        bad("Expected positive uint32 integer: " + v);
    return out;
}
double number(const std::string &v) {
    double out{}; auto parsed = std::from_chars(v.data(), v.data() + v.size(), out);
    if (parsed.ec != std::errc{} || parsed.ptr != v.data() + v.size() || !std::isfinite(out))
        bad("Expected finite decimal number: " + v);
    return out;
}
struct Options {
    std::map<std::string, std::string> single;
    std::vector<std::string> roles, positional;
    bool provenance{};
    std::string required(const char *name) const {
        auto it = single.find(name);
        if (it == single.end()) bad(std::string("Missing required option ") + name);
        return it->second;
    }
    std::string optional(const char *name) const {
        auto it = single.find(name); return it == single.end() ? "" : it->second;
    }
};
Options options(std::span<const std::string> args, const std::set<std::string> &allowed, bool repeated_role = false) {
    Options out;
    std::set<std::string> seen;
    for (size_t i = 0; i < args.size(); ++i) {
        const auto &a = args[i];
        if (!a.starts_with("--")) { out.positional.push_back(a); continue; }
        if (!allowed.contains(a)) bad("Unknown calibration option: " + a);
        if (a != "--role" && !seen.insert(a).second) bad("Duplicate singular option: " + a);
        if (a == "--measurement-provenance") { out.provenance = true; continue; }
        if (++i == args.size() || args[i].starts_with("--")) bad("Missing option argument for " + a);
        if (a == "--role" && repeated_role) {
            if (out.roles.size() >= 64) bad("Calibration role limit is 64");
            if (std::find(out.roles.begin(), out.roles.end(), args[i]) != out.roles.end()) bad("Duplicate camera role");
            out.roles.push_back(args[i]);
        } else out.single.emplace(a, args[i]);
    }
    return out;
}
}
mantis::wire::v1::Request calibration_command(std::span<const std::string> args) {
    using namespace mantis::wire::v1;
    if (args.empty() || args.size() > 1300) bad("Calibration command requires 1..1300 arguments");
    for (const auto &a : args)
        if (a.empty() || a.size() > 4096) bad("Calibration arguments must contain 1..4096 bytes");
    Request r;
    const auto &mode = args[0];
    auto exact = [&](size_t count) { if (args.size() != count) bad("Unexpected or missing calibration argument"); };
    if (mode == "list") { exact(1); r.mutable_calibrations_list(); }
    else if (mode == "info") { exact(2); r.mutable_calibration_info()->set_id(args[1]); }
    else if (mode == "active") { exact(2); r.mutable_calibration_active()->set_id(args[1]); }
    else if (mode == "clear") { exact(2); r.mutable_calibration_clear()->set_id(args[1]); }
    else if (mode == "activate") {
        exact(3); r.mutable_calibration_activate()->set_logical_device_id(args[1]);
        r.mutable_calibration_activate()->set_rig_artifact_id(args[2]);
    } else if (mode == "target") {
        if (args.size() < 3 || args[1] != "create" || (args[2] != "checkerboard" && args[2] != "charuco"))
            bad("Expected calibration target create checkerboard|charuco");
        const bool charuco = args[2] == "charuco";
        std::set<std::string> allowed{"--squares-x", "--squares-y", "--square-mm", "--series", "--measured-width-mm",
            "--measured-height-mm", "--measurement-provenance", "--width-uncertainty-mm", "--height-uncertainty-mm", "--instrument", "--note"};
        if (charuco) allowed.insert({"--marker-mm", "--dictionary", "--layout"});
        auto o = options(args.subspan(3), allowed);
        if (!o.positional.empty()) bad("Unexpected target positional argument");
        auto *request = r.mutable_calibration_target_create(); request->set_series_id(o.optional("--series"));
        auto *t = request->mutable_target();
        t->set_squares_x(integer(o.required("--squares-x"))); t->set_squares_y(integer(o.required("--squares-y")));
        t->set_nominal_square_size_mm(number(o.required("--square-mm")));
        if (charuco) {
            auto *p = t->mutable_charuco(); p->set_nominal_marker_size_mm(number(o.required("--marker-mm")));
            p->set_dictionary(o.required("--dictionary")); auto layout = o.required("--layout");
            if (layout != "black_square_at_origin" && layout != "white_square_at_origin_even_rows") bad("Unknown physical layout");
            p->set_pattern_layout(layout);
        } else t->mutable_checkerboard();
        bool width = o.single.contains("--measured-width-mm"), height = o.single.contains("--measured-height-mm");
        if (width != height) bad("Measured width/height must be supplied together");
        if (width) {
            t->mutable_measurement()->set_active_width_mm(number(o.required("--measured-width-mm")));
            t->mutable_measurement()->set_active_height_mm(number(o.required("--measured-height-mm")));
        }
        bool provenance = o.provenance || o.single.contains("--width-uncertainty-mm") || o.single.contains("--height-uncertainty-mm") ||
                          o.single.contains("--instrument") || o.single.contains("--note");
        if (provenance) {
            if (!width) bad("Measurement provenance requires measured active extents");
            auto *p = t->mutable_measurement()->mutable_provenance();
            if (o.single.contains("--width-uncertainty-mm")) p->set_width_uncertainty_mm(number(o.required("--width-uncertainty-mm")));
            if (o.single.contains("--height-uncertainty-mm")) p->set_height_uncertainty_mm(number(o.required("--height-uncertainty-mm")));
            if (o.single.contains("--instrument")) p->set_instrument(o.required("--instrument"));
            if (o.single.contains("--note")) p->set_note(o.required("--note"));
        }
    } else if (mode == "dataset") {
        if (args.size() < 2 || args[1] != "build") bad("Expected calibration dataset build");
        auto o = options(args.subspan(2), {"--role", "--max-samples", "--series"}, true);
        if (o.positional.size() < 2 || o.positional.size() > 1025 || o.roles.empty()) bad("Dataset requires TARGET, 1..1024 RAW inputs and roles");
        auto *d = r.mutable_calibration_dataset_build(); d->set_target_artifact_id(o.positional[0]);
        std::set<std::string> seen;
        for (size_t i = 1; i < o.positional.size(); ++i) {
            if (!seen.insert(o.positional[i]).second) bad("Duplicate RawCapture artifact");
            d->add_raw_capture_artifact_ids(o.positional[i]);
        }
        for (const auto &role : o.roles) d->add_camera_roles(role);
        d->set_max_selected_per_camera(integer(o.required("--max-samples"))); d->set_series_id(o.optional("--series"));
    } else if (mode == "camera") {
        if (args.size() < 2 || args[1] != "solve") bad("Expected calibration camera solve");
        auto o = options(args.subspan(2), {"--heldout", "--series"});
        if (o.positional.size() != 2) bad("Camera solve requires DATASET ROLE");
        auto *c = r.mutable_calibration_camera_solve(); c->set_dataset_artifact_id(o.positional[0]); c->set_camera_role(o.positional[1]);
        c->set_heldout_per_camera(integer(o.required("--heldout"))); c->set_series_id(o.optional("--series"));
    } else if (mode == "rig") {
        if (args.size() < 2 || args[1] != "solve") bad("Expected calibration rig solve");
        auto o = options(args.subspan(2), {"--heldout", "--rig-frame-id", "--rig-frame-name", "--series"});
        if (o.positional.size() != 3) bad("Rig solve requires DATASET LEFT_CAMERA RIGHT_CAMERA");
        auto *c = r.mutable_calibration_rig_solve(); c->set_dataset_artifact_id(o.positional[0]);
        c->set_left_camera_artifact_id(o.positional[1]); c->set_right_camera_artifact_id(o.positional[2]);
        if (o.positional[1] == o.positional[2]) bad("LEFT and RIGHT camera artifacts must differ");
        c->set_heldout_pairs(integer(o.required("--heldout"))); c->set_rig_frame_id(o.required("--rig-frame-id"));
        c->set_rig_frame_name(o.required("--rig-frame-name")); c->set_series_id(o.optional("--series"));
    } else bad("Unknown calibration command");
    return r;
}
