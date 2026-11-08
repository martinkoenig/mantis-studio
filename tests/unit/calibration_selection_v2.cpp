#include <iostream>
#include <mantis/calibration_selection_v2.hpp> // Pure, self-contained model; no detector or Store.
#include <mantis/calibration_solve.hpp>
#include <stdexcept>
#include <tuple>
#include <type_traits>

using namespace mantis;
using namespace mantis::calibration;
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error("Check failed at line " + std::to_string(__LINE__) + ": " #x);          \
    } while (false)

ObservationKey legacy_key(std::string raw, uint64_t sequence, std::string role = "left",
                          std::string camera = "camera.left") {
    return {{{std::move(raw)}, sequence}, std::move(role), {std::move(camera)}};
}
ObservationKeyV2 embedded(const ObservationKey &key) {
    auto result = embed_legacy_key(key);
    CHECK(result);
    return *result;
}
ObservationKeyV2 raw_key(std::string raw, uint64_t sequence, std::string role = "left",
                         std::string camera = "camera.left") {
    return embedded(legacy_key(std::move(raw), sequence, std::move(role), std::move(camera)));
}
LiveAcquisitionIdentityV1 live_identity(uint64_t generation = 1) {
    LiveAcquisitionIdentityV1 value;
    value.generation = generation;
    value.daemon_nonce[0] = 0x80;
    value.acquisition_nonce[15] = 0xff;
    return value;
}
ObservationKeyV2 live_key(LiveAcquisitionIdentityV1 identity, uint64_t sequence = 0) {
    return {{identity, sequence}, "left", {"camera.left"}};
}
DiversityDescriptor descriptor(double x = 0, double y = 0) {
    return {x, y, 0.1, 0.1, 0.0025, 0.0025, 0, 1};
}
template <class T> void invalid(const Result<T> &result, Status status = Status::invalid_argument) {
    CHECK(!result);
    CHECK(result.error().code == status);
    CHECK(result.error().component == "calibration" && !result.error().message.empty());
}
template <class T> T required(Result<T> result) {
    CHECK(result);
    return std::move(*result);
}

