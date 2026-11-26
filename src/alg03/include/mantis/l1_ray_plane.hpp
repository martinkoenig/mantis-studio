#pragma once
#include <mantis/calibration_artifacts.hpp>
#include <mantis/pipeline_runtime.hpp>

// Internal, explicitly constructed CPU reference; not a Client/Plugin SDK API.
namespace mantis::alg03 {
inline const artifact::ArtifactType plane_type{"org.mantis.Alg03DeclaredPlane", 1},
    policy_type{"org.mantis.Alg03Policy", 1}, result_type{"org.mantis.Alg03Result", 1};
inline constexpr uint64_t max_samples = 65536, max_source_bytes = 128ull << 20,
                          max_result_bytes = 16ull << 20, max_geometry_bytes = 64ull << 20;
inline constexpr std::string_view prefix = "org.mantis.alg03.",
                                  document_attribute = "org.mantis.alg03.document",
                                  manifest_attribute = "org.mantis.alg03.manifest";
enum class Side { two_sided, camera_on_positive_side };
enum class Selection { recorded_effective, explicit_processing };
enum class SourceContract { alg02_subpixel_1, synthetic_reference_1 };
enum class Uncertainty { numeric_only, require_declared_bound };
struct PlaneBounds {
    double normal_l2{}, offset_abs_mm{};
    std::optional<data::ContentReference> evidence;
};
struct RayBounds {
    double origin_l2_mm{}, direction_l2{};
    std::optional<data::ContentReference> evidence;
};
struct CameraBinding {
    data::ComponentId component;
    calibration::DatasetCamera camera;
    data::ExactCalibrationReference calibration;
};
struct Plane {
    data::ExactCalibrationReference rig;
    spatial::CoordinateFrame frame;
    std::array<double, 3> normal_xyz{};
    double offset_mm{};
    data::LineIdentity line;
    std::vector<CameraBinding> cameras;
    Side side{Side::camera_on_positive_side};
    data::ObservationOrigin origin{data::ObservationOrigin::synthetic};
    std::optional<PlaneBounds> parameter_bounds;
    data::ImplementationIdentity producer;
};
struct Policy {
    SourceContract source_contract{SourceContract::alg02_subpixel_1};
    struct Domain {
        double a{.5}, b{.5};
    } domain;
    double min_incidence_cosine{.01}, z_min_mm{100}, z_max_mm{2000}, max_abs_position_mm{2000},
        max_numeric_error_mm{2e-8};
    Uncertainty uncertainty_requirement{Uncertainty::numeric_only};
    std::optional<RayBounds> declared_ray_bounds;
    std::optional<double> max_declared_model_error_mm;
    uint32_t max_samples{static_cast<uint32_t>(alg03::max_samples)}, max_dependency_nodes{4096};
    uint64_t max_source_bytes{alg03::max_source_bytes}, max_result_bytes{alg03::max_result_bytes},
        max_geometry_bytes{alg03::max_geometry_bytes};
    data::ImplementationIdentity producer;
};
struct Bindings {
    data::ContentReference observation, plane, policy;
    data::ExactCalibrationReference camera, rig;
    Selection calibration_selection{Selection::recorded_effective};
};
enum Reason : uint32_t {
    producer_rejected = 1u << 0,
    ambiguous = 1u << 1,
    unresolved_attribution = 1u << 2,
    line_mismatch = 1u << 3,
    image_fov = 1u << 4,
    inverse_failure = 1u << 5,
    inverse_uncertified = 1u << 6,
    parallel = 1u << 7,
    camera_on_plane = 1u << 8,
    near_parallel = 1u << 9,
    backside = 1u << 10,
    behind_camera = 1u << 11,
    depth_or_position_domain = 1u << 12,
    numeric_guard = 1u << 13,
    declared_bound_unavailable_or_exceeded = 1u << 14
};
// Diagnostics: inverse residual, inverse bound, q, c, tau, depth, arithmetic
// radius, declared-model radius, source-quantization radius, in contract order.
struct Sample {
    uint32_t reasons{}, diagnostic_valid{};
    std::array<double, 3> position{};
    std::array<double, 9> diagnostics{};
};
Result<void> validate(const Plane &);
Result<void> validate(const Policy &);
Result<double> certify_domain(const calibration::PinholeBrown5 &, const Policy::Domain &);
// Same bounded mathematical path used by the production node. No storage/hardware.
Sample intersect(const calibration::PinholeBrown5 &, const spatial::Transform &, const Plane &,
                 const Policy &, float u, float v, const CancellationToken & = {});
Result<std::string> encode_document(const Plane &);
Result<std::string> encode_document(const Policy &);
Result<data::ContentReference> create_plane(artifact::Store &, const Plane &, const CancellationToken & = {});
Result<data::ContentReference> create_policy(artifact::Store &, const Policy &,
                                             const CancellationToken & = {});
Result<Plane> load_plane(const artifact::Store &, const data::ContentReference &,
                         const CancellationToken & = {});
Result<Policy> load_policy(const artifact::Store &, const data::ContentReference &,
                           const CancellationToken & = {});

class Prepared {
  public:
    Prepared(const Prepared &) = default;
    Prepared(Prepared &&) noexcept = default;
    Prepared &operator=(const Prepared &) = default;
    Prepared &operator=(Prepared &&) noexcept = default;

  private:
    struct State;
    std::shared_ptr<const State> state_;
    explicit Prepared(std::shared_ptr<const State>);
    friend pipeline::SemanticNode triangulator(Prepared);
    friend Result<Prepared> prepare(const artifact::Store &, const Bindings &, const CancellationToken &);
    friend Result<data::Published> process(const Prepared &, const data::LaserObservation &,
                                           const CancellationToken &);
    friend Result<void> validate_result(const Prepared &, const data::Packet &, const CancellationToken &);
    friend Result<artifact::ArtifactReference>
    persist_result(artifact::Store &, const Prepared &, const data::Packet &, const CancellationToken &);
};
Result<Prepared> prepare(const artifact::Store &, const Bindings &, const CancellationToken & = {});
Result<data::Published> process(const Prepared &, const data::LaserObservation &,
                                const CancellationToken & = {});
pipeline::SemanticNode triangulator(Prepared);
Result<void> validate_result(const Prepared &, const data::Packet &, const CancellationToken & = {});
Result<artifact::ArtifactReference> persist_result(artifact::Store &, const Prepared &, const data::Packet &,
                                                   const CancellationToken & = {});
Result<data::Published> load_result(const artifact::Store &, const Id &, const CancellationToken & = {});
} // namespace mantis::alg03
