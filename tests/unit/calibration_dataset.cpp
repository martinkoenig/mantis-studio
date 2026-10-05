#include <mantis/calibration_dataset.hpp> // Self-contained and pure; no detector/storage linkage.
#include <mantis/calibration.hpp>
#include <iostream>

using namespace mantis;
using namespace mantis::calibration;
#define CHECK(x) do { if (!(x)) throw std::runtime_error("Check failed at line " + std::to_string(__LINE__) + ": " #x); } while (false)
CalibrationTarget target(bool charuco = false) {
    CalibrationTarget value;
    value.identity = {{"target.fixture"}, 7}; value.grid = {3, 3, 40};
    if (charuco) value.pattern = CharucoDefinition{"DICT_6X6_250", 25};
    return value;
}
TargetObservation observation(const CalibrationTarget &value, ObservationKey key, double centroid = 0.5) {
    TargetObservation result;
    result.source = {key.frame.raw_capture_id, key.frame.frameset_sequence, key.camera_id, key.camera_role};
    result.target = value.identity; result.target_type = value.type();
    result.image_width = result.image_height = 1000;
    for (uint32_t id = 0; id < 4; ++id) {
        result.point_ids.push_back(id);
        result.image_points_px.push_back({centroid * 1000 + (id % 2 ? 50 : -50), double(id / 2 ? 550 : 450)});
        result.object_points_mm.push_back(*scale_target_point(value, {double(id % 2 + 1) * 40, double(id / 2 + 1) * 40, 0}));
    }
    result.evidence = {4, value.type() == TargetType::charuco ? 4u : 0u, false};
    return result;
}
CalibrationDataset fixture() {
    CalibrationDataset value;
    value.target = target(); value.config = {1, {"left", "right"}, 1, 3};
    value.raw_capture_ids = {{"A"}, {"B"}};
    for (const auto &role : value.config.camera_roles)
        value.cameras.push_back({role, {"camera." + role}, 1000, 1000, {{"optical." + role}, "optical +X right +Y down +Z forward"}});
    const double left[] = {0.1, 0.2, 0.9, 0.5}, right[] = {0.8, 0.9, 0.2, 0.5};
    size_t index{};
    for (const auto &capture : value.raw_capture_ids) for (uint64_t sequence : {17u, 18u, 19u}) {
        if (capture.value == "B" && sequence == 19) continue;
        for (const auto &camera : value.cameras) {
            DatasetObservationRecord record;
            record.key = {{capture, sequence}, camera.role, camera.camera_id};
            if (sequence != 19) {
                record.outcome = DetectionOutcome::detected;
                record.observation = observation(value.target, record.key, camera.role == "left" ? left[index] : right[index]);
                record.diversity = *describe_diversity(value.target, *record.observation);
            }
            value.records.push_back(std::move(record));
        }
        if (sequence != 19) ++index;
    }
    CHECK(validate_calibration_dataset(value));
    return value;
}
void rejects(const CalibrationDataset &value) {
    const auto a = validate_calibration_dataset(value), b = validate_calibration_dataset(value);
    CHECK(!a && !b && a.error().code == Status::invalid_argument && a.error().component == "calibration");
    CHECK(!a.error().message.empty() && a.error().message == b.error().message);
}
template<class Change> void invalid(Change change) { auto value = fixture(); change(value); rejects(value); }
std::vector<SelectedObservation> selected(const CalibrationDataset &value, std::string_view role) {
    std::vector<SelectedObservation> result;
    for (const auto &record : value.records) if (record.key.camera_role == role && record.selection_rank)
        result.push_back({record.key, *record.selection_rank});
    std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) { return a.rank < b.rank; });
    return result;
}
ObservationKey key(std::string capture, uint64_t sequence, std::string role = "left") {
    return {{{std::move(capture)}, sequence}, role, {"camera." + role}};
}
void canonical_and_validation() {
    CHECK(key("A", 17) < key("A", 18)); CHECK(key("A", 99) < key("B", 0));
    CHECK(key("A", 17, "left") < key("A", 17, "right"));
    auto ids = canonical_raw_capture_ids({{"B"}, {"A"}});
    CHECK(ids && *ids == std::vector<Id>({{"A"}, {"B"}}));
    CHECK(!canonical_raw_capture_ids({{"A"}, {"A"}})); CHECK(!canonical_raw_capture_ids({}));
    CHECK(!canonical_raw_capture_ids({{""}}));
    auto config = canonical_analysis_config({1, {"right", "left"}, 1, 3});
    CHECK(config && config->camera_roles == std::vector<std::string>({"left", "right"}));
    for (const auto &roles : {std::vector<std::string>{}, std::vector<std::string>{""}, std::vector<std::string>{"left", "left"}})
        CHECK(!canonical_analysis_config({1, roles, 1, 3}));
    CHECK(!canonical_analysis_config({1, {"left"}, 1, 0}));
    invalid([](auto &d) { std::reverse(d.raw_capture_ids.begin(), d.raw_capture_ids.end()); });
    invalid([](auto &d) { d.raw_capture_ids.push_back({"B"}); });
    invalid([](auto &d) { d.config.camera_roles = {"right", "left"}; });
    invalid([](auto &d) { d.config.camera_roles = {"left", "left"}; });
    invalid([](auto &d) { d.config.schema_version = 2; });
    invalid([](auto &d) { d.config.selection_policy_version = 2; });
    invalid([](auto &d) { d.config.max_selected_per_camera = 0; });
    invalid([](auto &d) { std::swap(d.cameras[0], d.cameras[1]); });
    invalid([](auto &d) { d.cameras[0].camera_id.value.clear(); });
    invalid([](auto &d) { d.cameras[0].image_width = 0; });
    invalid([](auto &d) { d.cameras[0].optical_frame.id.value.clear(); });
    invalid([](auto &d) { d.target.identity.id.value.clear(); });
    invalid([](auto &d) { d.target.grid.squares_x = 1; });
    invalid([](auto &d) { std::swap(d.records[0], d.records[1]); });
    invalid([](auto &d) { d.records.insert(d.records.begin(), d.records[0]); });
    invalid([](auto &d) { d.records[0].key.frame.raw_capture_id = {"unknown"}; });
    invalid([](auto &d) { d.records[0].key.camera_role = "unknown"; });
    invalid([](auto &d) { d.records[0].key.camera_id = {"unknown"}; });
    invalid([](auto &d) { d.records.erase(d.records.begin()); });
    invalid([](auto &d) { d.records[0].observation.reset(); });
    invalid([](auto &d) { d.records[0].diversity.reset(); });
    invalid([](auto &d) { d.records[0].outcome = static_cast<DetectionOutcome>(99); });
    invalid([](auto &d) { d.records[0].outcome = DetectionOutcome::no_target; });
    invalid([](auto &d) { d.records[4].diversity = d.records[0].diversity; });
    invalid([](auto &d) { d.records[4].observation = d.records[0].observation; });
    invalid([](auto &d) { d.records[4].selection_rank = 0; });
    invalid([](auto &d) { d.records[0].observation->target.id = {"another.target"}; });
    invalid([](auto &d) { ++d.records[0].observation->target.revision; });
    invalid([](auto &d) { d.records[0].observation->target_type = TargetType::charuco; });
    invalid([](auto &d) { d.records[0].observation->image_width = 900; });
    invalid([](auto &d) { ++d.records[0].observation->source.frameset_sequence; });
    invalid([](auto &d) { d.records[0].observation->source.camera_role = "LEFT"; });
    invalid([](auto &d) { d.records[0].observation->object_points_mm[0].x_mm += 1; });
    invalid([](auto &d) { d.records[0].diversity->centroid_x += 0.01; });
    invalid([](auto &d) { d.records[0].diversity->visible_fraction = std::numeric_limits<double>::quiet_NaN(); });
    invalid([](auto &d) { d.records[0].selection_rank = 1; }); // Gap at zero.
    invalid([](auto &d) { d.records[0].selection_rank = 0; d.records[2].selection_rank = 0; });
    invalid([](auto &d) { d.config.max_selected_per_camera = 1; d.records[0].selection_rank = 0; d.records[2].selection_rank = 1; });
    auto zero = fixture();
    for (auto &record : zero.records) record = {record.key, DetectionOutcome::no_target, {}, {}, {}};
    CHECK(validate_calibration_dataset(zero) && select_dataset_samples(zero));
    auto summary = summarize_dataset(zero);
    CHECK(summary && summary->cameras.at("left").detected == 0 && summary->cameras.at("left").selected == 0);
    CHECK(summary->cameras.at("left").no_target == 5 && !summary->cameras.at("left").minimum_points);
}
void descriptor_contract() {
    auto value = target(); auto obs = observation(value, key("A", 17));
    obs.image_width = 100; obs.image_height = 200;
    obs.image_points_px = {{10, 20}, {30, 20}, {10, 60}, {30, 60}};
    const auto descriptor = describe_diversity(value, obs);
    CHECK(descriptor);
    const auto fixed = quantize_diversity_descriptor(*descriptor);
    CHECK(fixed && *fixed == FixedDiversityDescriptor({200000, 200000, 200000, 200000, 10000, 10000, 0, 1000000}));
    CHECK(descriptor->visible_fraction == 1);
    CHECK(std::abs(descriptor->variance_x - 0.01) < 1e-15 && std::abs(descriptor->variance_y - 0.01) < 1e-15);
    for (auto *array : {&obs.point_ids}) std::reverse(array->begin(), array->end());
    std::reverse(obs.image_points_px.begin(), obs.image_points_px.end());
    std::reverse(obs.object_points_mm.begin(), obs.object_points_mm.end());
    const auto reordered = describe_diversity(value, obs);
    CHECK(reordered && *reordered == *descriptor); // Canonical point-ID accumulation.
    value = target(true); obs = observation(value, key("A", 17));
    obs.point_ids = {1, 3}; obs.image_points_px = {{30, 20}, {30, 60}};
    obs.object_points_mm = {{80, 40, 0}, {80, 80, 0}}; obs.evidence = {2, 2, true};
    obs.image_width = 100; obs.image_height = 200;
    const auto partial = describe_diversity(value, obs);
    CHECK(partial && partial->visible_fraction == 0.5 && partial->extent_x == 0);
    CHECK(*quantize_diversity_descriptor(*partial) == FixedDiversityDescriptor({300000, 200000, 0, 200000, 0, 10000, 0, 500000}));
    obs.image_points_px = {{10, 20}, {30, 60}};
    CHECK(std::abs(describe_diversity(value, obs)->covariance_xy - 0.01) < 1e-15);
    auto rounding = *descriptor; rounding.centroid_x = 0.0000005; rounding.centroid_y = -0.0000005;
    CHECK((*quantize_diversity_descriptor(rounding))[0] == 1 && (*quantize_diversity_descriptor(rounding))[1] == -1);
    for (double bad : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(), 1e100, -1e100}) {
        auto invalid_descriptor = *descriptor; invalid_descriptor.centroid_x = bad;
        CHECK(!quantize_diversity_descriptor(invalid_descriptor));
    }
    for (double bad : {0.0, -1.0, 1.1}) { auto d = *descriptor; d.visible_fraction = bad; CHECK(!quantize_diversity_descriptor(d)); }
    FixedDiversityDescriptor a{}, b{};
    a[0] = std::numeric_limits<int64_t>::min(); b[0] = std::numeric_limits<int64_t>::max();
    CHECK(!diversity_squared_distance(a, b));
    a = {}; b = {}; a[0] = a[1] = 4'000'000'000;
    CHECK(!diversity_squared_distance(a, b)); // Accumulation overflow, individual squares fit.
    a = {}; a[0] = -3; b[0] = 4;
    CHECK(*diversity_squared_distance(a, b) == 49);
}
void selection_contract() {
    auto value = fixture(); CHECK(select_dataset_samples(value) && validate_calibration_dataset(value));
    const std::vector<SelectedObservation> left{{key("A", 17), 0}, {key("B", 17), 1}, {key("B", 18), 2}};
    const std::vector<SelectedObservation> right{{key("B", 17, "right"), 0}, {key("A", 18, "right"), 1}, {key("B", 18, "right"), 2}};
    CHECK(selected(value, "left") == left && selected(value, "right") == right);
    CHECK(value.records[0].selection_rank == 0 && !value.records[2].selection_rank && value.records[2].observation);
    CHECK(value.records[4].outcome == DetectionOutcome::no_target && !value.records[4].observation);
    for (unsigned pass = 0; pass < 3; ++pass) { CHECK(select_dataset_samples(value)); CHECK(selected(value, "left") == left); CHECK(selected(value, "right") == right); }
    const auto summary = summarize_dataset(value);
    CHECK(summary && summary->framesets_per_capture.at({"A"}) == 3 && summary->framesets_per_capture.at({"B"}) == 2);
    CHECK(summary->cameras.at("left").analyzed == 5 && summary->cameras.at("left").detected == 4);
    CHECK(summary->cameras.at("left").no_target == 1 && summary->cameras.at("left").selected == 3);
    std::vector<DiversityCandidate> candidates;
    for (const auto &record : value.records) if (record.key.camera_role == "left" && record.diversity) candidates.push_back({record.key, *record.diversity});
    CHECK(*select_diverse_observations(candidates, 3) == left);
    std::reverse(candidates.begin(), candidates.end()); CHECK(*select_diverse_observations(candidates, 3) == left);
    for (uint32_t budget : {1u, 2u, 3u, 4u, 9u}) CHECK(select_diverse_observations(candidates, budget)->size() == std::min(size_t(budget), candidates.size()));
    auto all = select_diverse_observations(candidates, 9);
    CHECK(all && (*all)[0].key == key("A", 17) && (*all)[1].key == key("A", 18) && (*all)[2].key == key("B", 17));
    CHECK(!select_diverse_observations(candidates, 0));
    const auto identical = candidates[0].diversity;
    for (auto &candidate : candidates) candidate.diversity = identical;
    const auto tied = select_diverse_observations(candidates, 3);
    CHECK(tied && (*tied)[0].key == key("A", 17) && (*tied)[1].key == key("A", 18) && (*tied)[2].key == key("B", 17));
    candidates.push_back(candidates[0]); CHECK(!select_diverse_observations(candidates, 3));
    candidates.pop_back(); candidates[0].diversity.centroid_x = 10000; candidates[1].diversity.centroid_x = -10000;
    CHECK(!select_diverse_observations(candidates, 2));
    candidates[0].key.camera_role = "right"; CHECK(!select_diverse_observations(candidates, 2));
    auto overflow = fixture();
    overflow.records[0].selection_rank = 0;
    for (size_t i : {size_t{0}, size_t{2}}) {
        overflow.records[i].observation = observation(overflow.target, overflow.records[i].key, i ? -10000 : 10000);
        overflow.records[i].diversity = *describe_diversity(overflow.target, *overflow.records[i].observation);
    }
    CHECK(validate_calibration_dataset(overflow));
    CHECK(!select_dataset_samples(overflow) && overflow.records[0].selection_rank == 0 && !overflow.records[2].selection_rank);
    std::cout << "Policy v1 exact keys: left A/17 B/17 B/18; right B/17 A/18 B/18\n";
}
int main() {
    try {
        canonical_and_validation(); descriptor_contract(); selection_contract();
        std::cout << "Calibration dataset: pure structure, descriptors, checked fixed-point distance and deterministic per-camera selection passed\n";
        return 0;
    } catch (const std::exception &failure) { std::cerr << failure.what() << '\n'; return 1; }
}