void identity_and_embedding() {
    static_assert(std::tuple_size_v<decltype(LiveAcquisitionIdentityV1::daemon_nonce)> == 16);
    static_assert(std::tuple_size_v<decltype(LiveAcquisitionIdentityV1::acquisition_nonce)> == 16);
    static_assert(std::is_same_v<decltype(LiveAcquisitionIdentityV1::daemon_nonce)::value_type, uint8_t>);
    auto identity = live_identity();
    CHECK(validate_live_acquisition_identity(identity));
    identity.generation = UINT64_MAX;
    CHECK(validate_live_acquisition_identity(identity)); // Pure values do not allocate the next generation.
    identity.generation = 0;
    invalid(validate_live_acquisition_identity(identity));
    identity.acquisition_nonce[0] = 1; // Relabeling bytes does not repair an invalid generation.
    invalid(validate_observation_key_v2(live_key(identity)));
    identity.generation = 1;
    for (uint32_t version : {0u, 2u, UINT32_MAX}) {
        identity.identity_schema = version;
        invalid(validate_live_acquisition_identity(identity), Status::incompatible);
        invalid(validate_source_identity_v2(SourceIdentityV2{identity}), Status::incompatible);
        invalid(validate_observation_key_v2(live_key(identity)), Status::incompatible);
    }
    identity = {};
    identity.generation = 1;
    // All-zero bytes have the right width. Neither acceptance nor a new nonce
    // proves CSPRNG allocation, freshness, physical origin or independent data.
    CHECK(validate_live_acquisition_identity(identity));
    CHECK(validate_live_acquisition_identity(identity));
    invalid(validate_source_identity_v2(SourceIdentityV2{RawCaptureIdentity{}}));
    auto key = raw_key("A", 0);
    CHECK(validate_observation_key_v2(key));
    CHECK(validate_observation_source({{"A"}, 0, {"camera.left"}, "left"}));
    CHECK(validate_observation_key_v2(live_key(live_identity(), 0)));
    key.camera_role.clear();
    invalid(validate_observation_key_v2(key));
    key = raw_key("A", 0);
    key.camera_id.value.clear();
    invalid(validate_observation_key_v2(key));
    invalid(embed_legacy_key(legacy_key("", 0)));
    invalid(embed_legacy_key(legacy_key("A", 0, "")));
    invalid(embed_legacy_key(legacy_key("A", 0, "left", "")));

    const auto original = legacy_key("exact.raw.id", UINT64_MAX, "LEFT", "physical.camera");
    const auto result = embedded(original);
    CHECK(std::get<RawCaptureIdentity>(result.frame.source).artifact_id == original.frame.raw_capture_id);
    CHECK(result.frame.parent_sequence == UINT64_MAX && result.camera_role == "LEFT");
    CHECK(result.camera_id == original.camera_id && embedded(original) == result);

    // Exhaustive pair relation over a retained legacy-shaped key population,
    // including sequence zero, high-bit unsigned sequences and UTF-8 prefixes.
    std::vector<ObservationKey> legacy;
    for (const std::string raw : {"A", "AA", "B", "\xc3\xa9"})
        for (uint64_t sequence : {uint64_t{0}, uint64_t{10}, uint64_t{20}, UINT64_MAX})
            for (const std::string role : {"LEFT", "left", "\xc3\xa9", "\xc3\xa9x"})
                for (const std::string camera : {"cam", "cam0", "\xc3\xa9"})
                    legacy.push_back(legacy_key(raw, sequence, role, camera));
    for (const auto &a : legacy) {
        const auto ea = embedded(a);
        for (const auto &b : legacy) {
            const auto eb = embedded(b);
            CHECK((a == b) == (ea == eb));
            CHECK((a < b) == (ea < eb));
        }
    }
}

