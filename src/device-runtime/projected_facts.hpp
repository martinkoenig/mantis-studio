// Internal structural comparison/refinement of typed semantic facts. No ABI or codec.
#pragma once
#include <mantis/projected_light_device.hpp>
#include <tuple>
#include <limits>
#include <stdexcept>
namespace mantis::device::detail {
inline auto fields(schema::DataTypeId &v) {
    return std::tie(v.name, v.version);
}
inline auto fields(const schema::DataTypeId &v) {
    return std::tie(v.name, v.version);
}
inline auto fields(Hash &v) {
    return std::tie(v.algorithm, v.hex);
}
inline auto fields(const Hash &v) {
    return std::tie(v.algorithm, v.hex);
}
inline auto fields(calibration::Reference &v) {
    return std::tie(v.id, v.schema_version, v.revision);
}
inline auto fields(const calibration::Reference &v) {
    return std::tie(v.id, v.schema_version, v.revision);
}
inline auto fields(time::ClockDomain &v) {
    return std::tie(v.id, v.name);
}
inline auto fields(const time::ClockDomain &v) {
    return std::tie(v.id, v.name);
}
inline auto fields(time::SyncGroup &v) {
    return std::tie(v.id, v.trigger);
}
inline auto fields(const time::SyncGroup &v) {
    return std::tie(v.id, v.trigger);
}
inline auto fields(time::ClockMapping &v) {
    return std::tie(v.source, v.target, v.scale, v.offset_ns, v.uncertainty_ns);
}
inline auto fields(const time::ClockMapping &v) {
    return std::tie(v.source, v.target, v.scale, v.offset_ns, v.uncertainty_ns);
}
inline auto fields(data::StepInstance &v) {
    return std::tie(v.run_id, v.repetition_index, v.step_index);
}
inline auto fields(const data::StepInstance &v) {
    return std::tie(v.run_id, v.repetition_index, v.step_index);
}
inline auto fields(data::StreamIdentity &v) {
    return std::tie(v.id, v.generation);
}
inline auto fields(const data::StreamIdentity &v) {
    return std::tie(v.id, v.generation);
}
inline auto fields(data::SourceFrameKey &v) {
    return std::tie(v.camera, v.stream, v.native_sequence);
}
inline auto fields(const data::SourceFrameKey &v) {
    return std::tie(v.camera, v.stream, v.native_sequence);
}
inline auto fields(data::TriggerKey &v) {
    return std::tie(v.run_id, v.source, v.controller_generation, v.sequence);
}
inline auto fields(const data::TriggerKey &v) {
    return std::tie(v.run_id, v.source, v.controller_generation, v.sequence);
}
inline auto fields(data::NativeTriggerIdentity &v) {
    return std::tie(v.controller, v.generation, v.value);
}
inline auto fields(const data::NativeTriggerIdentity &v) {
    return std::tie(v.controller, v.generation, v.value);
}
inline auto fields(data::ContentReference &v) {
    return std::tie(v.id, v.type, v.hash, v.revision);
}
inline auto fields(const data::ContentReference &v) {
    return std::tie(v.id, v.type, v.hash, v.revision);
}
inline auto fields(data::ProgramReference &v) {
    return std::tie(v.id, v.hash, v.content);
}
inline auto fields(const data::ProgramReference &v) {
    return std::tie(v.id, v.hash, v.content);
}
inline auto fields(data::ExactCalibrationReference &v) {
    return std::tie(v.calibration, v.content);
}
inline auto fields(const data::ExactCalibrationReference &v) {
    return std::tie(v.calibration, v.content);
}
inline auto fields(data::ClockIdentity &v) {
    return std::tie(v.domain, v.generation);
}
inline auto fields(const data::ClockIdentity &v) {
    return std::tie(v.domain, v.generation);
}
inline auto fields(data::SemanticTimestamp &v) {
    return std::tie(v.nanoseconds, v.clock);
}
inline auto fields(const data::SemanticTimestamp &v) {
    return std::tie(v.nanoseconds, v.clock);
}
inline auto fields(data::RuntimeTimestamp &v) {
    return std::tie(v.time, v.clock);
}
inline auto fields(const data::RuntimeTimestamp &v) {
    return std::tie(v.time, v.clock);
}
inline auto fields(data::TimeInterval &v) {
    return std::tie(v.start, v.end);
}
inline auto fields(const data::TimeInterval &v) {
    return std::tie(v.start, v.end);
}
inline auto fields(data::ClockMappingEvidence &v) {
    return std::tie(v.mapping, v.source_generation, v.target_generation, v.reference);
}
inline auto fields(const data::ClockMappingEvidence &v) {
    return std::tie(v.mapping, v.source_generation, v.target_generation, v.reference);
}
inline auto fields(data::EvidenceSource &v) {
    return std::tie(v.source, v.method, v.reference);
}
inline auto fields(const data::EvidenceSource &v) {
    return std::tie(v.source, v.method, v.reference);
}
inline auto fields(data::Acknowledgement &v) {
    return std::tie(v.request, v.stage, v.result, v.evidence, v.time, v.scope);
}
inline auto fields(const data::Acknowledgement &v) {
    return std::tie(v.request, v.stage, v.result, v.evidence, v.time, v.scope);
}
inline auto fields(data::EmitterCommand &v) {
    return std::tie(v.request, v.target, v.state, v.dispatched);
}
inline auto fields(const data::EmitterCommand &v) {
    return std::tie(v.request, v.target, v.state, v.dispatched);
}
inline auto fields(data::ExposureEvidence &v) {
    return std::tie(v.requested_duration, v.startup_readback_duration, v.integration_duration, v.interval,
                    v.evidence, v.uncertainty_ns);
}
inline auto fields(const data::ExposureEvidence &v) {
    return std::tie(v.requested_duration, v.startup_readback_duration, v.integration_duration, v.interval,
                    v.evidence, v.uncertainty_ns);
}
inline auto fields(data::ExposureAssociation &v) {
    return std::tie(v.frame, v.trigger, v.method, v.evidence, v.native_trigger);
}
inline auto fields(const data::ExposureAssociation &v) {
    return std::tie(v.frame, v.trigger, v.method, v.evidence, v.native_trigger);
}
inline auto fields(data::SyncEvidence &v) {
    return std::tie(v.group, v.quality, v.hardware_association);
}
inline auto fields(const data::SyncEvidence &v) {
    return std::tie(v.group, v.quality, v.hardware_association);
}
inline auto fields(data::CameraFrameEvidence &v) {
    return std::tie(v.frame, v.camera_role, v.width, v.height, v.source_timestamp, v.host_received,
                    v.timestamp_meaning, v.exposure, v.sync, v.camera_calibration, v.rig_calibration,
                    v.original_calibration);
}
inline auto fields(const data::CameraFrameEvidence &v) {
    return std::tie(v.frame, v.camera_role, v.width, v.height, v.source_timestamp, v.host_received,
                    v.timestamp_meaning, v.exposure, v.sync, v.camera_calibration, v.rig_calibration,
                    v.original_calibration);
}
inline auto fields(data::ExposureEffectiveState &v) {
    return std::tie(v.frame, v.state, v.scope, v.evidence, v.coverage);
}
inline auto fields(const data::ExposureEffectiveState &v) {
    return std::tie(v.frame, v.state, v.scope, v.evidence, v.coverage);
}
inline auto fields(data::CameraParticipant &v) {
    return std::tie(v.component, v.stream, v.role);
}
inline auto fields(const data::CameraParticipant &v) {
    return std::tie(v.component, v.stream, v.role);
}
inline auto fields(data::Participants &v) {
    return std::tie(v.cameras, v.emitters, v.controllers);
}
inline auto fields(const data::Participants &v) {
    return std::tie(v.cameras, v.emitters, v.controllers);
}
inline auto fields(data::TriggerEvent &v) {
    return std::tie(v.type, v.key, v.step, v.request, v.native_trigger, v.kind, v.acknowledgement, v.evidence,
                    v.device_time, v.host_received, v.host_dispatched, v.uncertainty_ns, v.clock_mapping,
                    v.intended_endpoints, v.actual_endpoints, v.requested_exposure, v.exposure_association);
}
inline auto fields(const data::TriggerEvent &v) {
    return std::tie(v.type, v.key, v.step, v.request, v.native_trigger, v.kind, v.acknowledgement, v.evidence,
                    v.device_time, v.host_received, v.host_dispatched, v.uncertainty_ns, v.clock_mapping,
                    v.intended_endpoints, v.actual_endpoints, v.requested_exposure, v.exposure_association);
}
inline auto fields(time::MonotonicTimestamp &v) {
    return std::tie(v.nanoseconds);
}
inline auto fields(const time::MonotonicTimestamp &v) {
    return std::tie(v.nanoseconds);
}
inline auto fields(data::CameraEffectiveState &v) {
    return std::tie(v.frame, v.state);
}
inline auto fields(const data::CameraEffectiveState &v) {
    return std::tie(v.frame, v.state);
}
inline auto fields(data::FrameSetKey &v) {
    return std::tie(v.run_id, v.stream, v.sequence);
}
inline auto fields(const data::FrameSetKey &v) {
    return std::tie(v.run_id, v.stream, v.sequence);
}
inline auto fields(data::ImplementationIdentity &v) {
    return std::tie(v.implementation, v.version, v.build, v.configuration);
}
inline auto fields(const data::ImplementationIdentity &v) {
    return std::tie(v.implementation, v.version, v.build, v.configuration);
}
inline auto fields(SemanticVersion &v) {
    return std::tie(v.major, v.minor, v.patch);
}
inline auto fields(const SemanticVersion &v) {
    return std::tie(v.major, v.minor, v.patch);
}
inline auto fields(data::StateObservation &v) {
    return std::tie(v.state, v.scope, v.evidence, v.time, v.coverage);
}
inline auto fields(const data::StateObservation &v) {
    return std::tie(v.state, v.scope, v.evidence, v.time, v.coverage);
}
inline auto fields(data::EmitterEvidence &v) {
    return std::tie(v.emitter, v.commanded, v.acknowledged, v.observed, v.exposure_effective);
}
inline auto fields(const data::EmitterEvidence &v) {
    return std::tie(v.emitter, v.commanded, v.acknowledged, v.observed, v.exposure_effective);
}
inline auto fields(data::LossAccounting &v) {
    return std::tie(v.kind, v.source, v.count, v.reason, v.frame, v.request);
}
inline auto fields(const data::LossAccounting &v) {
    return std::tie(v.kind, v.source, v.count, v.reason, v.frame, v.request);
}
inline auto fields(data::UnresolvedRequest &v) {
    return std::tie(v.request, v.target, v.reason);
}
inline auto fields(const data::UnresolvedRequest &v) {
    return std::tie(v.request, v.target, v.reason);
}
inline auto fields(data::AcquisitionEvidence &v) {
    return std::tie(v.type, v.key, v.program, v.step, v.causal_predecessors, v.participants,
                    v.implementations, v.emitters, v.frameset, v.frames, v.triggers, v.clock_mappings,
                    v.rig_calibration, v.disposition, v.reason, v.unresolved_requests, v.losses,
                    v.diagnostic);
}
inline auto fields(const data::AcquisitionEvidence &v) {
    return std::tie(v.type, v.key, v.program, v.step, v.causal_predecessors, v.participants,
                    v.implementations, v.emitters, v.frameset, v.frames, v.triggers, v.clock_mappings,
                    v.rig_calibration, v.disposition, v.reason, v.unresolved_requests, v.losses,
                    v.diagnostic);
}
inline auto fields(data::EvidenceKey &v) {
    return std::tie(v.run_id, v.ordinal);
}
inline auto fields(const data::EvidenceKey &v) {
    return std::tie(v.run_id, v.ordinal);
}
template <class T> bool refine(T &a, const T &b);
template <class T> bool refine(data::Evidence<T> &a, const data::Evidence<T> &b) {
    if (!b.get())
        return true;
    if (!a.get()) {
        a = *b.get();
        return true;
    }
    auto value = *a.get();
    if (!refine(value, *b.get()))
        return false;
    a = std::move(value);
    return true;
}
template <class T> bool refine(std::optional<T> &a, const std::optional<T> &b) {
    return a.has_value() == b.has_value() && (!a || refine(*a, *b));
}
template <class T> bool refine(std::vector<T> &a, const std::vector<T> &b) {
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (!refine(a[i], b[i]))
            return false;
    return true;
}
template <class T> bool refine(T &a, const T &b) {
    if constexpr (requires { fields(a); }) {
        auto left = fields(a), right = fields(b);
        return [&]<size_t... I>(std::index_sequence<I...>) {
            return (refine(std::get<I>(left), std::get<I>(right)) && ...);
        }(std::make_index_sequence<std::tuple_size_v<decltype(left)>>{});
    } else
        return a == b;
}
// A structural resource charge, not allocator introspection or serialized size.
struct AccountingOverflow : std::overflow_error {
    AccountingOverflow() : std::overflow_error("In-memory resource accounting overflow") {}
};
inline uint64_t add_bytes(uint64_t a, uint64_t b) {
    if (b > UINT64_MAX - a)
        throw AccountingOverflow{};
    return a + b;
}
inline uint64_t multiply_bytes(uint64_t a, uint64_t b) {
    if (b && a > UINT64_MAX / b)
        throw AccountingOverflow{};
    return a * b;
}
// Every allocation is charged its storage, maximum ordinary alignment padding,
// and two pointer-sized bookkeeping slots. This is an explicit admission policy;
// allocator arenas and executor-owned resources are outside this runtime charge.
inline uint64_t allocation_bytes(uint64_t storage) {
    return add_bytes(storage, alignof(std::max_align_t) - 1 + 2 * sizeof(void *));
}
inline auto fields(const data::BundleKey &v) { return std::tie(v.run_id, v.sequence); }
inline auto fields(const data::AcquisitionBundle &v) {
    return std::tie(v.type, v.key, v.published, v.evidence, v.triggers);
}
inline auto fields(const time::DeviceTimestamp &v) { return std::tie(v.domain); }
inline auto fields(const spatial::CoordinateFrame &v) { return std::tie(v.id, v.name); }
inline auto fields(const data::Header &v) {
    return std::tie(v.timestamp, v.sync, v.calibration, v.frame, v.metadata);
}
inline auto fields(const schema::AttributeDescriptor &v) {
    return std::tie(v.name, v.unit, v.shape, v.stride);
}
inline auto fields(const data::TriggerIntent &v) { return std::tie(v.controller, v.request, v.endpoints); }
inline auto fields(const data::CaptureIntent &v) { return std::tie(v.cameras, v.trigger); }
inline auto fields(const data::EmitterIntent &v) { return std::tie(v.emitter); }
inline auto fields(const data::AcquisitionStep &v) { return std::tie(v.label, v.emitters, v.capture); }
inline auto fields(const data::AcquisitionProgram &v) {
    return std::tie(v.type, v.identity, v.participants, v.steps);
}
inline auto fields(const device::Descriptor &v) {
    return std::tie(v.id, v.name, v.plugin_id, v.capabilities, v.children, v.parent, v.metadata);
}
inline auto fields(const device::ProjectedImageSource &v) { return std::tie(v.stream, v.physical_identity); }
inline auto fields(const data::LineIdentity &v) { return std::tie(v.emitter, v.pattern, v.pattern_revision, v.local_line); }
inline auto fields(const device::ProjectedComponent &v) {
    return std::tie(v.descriptor, v.role, v.image_source, v.controls, v.participants, v.trigger_endpoints,
                    v.emitter_states, v.capture_modes, v.trigger_modes, v.evidence_methods, v.evidence_scopes,
                    v.pattern, v.pattern_revision, v.lines);
}
inline auto fields(const device::ProjectedGraph &v) { return std::tie(v.parent, v.components, v.frameset_stream); }
template <class T> uint64_t heap_bytes(const T &v);
inline uint64_t heap_bytes(const std::string &v) {
    // Charging even small-string inline capacity is conservative and portable.
    return allocation_bytes(add_bytes(v.capacity(), 1));
}
template <class T> uint64_t heap_bytes(const data::Evidence<T> &v) {
    return v.get() ? heap_bytes(*v.get()) : 0;
}
template <class T> uint64_t heap_bytes(const std::optional<T> &v) {
    return v ? heap_bytes(*v) : 0;
}
template <class T> uint64_t heap_bytes(const std::vector<T> &v) {
    uint64_t size = v.capacity() ? allocation_bytes(multiply_bytes(v.capacity(), sizeof(T))) : 0;
    for (const auto &x : v)
        size = add_bytes(size, heap_bytes(x));
    return size;
}
inline uint64_t heap_bytes(const data::Metadata &v) {
    uint64_t size = 0;
    for (const auto &[key, value] : v) {
        // std::map value plus three tree links and a pointer-sized color/padding slot.
        size = add_bytes(size, allocation_bytes(sizeof(data::Metadata::value_type) + 4 * sizeof(void *)));
        size = add_bytes(size, add_bytes(heap_bytes(key), heap_bytes(value)));
    }
    return size;
}
template <class T> uint64_t heap_bytes(const T &v) {
    if constexpr (requires { fields(v); }) {
        return std::apply([](const auto &...x) {
            uint64_t result = 0;
            ((result = add_bytes(result, heap_bytes(x))), ...);
            return result;
        }, fields(v));
    } else if constexpr (requires { v.id; })
        return heap_bytes(v.id);
    else if constexpr (std::is_same_v<T, Id>)
        return heap_bytes(v.value);
    else
        return 0;
}
inline uint64_t packet_metadata_bytes(const data::Packet &packet) {
    uint64_t size = allocation_bytes(sizeof(data::Packet) + 2 * sizeof(void *));
    size = add_bytes(size, heap_bytes(packet.type));
    size = add_bytes(size, heap_bytes(packet.header));
    if (packet.attributes.capacity())
        size = add_bytes(size, allocation_bytes(multiply_bytes(packet.attributes.capacity(), sizeof(data::Attribute))));
    for (const auto &a : packet.attributes) {
        size = add_bytes(size, heap_bytes(a.descriptor));
        // One storage descriptor/control block per attribute is a conservative charge
        // even if several immutable attributes share it. Payload is counted separately.
        size = add_bytes(size, allocation_bytes(sizeof(memory::Storage) + 2 * sizeof(void *)));
    }
    if (packet.frames.capacity())
        size = add_bytes(size, allocation_bytes(multiply_bytes(packet.frames.capacity(), sizeof(data::Published))));
    // L1 restricts this tree to one FrameSet and 1..16 non-composite images.
    for (const auto &child : packet.frames)
        size = add_bytes(size, packet_metadata_bytes(*child));
    return size;
}
} // namespace mantis::device::detail
