#pragma once
#include <chrono>
#include <mantis/data.hpp>
#include <optional>

namespace mantis::data {
// L1 software sanity bounds, not hardware limits or physical safety thresholds.
inline constexpr size_t max_program_steps = 256, max_bundle_members = 64, max_participants = 64;
inline constexpr uint64_t max_executed_steps = 1'000'000, max_observation_samples = 1'000'000;
inline constexpr size_t max_semantic_entries = 4096, max_semantic_string = 1024, max_semantic_id = 256;
inline constexpr size_t max_semantic_attributes = 128, max_semantic_payload = 128 * 1024 * 1024;

// Identity domains cannot be accidentally interchanged. A RunId is itself a run generation.
template <class Tag> struct SemanticId {
    Id id;
    auto operator<=>(const SemanticId &) const = default;
};
using ProgramId = SemanticId<struct ProgramTag>;
using RunId = SemanticId<struct RunTag>;
using GenerationId = SemanticId<struct GenerationTag>;
using ComponentId = SemanticId<struct ComponentTag>;
using StreamId = SemanticId<struct StreamTag>;
using ProducerStreamId = SemanticId<struct ProducerStreamTag>;
using RequestId = SemanticId<struct RequestTag>;
using PatternId = SemanticId<struct PatternTag>;
using LineId = SemanticId<struct LineTag>;
template <class Tag> struct SemanticSequence {
    uint64_t value{};
    auto operator<=>(const SemanticSequence &) const = default;
};
using EventSequence = SemanticSequence<struct EventSequenceTag>;
using BundleSequence = SemanticSequence<struct BundleSequenceTag>;
using ObservationSequence = SemanticSequence<struct ObservationSequenceTag>;
using CausalOrdinal = SemanticSequence<struct CausalOrdinalTag>;
struct StepInstance {
    RunId run_id;
    uint64_t repetition_index{};
    uint32_t step_index{};
    auto operator<=>(const StepInstance &) const = default;
};
struct StreamIdentity {
    StreamId id;
    GenerationId generation;
    auto operator<=>(const StreamIdentity &) const = default;
};
struct SourceFrameKey {
    ComponentId camera;
    StreamIdentity stream;
    uint64_t native_sequence{}; // Never a step, trigger, bundle or observation sequence.
    auto operator<=>(const SourceFrameKey &) const = default;
};
struct FrameSetKey {
    RunId run_id;
    StreamIdentity stream;
    uint64_t sequence{};
    auto operator<=>(const FrameSetKey &) const = default;
};
struct BundleKey {
    RunId run_id;
    BundleSequence sequence;
    auto operator<=>(const BundleKey &) const = default;
};
struct EvidenceKey {
    RunId run_id;
    CausalOrdinal ordinal;
    auto operator<=>(const EvidenceKey &) const = default;
};
struct TriggerKey {
    RunId run_id;
    ComponentId source;
    GenerationId controller_generation;
    EventSequence sequence;
    auto operator<=>(const TriggerKey &) const = default;
};
struct NativeTriggerIdentity {
    ComponentId controller;
    GenerationId generation;
    uint64_t value{};
    auto operator<=>(const NativeTriggerIdentity &) const = default;
};

struct Unknown {
    auto operator<=>(const Unknown &) const = default;
};
struct Unavailable {
    auto operator<=>(const Unavailable &) const = default;
};
enum class Presence { established, unknown, unavailable };
// Only the established alternative can hold a value; there is no sentinel/default evidence value.
template <class T> class Evidence {
    std::variant<Unknown, Unavailable, T> value_;

  public:
    Evidence() : value_(Unknown{}) {}
    Evidence(Unknown v) : value_(v) {}
    Evidence(Unavailable v) : value_(v) {}
    Evidence(T v) : value_(std::move(v)) {}
    Presence presence() const {
        return std::holds_alternative<T>(value_)         ? Presence::established
               : std::holds_alternative<Unknown>(value_) ? Presence::unknown
                                                         : Presence::unavailable;
    }
    const T *get() const {
        return std::get_if<T>(&value_);
    }
    auto operator<=>(const Evidence &) const = default;
};

// Dependency-safe immutable reference. No artifact-api dependency or new artifact meaning.
struct ContentReference {
    Id id;
    schema::DataTypeId type;
    Evidence<Hash> hash;
    uint64_t revision{};
};
struct ProgramReference {
    ProgramId id;
    Evidence<Hash> hash;
    Evidence<ContentReference> content;
};
struct ExactCalibrationReference {
    calibration::Reference calibration;
    Evidence<ContentReference> content;
};
struct ClockIdentity {
    time::ClockDomain domain;
    GenerationId generation;
};
struct SemanticTimestamp {
    int64_t nanoseconds{}; // Zero is a valid time, never absence.
    ClockIdentity clock;
};
// Host/runtime monotonic time is a different type from sensor/physical-event time.
struct RuntimeTimestamp {
    time::MonotonicTimestamp time;
    ClockIdentity clock;
};
struct TimeInterval {
    SemanticTimestamp start, end;
};
struct ClockMappingEvidence {
    time::ClockMapping mapping;
    GenerationId source_generation, target_generation;
    ContentReference reference;
};
using Duration = std::chrono::nanoseconds;
enum class EvidenceMethod {
    software_dispatch,
    controller_report,
    register_readback,
    electrical_readback,
    optical_sensor,
    validated_executor,
    camera_metadata,
    software_association,
    imported
};
enum class EvidenceScope { controller_register, electrical_enable, optical_emission };
struct EvidenceSource {
    ComponentId source;
    EvidenceMethod method{EvidenceMethod::software_dispatch};
    Evidence<ContentReference> reference;
};
enum class EmitterState { off, on }; // Generic enable only; no hidden power/modulation setting.
// Required value fields below use optional only for construction: validators reject omission.
// Physical evidence availability is always represented by Evidence<T>.
enum class AcknowledgementStage { acceptance, completion };
enum class AcknowledgementResult { success, rejected, failed };
struct Acknowledgement {
    RequestId request;
    std::optional<AcknowledgementStage> stage;
    std::optional<AcknowledgementResult> result;
    EvidenceSource evidence;
    Evidence<SemanticTimestamp> time;
    EvidenceScope scope{EvidenceScope::controller_register}; // Acknowledgement scope, not observed state.
};
struct EmitterCommand {
    RequestId request;
    ComponentId target;
    EmitterState state{EmitterState::off};
    RuntimeTimestamp dispatched;
};
struct StateObservation {
    std::optional<EmitterState> state;
    EvidenceScope scope{EvidenceScope::controller_register};
    EvidenceSource evidence;
    Evidence<SemanticTimestamp> time;
    Evidence<TimeInterval> coverage;
};
enum class TimestampMeaning { exposure_start, exposure_end, driver_delivery, device_event };
struct ExposureEvidence {
    Evidence<Duration> requested_duration, startup_readback_duration, integration_duration;
    Evidence<TimeInterval> interval;
    EvidenceSource evidence;
    Evidence<double> uncertainty_ns;
};
enum class AssociationMethod { native_trigger, validated_executor, software_correspondence, imported };
struct ExposureAssociation {
    SourceFrameKey frame;
    TriggerKey trigger;
    AssociationMethod method{AssociationMethod::software_correspondence};
    EvidenceSource evidence;
    Evidence<NativeTriggerIdentity> native_trigger;
};
struct SyncEvidence {
    time::SyncGroup group;
    time::SyncQuality quality{time::SyncQuality::unknown};
    Evidence<ExposureAssociation> hardware_association;
};
struct CameraFrameEvidence {
    SourceFrameKey frame;
    std::string camera_role;
    uint32_t width{}, height{};
    Evidence<SemanticTimestamp> source_timestamp;
    Evidence<RuntimeTimestamp> host_received;
    Evidence<TimestampMeaning> timestamp_meaning;
    Evidence<ExposureEvidence> exposure;
    Evidence<SyncEvidence> sync;
    Evidence<ExactCalibrationReference> camera_calibration, rig_calibration, original_calibration;
};
struct ExposureEffectiveState {
    SourceFrameKey frame;
    std::optional<EmitterState> state;
    EvidenceScope scope{EvidenceScope::controller_register};
    EvidenceSource evidence;
    TimeInterval coverage; // Must cover the exact frame's established exposure interval.
};
struct CameraEffectiveState {
    SourceFrameKey frame;
    Evidence<ExposureEffectiveState> state;
};
struct EmitterEvidence {
    ComponentId emitter;
    Evidence<EmitterCommand> commanded;
    Evidence<Acknowledgement> acknowledged;
    Evidence<StateObservation> observed;
    std::vector<CameraEffectiveState> exposure_effective;
};
struct CameraParticipant {
    ComponentId component;
    StreamId stream;
    std::string role;
};
struct Participants {
    std::vector<CameraParticipant> cameras;
    std::vector<ComponentId> emitters, controllers;
};
enum class CaptureMode { none, free_running, hardware_trigger };
enum class EvidenceRequirement { commanded_only, controller_acknowledged, exposure_effective };
struct TriggerIntent {
    ComponentId controller;
    RequestId request;
    std::vector<ComponentId> endpoints;
};
struct CaptureIntent {
    std::optional<CaptureMode> mode;
    std::vector<ComponentId> cameras;
    std::optional<TriggerIntent> trigger;
};
struct EmitterIntent {
    ComponentId emitter;
    EmitterState state{EmitterState::off};
};
struct AcquisitionStep {
    uint32_t index{};
    std::string label;
    std::vector<EmitterIntent> emitters;
    CaptureIntent capture;
    std::optional<EvidenceRequirement> evidence_requirement;
    EvidenceScope required_scope{EvidenceScope::controller_register};
    Duration settle{}, max_duration{};
};
struct RunBounds {
    Duration max_duration{};
    // Continuous ON limit spans step/repetition boundaries; enforcement belongs to the executor.
    Duration max_on_duration{};
    uint64_t max_step_instances{}, max_commands{}, max_events{}, max_bytes{}, max_in_flight_captures{};
};
enum class TerminalPolicy { inhibit_triggers_and_request_all_off };
struct AcquisitionProgram {
    static constexpr TerminalPolicy terminal_policy = TerminalPolicy::inhibit_triggers_and_request_all_off;
    schema::DataTypeId type{schema::acquisition_program};
    ProgramReference identity;
    Participants participants;
    std::vector<AcquisitionStep> steps;
    uint64_t repetitions{};
    RunBounds bounds;
    // Schema 1 has no configurable terminal policy: inhibit future triggers and request all OFF.
};

struct TriggerEvent {
    schema::DataTypeId type{schema::trigger_event};
    TriggerKey key;
    StepInstance step;
    RequestId request;
    Evidence<NativeTriggerIdentity> native_trigger;
    enum class Kind {
        requested,
        acknowledged_accepted,
        acknowledged_completed,
        rejected,
        observed,
        timed_out,
        cancelled
    };
    std::optional<Kind> kind;
    Evidence<Acknowledgement> acknowledgement;
    EvidenceSource evidence;
    Evidence<SemanticTimestamp> device_time;
    Evidence<RuntimeTimestamp> host_received, host_dispatched;
    Evidence<double> uncertainty_ns;
    Evidence<ClockMappingEvidence> clock_mapping;
    std::vector<ComponentId> intended_endpoints;
    Evidence<std::vector<ComponentId>> actual_endpoints;
    Evidence<SourceFrameKey> requested_exposure;
    Evidence<ExposureAssociation> exposure_association;
};
struct ImplementationIdentity {
    Id implementation;
    SemanticVersion version;
    std::string build;
    Evidence<ContentReference> configuration;
};
enum class AcquisitionDisposition { startup, captured, control_only, completed, failed, stopped, cancelled };
enum class AcquisitionReason {
    none,
    timeout,
    rejected,
    device_failure,
    transport_failure,
    evidence_missing,
    contradictory_evidence,
    resource_limit,
    user_stop,
    user_cancel,
    cleanup_failure
};
enum class LossKind { command, trigger, frame, exposure, bundle, record, excluded_frame };
struct LossAccounting {
    LossKind kind{LossKind::frame};
    ComponentId source;
    Evidence<uint64_t> count;
    AcquisitionReason reason{AcquisitionReason::none};
    Evidence<SourceFrameKey> frame;
    Evidence<RequestId> request;
};
struct UnresolvedRequest {
    RequestId request;
    ComponentId target;
    AcquisitionReason reason{AcquisitionReason::evidence_missing};
};
struct AcquisitionEvidence {
    schema::DataTypeId type{schema::acquisition_evidence};
    EvidenceKey key;
    ProgramReference program;
    Evidence<StepInstance> step;
    std::vector<EvidenceKey> causal_predecessors;
    Participants participants;
    std::vector<ImplementationIdentity> implementations;
    std::vector<EmitterEvidence> emitters;
    Evidence<FrameSetKey> frameset;
    std::vector<CameraFrameEvidence> frames;
    std::vector<TriggerKey> triggers;
    std::vector<ClockMappingEvidence> clock_mappings;
    Evidence<ExactCalibrationReference> rig_calibration;
    std::optional<AcquisitionDisposition> disposition;
    AcquisitionReason reason{AcquisitionReason::none};
    std::vector<UnresolvedRequest> unresolved_requests;
    std::vector<LossAccounting> losses;
    std::string diagnostic; // Non-authoritative, bounded human explanation only.
};
struct FrameSetAssociation {
    FrameSetKey key;
    std::vector<SourceFrameKey> frames; // Association only: unchanged image-only FrameSet lives elsewhere.
};
struct AcquisitionBundle {
    schema::DataTypeId type{schema::acquisition_bundle};
    BundleKey key;
    RuntimeTimestamp published;                  // Runtime monotonic publication time, never exposure time.
    AcquisitionEvidence evidence;                // Exactly one, by construction.
    std::optional<FrameSetAssociation> frameset; // Zero or one, by construction.
    std::vector<TriggerEvent> triggers;
};

namespace laser {
inline constexpr std::string_view source_pixel = "org.mantis.laser.source_pixel";
inline constexpr std::string_view quality_flags = "org.mantis.laser.quality_flags";
inline constexpr std::string_view emitter_index = "org.mantis.laser.emitter_index";
inline constexpr std::string_view emitter_valid = "org.mantis.laser.emitter_valid";
inline constexpr std::string_view line_index = "org.mantis.laser.line_index";
inline constexpr std::string_view line_valid = "org.mantis.laser.line_valid";
inline constexpr std::string_view confidence = "org.mantis.laser.confidence";
inline constexpr std::string_view confidence_valid = "org.mantis.laser.confidence_valid";
inline constexpr uint32_t emitter_unknown = 1u << 0, line_unknown = 1u << 1, ambiguous = 1u << 2,
                          producer_rejected = 1u << 3;
inline constexpr std::string_view pixel_convention =
    "x right; y down; origin at center of original pixel (0,0)";
// Descriptor helpers are for N>0; successful N=0 has no attributes.
schema::AttributeDescriptor source_pixel_descriptor(uint64_t n);
schema::AttributeDescriptor quality_flags_descriptor(uint64_t n);
} // namespace laser
struct EmitterPatternIdentity {
    ComponentId emitter;
    PatternId pattern;
    uint64_t revision{};
    auto operator<=>(const EmitterPatternIdentity &) const = default;
};
struct LineIdentity {
    ComponentId emitter;
    PatternId pattern;
    uint64_t pattern_revision{};
    LineId local_line;
    auto operator<=>(const LineIdentity &) const = default;
};
struct ProgramCorrelation {
    ProgramReference program;
    StepInstance step;
};
struct PreprocessingTransform {
    // Homogeneous original_from_processed 2D transform; reference may describe richer preprocessing.
    std::array<double, 9> original_from_processed{1, 0, 0, 0, 1, 0, 0, 0, 1};
    Evidence<ContentReference> reference;
};
enum class ObservationOrigin { real, synthetic, imported };
enum class ObservationDisposition { success, extractor_failed, extractor_unavailable };
struct ObservationKey {
    Evidence<RunId> run_id;
    ProducerStreamId producer_stream;
    GenerationId producer_generation;
    ObservationSequence sequence;
};
struct LaserObservationContext {
    CameraFrameEvidence source;
    Evidence<FrameSetKey> frameset;
    Evidence<BundleKey> bundle;
    Evidence<ContentReference> raw_input;
    spatial::CoordinateFrame optical_frame;
    Evidence<PreprocessingTransform> preprocessing;
    std::vector<ComponentId> requested_emitters;
    std::vector<EmitterEvidence> emitter_evidence;
    std::vector<EmitterPatternIdentity>
        emitter_patterns; // Pattern can be known while line identity is unknown.
    Evidence<ProgramCorrelation> correlation;
    Evidence<EvidenceKey> acquisition_evidence;
    std::vector<TriggerKey> triggers;
    std::vector<ClockMappingEvidence> clock_mappings;
    ImplementationIdentity producer;
    Evidence<ContentReference> parameters;
    std::vector<ContentReference> exact_inputs;
    std::optional<ObservationOrigin> origin;
    Evidence<RuntimeTimestamp> producer_completed;
    uint32_t packet_quality_flags{}; // Producer-defined; no universal quality/probability claim.
};
struct LaserObservation {
    schema::DataTypeId type{schema::laser_observation};
    ObservationKey key;
    LaserObservationContext context;
    std::optional<ObservationDisposition> disposition;
    uint64_t sample_count{};
    // Index validity records producer attribution, not independently confirmed optical emission.
    std::vector<ComponentId> emitter_dictionary;
    std::vector<LineIdentity> line_dictionary;
    std::vector<Attribute> attributes; // Bulk immutable buffers; no objects allocated per sample.
    Evidence<ImplementationIdentity> confidence_interpretation;
    std::string diagnostic;
};
Result<void> validate(const AcquisitionProgram &);
Result<void> validate(const TriggerEvent &);
Result<void> validate(const AcquisitionEvidence &);
Result<void> validate(const AcquisitionBundle &);
Result<void> validate(const LaserObservation &);
// Ordering needs previous publication context; validation never sorts or renumbers.
Result<void> validate_successor(const AcquisitionBundle &previous, const AcquisitionBundle &next);
Result<void> validate_successor(const LaserObservation &previous, const LaserObservation &next);
} // namespace mantis::data