void ordering_and_goldens() {
    const auto a10 = raw_key("A", 10), a20 = raw_key("A", 20), b1 = raw_key("B", 1);
    std::vector<DiversityCandidateV2> candidates{
        {a10, descriptor(), {}}, {a20, descriptor(), {}}, {b1, descriptor(), {}}};
    const std::vector<SelectedObservationV2> equal{{a10, 0}, {a20, 1}};
    const std::vector<SelectedObservationV2> separated{{a10, 0}, {b1, 1}};
    std::array<unsigned, 3> order{0, 1, 2};
    do {
        std::vector<DiversityCandidateV2> shuffled;
        for (auto i : order)
            shuffled.push_back(candidates[i]);
        CHECK(required(select_diverse_observations_v2(shuffled, 2)) == equal);
        for (auto &candidate : shuffled)
            candidate.diversity.centroid_x = candidate.key == a10 ? 0 : candidate.key == a20 ? 0.5 : 1;
        CHECK(required(select_diverse_observations_v2(shuffled, 2)) == separated);
    } while (std::next_permutation(order.begin(), order.end()));

    // Explicit mixed-kind golden: source kind precedes source bytes/sequence;
    // live identities compare daemon bytes, acquisition bytes, generation, sequence.
    auto acquisition_later = live_identity();
    acquisition_later.acquisition_nonce[0] = 1;
    auto daemon_later = live_identity();
    daemon_later.daemon_nonce[0] = 0xff;
    auto daemon_earlier = live_identity();
    daemon_earlier.daemon_nonce[0] = 0x7f;
    CHECK(live_key(daemon_earlier) < live_key(live_identity()));
    const std::vector<ObservationKeyV2> expected{
        raw_key("A", UINT64_MAX),      raw_key("\xc3\xa9", 0),
        live_key(live_identity(), 0),  live_key(live_identity(), UINT64_MAX),
        live_key(live_identity(2), 0), live_key(live_identity(UINT64_MAX), 0),
        live_key(acquisition_later),   live_key(daemon_later)};
    candidates.clear();
    for (auto it = expected.rbegin(); it != expected.rend(); ++it)
        candidates.push_back({*it, descriptor(), {}});
    for (uint32_t budget : {2u, 8u, 9u}) {
        auto selected = select_diverse_observations_v2(candidates, budget);
        CHECK(selected && selected->size() == std::min(size_t(budget), expected.size()));
        for (size_t i = 0; i < selected->size(); ++i)
            CHECK((*selected)[i] == (SelectedObservationV2{expected[i], static_cast<uint32_t>(i)}));
    }
    // Unsigned byte order and shorter-prefix-first, independent of locale and char signedness.
    const std::vector<std::string> bytes{"A", "a", "az", "\xc3\xa9", "\xc3\xa9x", "\xe2\x82\xac"};
    for (bool roles : {false, true}) {
        std::vector<ObservationKeyV2> keys;
        for (const auto &value : bytes) {
            auto key = a10;
            if (roles)
                key.camera_role = value;
            else
                key.camera_id.value = value;
            CHECK(validate_observation_key_v2(key));
            keys.push_back(key);
        }
        for (size_t i = 0; i < keys.size(); ++i)
            for (size_t j = 0; j < keys.size(); ++j) {
                CHECK((keys[i] < keys[j]) == (i < j));
                CHECK((keys[i] == keys[j]) == (i == j));
            }
    }
    // The centroid seed precedes the canonical key when count exceeds budget;
    // when all fit, descriptor order must not replace key order.
    candidates = {{a10, descriptor(1), {}}, {a20, descriptor(0), {}}};
    CHECK(required(select_diverse_observations_v2(candidates, 1)) ==
          (std::vector<SelectedObservationV2>{{a20, 0}}));
    CHECK(required(select_diverse_observations_v2(candidates, 2)) == equal);
    CHECK(required(select_diverse_observations_v2({}, 1)).empty());
    invalid(select_diverse_observations_v2({}, 0));
    // Maximin must use distance to every selected sample, not just the last.
    candidates = {{a10, descriptor(0), {}},
                  {a20, descriptor(0.25), {}},
                  {b1, descriptor(0.5), {}},
                  {raw_key("C", 1), descriptor(1), {}}};
    const std::vector<SelectedObservationV2> maximin{{a10, 0}, {raw_key("C", 1), 1}, {b1, 2}};
    CHECK(required(select_diverse_observations_v2(candidates, 3)) == maximin);
    // All argument permutations across both raw and live source identities.
    candidates = {{a10, descriptor(0), {}},
                  {b1, descriptor(0.25), {}},
                  {live_key(live_identity()), descriptor(0.5), {}},
                  {live_key(daemon_later), descriptor(1), {}}};
    const std::vector<SelectedObservationV2> mixed_maximin{
        {a10, 0}, {live_key(daemon_later), 1}, {live_key(live_identity()), 2}};
    std::array<unsigned, 4> mixed_order{0, 1, 2, 3};
    do {
        std::vector<DiversityCandidateV2> shuffled;
        for (auto i : mixed_order)
            shuffled.push_back(candidates[i]);
        CHECK(required(select_diverse_observations_v2(shuffled, 3)) == mixed_maximin);
    } while (std::next_permutation(mixed_order.begin(), mixed_order.end()));
    std::cout << "selection2 goldens: equal A/10:0 A/20:1; separated A/10:0 B/1:1; maximin A/10 C/1 B/1\n";
}

