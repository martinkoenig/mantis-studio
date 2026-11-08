#pragma once
#include <array>
#include <compare>
#include <mantis/calibration_dataset.hpp>
#include <span>
#include <string_view>
#include <variant>

namespace mantis::calibration {
// ADR-044 L02. Fixed-width values only: allocation, CSPRNG freshness and monotonic
// generation across operations must be established by the daemon, not this validator.
struct LiveAcquisitionIdentityV1 {
    uint32_t identity_schema{1};
    std::array<uint8_t, 16> daemon_nonce{}, acquisition_nonce{};
    uint64_t generation{};
    auto operator<=>(const LiveAcquisitionIdentityV1 &) const = default;
};
struct RawCaptureIdentity {
    Id artifact_id;
    auto operator<=>(const RawCaptureIdentity &) const = default;
};
// Variant order is the source-kind order: raw_capture=0, live_observation=1.
// A separate writable kind tag could disagree with the actual identity.
using SourceIdentityV2 = std::variant<RawCaptureIdentity, LiveAcquisitionIdentityV1>;
struct FrameSetKeyV2 {
    SourceIdentityV2 source;
    uint64_t parent_sequence{}; // Zero retains its legacy meaning; it is not a missing-value sentinel.
    auto operator<=>(const FrameSetKeyV2 &) const = default;
};
namespace selection_v2_detail {
inline std::strong_ordering compare_bytes(std::string_view a, std::string_view b) {
    for (size_t i = 0; i < std::min(a.size(), b.size()); ++i) {
        const auto order = static_cast<unsigned char>(a[i]) <=> static_cast<unsigned char>(b[i]);
        if (order != 0)
            return order;
    }
    return a.size() <=> b.size();
}
} // namespace selection_v2_detail
struct ObservationKeyV2 {
    FrameSetKeyV2 frame;
    std::string camera_role;
    Id camera_id;
    bool operator==(const ObservationKeyV2 &) const = default;
    std::strong_ordering operator<=>(const ObservationKeyV2 &other) const {
        if (const auto order = frame <=> other.frame; order != 0)
            return order;
        if (const auto order = selection_v2_detail::compare_bytes(camera_role, other.camera_role); order != 0)
            return order;
        return selection_v2_detail::compare_bytes(camera_id.value, other.camera_id.value);
    }
};
inline Result<void> validate_live_acquisition_identity(const LiveAcquisitionIdentityV1 &identity) {
    if (identity.identity_schema != 1)
        return std::unexpected(
            Error{Status::incompatible, "Unsupported live acquisition identity version", "calibration"});
    if (!identity.generation)
        return detail::target_error("Live acquisition generation must be positive");
    return {};
}
inline Result<void> validate_source_identity_v2(const SourceIdentityV2 &identity) {
    if (identity.valueless_by_exception())
        return detail::target_error("Source identity is unavailable");
    if (const auto *raw = std::get_if<RawCaptureIdentity>(&identity)) {
        if (raw->artifact_id.value.empty())
            return detail::target_error("RawCapture source identity must be nonempty");
        return {};
    }
    return validate_live_acquisition_identity(std::get<LiveAcquisitionIdentityV1>(identity));
}
inline Result<void> validate_frameset_key_v2(const FrameSetKeyV2 &key) {
    return validate_source_identity_v2(key.source);
}
inline Result<void> validate_observation_key_v2(const ObservationKeyV2 &key) {
    auto valid = validate_frameset_key_v2(key.frame);
    if (!valid)
        return valid;
    if (key.camera_role.empty() || key.camera_id.value.empty())
        return detail::target_error(
            "Observation key requires exact camera role and physical camera identity");
    return {};
}
// E is exact and order-preserving, without changing or relaxing the legacy validator.
inline Result<FrameSetKeyV2> embed_legacy_key(const FrameSetKey &key) {
    FrameSetKeyV2 embedded{RawCaptureIdentity{key.raw_capture_id}, key.frameset_sequence};
    auto valid = validate_frameset_key_v2(embedded);
    if (!valid)
        return std::unexpected(valid.error());
    return embedded;
}
inline Result<ObservationKeyV2> embed_legacy_key(const ObservationKey &key) {
    auto frame = embed_legacy_key(key.frame);
    if (!frame)
        return std::unexpected(frame.error());
    ObservationKeyV2 embedded{*frame, key.camera_role, key.camera_id};
    auto valid = validate_observation_key_v2(embedded);
    if (!valid)
        return std::unexpected(valid.error());
    return embedded;
}
// A projection of successful observations, not a source or Dataset2 document.
// Callers retain the complete observation/no-target population. Selection never
// removes candidates or changes their keys/descriptors, including unselected ones.
struct DiversityCandidateV2 {
    ObservationKeyV2 key;
    DiversityDescriptor diversity;
    std::optional<uint32_t> selection_rank;
    bool operator==(const DiversityCandidateV2 &) const = default;
};
struct SelectedObservationV2 {
    ObservationKeyV2 key;
    uint32_t rank{};
    bool operator==(const SelectedObservationV2 &) const = default;
};
// Selection2 changes only the key space. Descriptor quantization and checked
// distance arithmetic are the existing policy1 functions. Existing ranks are outputs.
inline Result<std::vector<SelectedObservationV2>>
select_diverse_observations_v2(std::vector<DiversityCandidateV2> candidates, uint32_t budget) {
    if (!budget)
        return detail::target_error("Calibration sample budget must be positive");
    for (const auto &candidate : candidates) {
        auto valid = validate_observation_key_v2(candidate.key);
        if (!valid)
            return std::unexpected(valid.error());
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const auto &a, const auto &b) { return a.key < b.key; });
    std::vector<FixedDiversityDescriptor> fixed;
    fixed.reserve(candidates.size());
    for (size_t i = 0; i < candidates.size(); ++i) {
        const auto &key = candidates[i].key;
        if (i && candidates[i - 1].key == key)
            return detail::target_error("Diversity candidate keys must be unique");
        if (i && (key.camera_role != candidates[0].key.camera_role ||
                  key.camera_id != candidates[0].key.camera_id))
            return detail::target_error("Diversity selection requires one physical camera and role");
        auto descriptor = quantize_diversity_descriptor(candidates[i].diversity);
        if (!descriptor)
            return std::unexpected(descriptor.error());
        fixed.push_back(*descriptor);
    }
    std::vector<SelectedObservationV2> selected;
    if (candidates.size() <= budget) {
        for (const auto &candidate : candidates)
            selected.push_back({candidate.key, static_cast<uint32_t>(selected.size())});
        return selected;
    }
    size_t next{};
    for (size_t i = 1; i < fixed.size(); ++i)
        if (fixed[i] < fixed[next])
            next = i;
    std::vector<bool> used(candidates.size());
    std::vector<uint64_t> minimum(candidates.size(), std::numeric_limits<uint64_t>::max());
    while (selected.size() < budget) {
        used[next] = true;
        selected.push_back({candidates[next].key, static_cast<uint32_t>(selected.size())});
        if (selected.size() == budget)
            break;
        const auto last = next;
        std::optional<size_t> best;
        for (size_t i = 0; i < candidates.size(); ++i) {
            if (used[i])
                continue;
            auto distance = diversity_squared_distance(fixed[i], fixed[last]);
            if (!distance)
                return std::unexpected(distance.error());
            minimum[i] = std::min(minimum[i], *distance);
            // Canonical key order plus strict comparison resolves equal distances.
            if (!best || minimum[i] > minimum[*best])
                best = i;
        }
        next = *best;
    }
    return selected;
}
// Rank each exact (role, physical-camera) group independently. Commit only after
// every group succeeds; any invalid key/duplicate/descriptor/overflow leaves all
// existing ranks intact, even when an earlier camera group has already succeeded.
inline Result<void> rank_diverse_observations_v2(std::span<DiversityCandidateV2> candidates,
                                                 uint32_t budget) {
    if (!budget)
        return detail::target_error("Calibration sample budget must be positive");
    std::map<std::pair<std::string, Id>, std::vector<DiversityCandidateV2>> groups;
    for (const auto &candidate : candidates) {
        auto valid = validate_observation_key_v2(candidate.key);
        if (!valid)
            return valid;
        groups[{candidate.key.camera_role, candidate.key.camera_id}].push_back(candidate);
    }
    std::map<ObservationKeyV2, uint32_t> ranks;
    for (auto &[camera, group] : groups) {
        (void)camera;
        auto selected = select_diverse_observations_v2(std::move(group), budget);
        if (!selected)
            return std::unexpected(selected.error());
        for (const auto &value : *selected)
            ranks.emplace(value.key, value.rank);
    }
    for (auto &candidate : candidates) {
        const auto found = ranks.find(candidate.key);
        candidate.selection_rank =
            found == ranks.end() ? std::nullopt : std::optional<uint32_t>{found->second};
    }
    return {};
}
} // namespace mantis::calibration
