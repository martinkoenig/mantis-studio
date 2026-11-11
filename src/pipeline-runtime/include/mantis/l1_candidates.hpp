#pragma once
#include <mantis/pipeline_api.hpp>

namespace mantis::pipeline {
// Internal CPU node configuration, not a Plugin SDK ABI or service DTO. The caller
// resolves immutable raw artifacts and explicitly declares the single-line emitter.
struct L1DarkReference {
    std::shared_ptr<const data::AcquisitionBundle> bundle;
    data::ContentReference raw_input;
};
// Optional ALG-02 local quadratic interpolation. Integer DN thresholds avoid
// nonfinite configuration; these are numerical conditioning gates, not SNR or
// physical accuracy estimates. See docs/algorithms/l1-subpixel.md.
struct L1SubpixelConfig {
    uint16_t minimum_neighbor_signal{4};
    uint16_t minimum_curvature{2}; // 2*center-left-right, DN per pixel squared
};
struct L1CandidateConfig {
    data::RunId run;
    data::ComponentId camera, emitter;
    data::StreamIdentity stream;
    std::string camera_role;
    data::PatternId pattern;
    uint64_t pattern_revision{};
    data::LineId line;
    data::ProducerStreamId producer_stream;
    data::GenerationId producer_generation;
    data::ContentReference raw_input;
    std::optional<data::ObservationOrigin> origin;
    spatial::CoordinateFrame optical_frame;
    // Original-source pixels; omitted ROI means the complete image.
    struct Roi {
        uint32_t x{}, y{}, width{}, height{};
    };
    std::optional<Roi> roi;
    // Native DN units (0..255 RAW8, 0..1023 Y10P), no floating point thresholds.
    uint16_t minimum_signal{32}, background_level{}, ambiguity_margin{4};
    uint16_t saturation_level{255};
    uint32_t minimum_width{1}, maximum_width{16};
    uint32_t maximum_dimension{8192};
    uint64_t maximum_pixels{32ull * 1024 * 1024};
    std::optional<L1DarkReference> dark;
    // Absent preserves ALG-01 1.0.0 selection/parameter identity. Present selects
    // the separately identified org.mantis.l1-subpixel 1.0.0 algorithm.
    std::optional<L1SubpixelConfig> subpixel;
};
namespace l1 {
inline constexpr std::string_view reason = "org.mantis.l1.reason";
inline constexpr std::string_view peak = "org.mantis.l1.peak_dn";
inline constexpr std::string_view width = "org.mantis.l1.profile_width";
inline constexpr uint32_t saturated = 1, competing_peaks = 2, width_rejected = 4;
inline constexpr uint32_t insufficient_neighbors = 8, flat_peak = 16, weak_curvature = 32,
                          weak_neighbors = 64, edge_profile = 128, numeric_rejected = 256,
                          multiple_local_peaks = 512;
inline constexpr std::string_view refinement_valid = "org.mantis.l1.refinement_valid";
inline constexpr std::string_view curvature = "org.mantis.l1.curvature_dn_per_pixel2";
// Packet flags: missing signal is not proof of physical occlusion.
inline constexpr uint32_t missing_profiles = 1, rejected_profiles = 2, attribution_unresolved = 4,
                          saturation_observed = 8;
} // namespace l1
// Throws Failure for invalid configuration. Each instance processes serial calls;
// separate instances may run concurrently. Calls retain no input publication;
// the optional immutable dark reference lives with the configuration.
SemanticNode l1_candidate_extractor(L1CandidateConfig);
} // namespace mantis::pipeline