CalibrationDataset legacy_fixture() {
    CalibrationDataset value;
    value.target.identity = {{"target.fixture"}, 7};
    value.target.grid = {3, 3, 40};
    value.target.pattern = CharucoDefinition{"DICT_6X6_250", 25};
    value.config = {1, {"left", "right"}, 1, 3};
    value.raw_capture_ids = {{"A"}, {"B"}};
    for (const auto &role : value.config.camera_roles)
        value.cameras.push_back({role,
                                 {"camera." + role},
                                 1000,
                                 1000,
                                 {{"optical." + role}, "optical +X right +Y down +Z forward"}});
    const std::array<double, 4> left{0.1, 0.2, 0.9, 0.5}, right{0.8, 0.9, 0.2, 0.5};
    size_t index{};
    for (const auto &raw : value.raw_capture_ids) {
        for (uint64_t sequence : {17u, 18u, 19u}) {
            if (raw.value == "B" && sequence == 19)
                continue;
            for (const auto &camera : value.cameras) {
                DatasetObservationRecord record;
                record.key = {{raw, sequence}, camera.role, camera.camera_id};
                if (sequence != 19) {
                    record.outcome = DetectionOutcome::detected;
                    TargetObservation observation;
                    observation.source = {raw, sequence, camera.camera_id, camera.role};
                    observation.target = value.target.identity;
                    observation.target_type = value.target.type();
                    observation.image_width = observation.image_height = 1000;
                    const double center = camera.role == "left" ? left[index] : right[index];
                    for (uint32_t id = 0; id < 4; ++id) {
                        observation.point_ids.push_back(id);
                        observation.image_points_px.push_back(
                            {center * 1000 + (id % 2 ? 50 : -50), double(id / 2 ? 550 : 450)});
                        observation.object_points_mm.push_back(
                            {double(id % 2 + 1) * 40, double(id / 2 + 1) * 40, 0});
                    }
                    observation.evidence = {4, 4, false};
                    auto diversity = describe_diversity(value.target, observation);
                    CHECK(diversity);
                    record.observation = std::move(observation);
                    record.diversity = *diversity;
                }
                value.records.push_back(std::move(record));
            }
            if (sequence != 19)
                ++index;
        }
    }
    CHECK(validate_calibration_dataset(value));
    return value;
}

void legacy_selection_and_population() {
    auto baseline = legacy_fixture();
    auto projected = baseline;
    CHECK(select_dataset_samples(baseline));
    std::vector<DiversityCandidateV2> candidates;
    for (const auto &record : projected.records)
        if (record.diversity)
            candidates.push_back({embedded(record.key), *record.diversity, 99});
    std::reverse(candidates.begin(), candidates.end());
    const auto original_candidates = candidates;
    CHECK(rank_diverse_observations_v2(candidates, 3));
    std::map<ObservationKeyV2, std::optional<uint32_t>> ranks;
    for (size_t i = 0; i < candidates.size(); ++i) {
        CHECK(candidates[i].key == original_candidates[i].key);
        CHECK(candidates[i].diversity == original_candidates[i].diversity);
        ranks.emplace(candidates[i].key, candidates[i].selection_rank);
    }
    CHECK(projected.records.size() == 10 && candidates.size() == 8);
    for (size_t i = 0; i < projected.records.size(); ++i) {
        auto &record = projected.records[i];
        if (record.diversity)
            record.selection_rank = ranks.at(embedded(record.key));
        CHECK(record.selection_rank == baseline.records[i].selection_rank);
    }
    CHECK(validate_calibration_dataset(projected));
    const std::array<std::vector<SelectedObservationV2>, 2> expected{
        {{{raw_key("A", 17), 0}, {raw_key("B", 17), 1}, {raw_key("B", 18), 2}},
         {{raw_key("B", 17, "right", "camera.right"), 0},
          {raw_key("A", 18, "right", "camera.right"), 1},
          {raw_key("B", 18, "right", "camera.right"), 2}}}};
    for (size_t i = 0; i < baseline.cameras.size(); ++i) {
        std::vector<DiversityCandidate> legacy;
        std::vector<DiversityCandidateV2> v2;
        for (const auto &record : baseline.records)
            if (record.key.camera_role == baseline.cameras[i].role && record.diversity) {
                legacy.push_back({record.key, *record.diversity});
                v2.push_back({embedded(record.key), *record.diversity, {}});
            }
        for (uint32_t budget : {1u, 2u, 3u, 4u, 9u}) {
            const auto old_selected = select_diverse_observations(legacy, budget);
            const auto new_selected = select_diverse_observations_v2(v2, budget);
            CHECK(old_selected && new_selected && old_selected->size() == new_selected->size());
            for (size_t j = 0; j < old_selected->size(); ++j)
                CHECK((*new_selected)[j] ==
                      (SelectedObservationV2{embedded((*old_selected)[j].key), (*old_selected)[j].rank}));
            if (budget == 3)
                CHECK(*new_selected == expected[i]);
            std::reverse(legacy.begin(), legacy.end());
            std::reverse(v2.begin(), v2.end());
        }
        MonoSolveConfig config;
        config.heldout_per_camera = 1;
        const auto old_partition = partition_mono_samples(baseline, baseline.cameras[i], config);
        const auto new_partition = partition_mono_samples(projected, projected.cameras[i], config);
        CHECK(old_partition && new_partition && old_partition->samples == new_partition->samples);
        CHECK(new_partition->samples.size() == 3);
        for (size_t j = 0; j < 3; ++j) {
            const auto &sample = new_partition->samples[j];
            CHECK(embedded(sample.key) == expected[i][j].key && sample.selection_rank == j);
            CHECK(sample.disposition ==
                  (j == 1 ? SolveSampleDisposition::held_out : SolveSampleDisposition::training));
        }
    }
    // Apply only ranks back to the exact legacy population to check the unchanged
    // M4 seam. This is not a v2 solver: Stage A supplies no new solver/document API.
    StereoSolveConfig stereo;
    stereo.left_role = "left";
    stereo.right_role = "right";
    stereo.heldout_pairs = 1;
    stereo.rig_frame = {{"rig.fixture"}, "rig"};
    const auto old_partition = partition_stereo_samples(baseline, stereo);
    const auto new_partition = partition_stereo_samples(projected, stereo);
    CHECK(old_partition && new_partition && old_partition->samples == new_partition->samples);
    const std::array<FrameSetKey, 4> union_keys{{{{"B"}, 17}, {{"A"}, 17}, {{"A"}, 18}, {{"B"}, 18}}};
    CHECK(new_partition->samples.size() == union_keys.size());
    for (size_t i = 0; i < union_keys.size(); ++i) {
        CHECK(new_partition->samples[i].key == union_keys[i]);
        CHECK(new_partition->samples[i].common_point_ids == (std::vector<uint32_t>{0, 1, 2, 3}));
        CHECK(new_partition->samples[i].disposition ==
              (i == 1 ? SolveSampleDisposition::held_out : SolveSampleDisposition::training));
    }
    CHECK(!projected.records[1].selection_rank && projected.records[1].observation); // A17/right retained.
    CHECK(!projected.records[2].selection_rank && projected.records[2].observation); // A18/left retained.
    CHECK(projected.records[4].outcome == DetectionOutcome::no_target && !projected.records[4].observation);
    std::cout << "legacy embedding: left A/17:0 B/17:1 B/18:2; right B/17:0 A/18:1 B/18:2; stereo B/17 A/17 "
                 "A/18 B/18\n";
}

void numerical_boundaries() {
    auto d = descriptor();
    d.centroid_x = 0.0000005;
    d.centroid_y = d.covariance_xy = -0.0000005;
    const FixedDiversityDescriptor halfway{1, -1, 100000, 100000, 2500, 2500, -1, 1000000};
    CHECK(required(quantize_diversity_descriptor(d)) == halfway);
    d.centroid_x = std::nextafter(0.0000005, 0.0);
    CHECK(required(quantize_diversity_descriptor(d))[0] == 0);
    d.centroid_x = std::nextafter(0.0000005, 1.0);
    CHECK(required(quantize_diversity_descriptor(d))[0] == 1);
    const double bound = 0x1p63 / 1'000'000.0;
    d.centroid_x = -bound;
    CHECK(required(quantize_diversity_descriptor(d))[0] == INT64_MIN);
    d.centroid_x = bound;
    invalid(quantize_diversity_descriptor(d));
    d.centroid_x = std::nextafter(bound, 0.0);
    CHECK(required(quantize_diversity_descriptor(d))[0] == INT64_C(9223372036854773760));
    d.centroid_x = std::nextafter(-bound, -std::numeric_limits<double>::infinity());
    invalid(quantize_diversity_descriptor(d));
    const auto a = raw_key("A", 10), b = raw_key("B", 1);
    std::vector<DiversityCandidateV2> near_rounding{{a, descriptor(std::nextafter(0.0000005, 1.0)), {}},
                                                    {b, descriptor(std::nextafter(0.0000005, 0.0)), {}}};
    CHECK(required(select_diverse_observations_v2(near_rounding, 1)) ==
          (std::vector<SelectedObservationV2>{{b, 0}}));
    const std::array<double DiversityDescriptor::*, 8> members{
        &DiversityDescriptor::centroid_x,    &DiversityDescriptor::centroid_y,
        &DiversityDescriptor::extent_x,      &DiversityDescriptor::extent_y,
        &DiversityDescriptor::variance_x,    &DiversityDescriptor::variance_y,
        &DiversityDescriptor::covariance_xy, &DiversityDescriptor::visible_fraction};
    for (auto member : members)
        for (double bad : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
                           -std::numeric_limits<double>::infinity(), 1e100, -1e100}) {
            d = descriptor();
            d.*member = bad;
            invalid(select_diverse_observations_v2({{a, d, {}}}, 1)); // Also validate when all fit.
        }
    for (auto member : {&DiversityDescriptor::extent_x, &DiversityDescriptor::extent_y,
                        &DiversityDescriptor::variance_x, &DiversityDescriptor::variance_y}) {
        d = descriptor();
        d.*member = -0.00000001;
        invalid(select_diverse_observations_v2({{a, d, {}}}, 1));
        d.*member = 0;
        CHECK(select_diverse_observations_v2({{a, d, {}}}, 1));
    }
    for (double bad : {0.0, -1.0, std::nextafter(1.0, 2.0)}) {
        d = descriptor();
        d.visible_fraction = bad;
        invalid(select_diverse_observations_v2({{a, d, {}}}, 1));
    }
    d = descriptor();
    d.visible_fraction = std::numeric_limits<double>::denorm_min();
    CHECK(required(quantize_diversity_descriptor(d))[7] == 0); // No invented minimum coverage threshold.
    FixedDiversityDescriptor x{}, y{};
    x[0] = 4'294'967'295;
    CHECK(required(diversity_squared_distance(x, y)) == UINT64_C(18446744065119617025));
    ++x[0];
    invalid(diversity_squared_distance(x, y));
    x[0] = x[1] = 3'000'000'000;
    CHECK(required(diversity_squared_distance(x, y)) == UINT64_C(18000000000000000000));
    x[0] = x[1] = 4'000'000'000;
    auto sum = diversity_squared_distance(x, y);
    CHECK(!sum); // Sensitivity oracle: each square fits; their sum must fail explicitly.
    invalid(sum);
    CHECK(sum.error().message.find("accumulation") != std::string::npos);
    x = {};
    x[0] = INT64_MIN;
    y[0] = INT64_MAX;
    invalid(diversity_squared_distance(x, y));
    x[0] = -3;
    y[0] = 4;
    CHECK(required(diversity_squared_distance(x, y)) == 49);
}

void isolation_and_transactional_failure() {
    const auto a = raw_key("A", 10), b = raw_key("A", 20), c = raw_key("B", 1);
    std::vector<DiversityCandidateV2> one{
        {a, descriptor(), 7}, {b, descriptor(0.5), 8}, {c, descriptor(1), 9}};
    auto duplicate = one;
    duplicate.push_back({a, descriptor(0.9), {}});
    invalid(select_diverse_observations_v2(duplicate, 9));
    const auto duplicate_before = duplicate;
    invalid(rank_diverse_observations_v2(duplicate, 2));
    CHECK(duplicate == duplicate_before);
    auto live = live_key(live_identity());
    invalid(select_diverse_observations_v2({{live, descriptor(), {}}, {live, descriptor(1), {}}}, 2));
    std::get<LiveAcquisitionIdentityV1>(live.frame.source).identity_schema = 99;
    invalid(select_diverse_observations_v2({{live, descriptor(), {}}}, 1), Status::incompatible);

    for (bool role : {false, true}) {
        auto mixed = one;
        if (role)
            mixed[1].key.camera_role = "right";
        else
            mixed[1].key.camera_id = {"different.physical.camera"};
        invalid(select_diverse_observations_v2(mixed, 9));
        CHECK(rank_diverse_observations_v2(mixed, 1));
        CHECK(mixed[0].selection_rank == 0 && mixed[1].selection_rank == 0 && !mixed[2].selection_rank);
    }
    std::vector<DiversityCandidateV2> both = one;
    for (const auto &candidate : one) {
        auto right = candidate;
        right.key.camera_role = "right";
        right.key.camera_id = {"camera.right"};
        right.diversity.centroid_x = 1 - right.diversity.centroid_x;
        both.push_back(right);
    }
    const auto original = both;
    CHECK(rank_diverse_observations_v2(both, 2));
    CHECK(both[0].selection_rank == 0 && !both[1].selection_rank && both[2].selection_rank == 1);
    CHECK(both[3].selection_rank == 1 && !both[4].selection_rank && both[5].selection_rank == 0);
    for (size_t i = 0; i < both.size(); ++i)
        CHECK(both[i].key == original[i].key && both[i].diversity == original[i].diversity);
    auto reversed = original;
    std::reverse(reversed.begin(), reversed.end());
    CHECK(rank_diverse_observations_v2(reversed, 2));
    std::reverse(reversed.begin(), reversed.end());
    CHECK(reversed == both);

    // Left succeeds first; right fails after tentative seed selection. Every
    // original rank in both groups must survive, not just the failing group's.
    for (bool accumulation : {false, true}) {
        auto overflow = original;
        overflow[4].diversity.centroid_x = accumulation ? 4000 : 4294.967296;
        overflow[4].diversity.centroid_y = accumulation ? 4000 : 0;
        overflow[3].diversity = descriptor();
        const auto before = overflow;
        auto result = rank_diverse_observations_v2(overflow, 2);
        invalid(result);
        CHECK(result.error().message.find(accumulation ? "accumulation" : "squared distance") !=
              std::string::npos);
        CHECK(overflow == before);
        CHECK(!select_diverse_observations_v2({overflow[3], overflow[4], overflow[5]}, 2));
        // Legacy selects all without calculating unused distances when all fit.
        CHECK(rank_diverse_observations_v2(overflow, 3));
    }
    for (unsigned failure = 0; failure < 5; ++failure) {
        auto bad = original;
        if (failure == 0)
            bad[4].key.camera_id.value.clear();
        if (failure == 1)
            bad[4].key.frame.source = RawCaptureIdentity{};
        if (failure == 2)
            bad[4].key = live_key(live_identity(0));
        if (failure == 3)
            bad[4].diversity.visible_fraction = 0;
        if (failure == 4)
            bad[4].key = bad[3].key;
        const auto before = bad;
        invalid(rank_diverse_observations_v2(bad, 2));
        CHECK(bad == before);
    }
    auto unchanged = original;
    invalid(rank_diverse_observations_v2(unchanged, 0));
    CHECK(unchanged == original);
    std::cout << "selection2 negatives: identity/version/duplicate/camera/finite/range; square and sum "
                 "overflow; all-camera rollback\n";
}

int main() {
    try {
        identity_and_embedding();
        ordering_and_goldens();
        legacy_selection_and_population();
        numerical_boundaries();
        isolation_and_transactional_failure();
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
