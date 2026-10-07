// Generated field conversions; no serialization or runtime policy.
schema::DataTypeId decode(const MantisDataTypeV1 &v, Reader &r) {
    r.prefix(v);
    schema::DataTypeId o;
    o.name = decode_value<std::string>(v.name, r);
    o.version = decode_value<uint32_t>(v.version, r);
    return o;
}
MantisDataTypeV1 encode(const schema::DataTypeId &v, Writer &w) {
    MantisDataTypeV1 o{};
    init(o);
    o.name = encode_value(v.name, w);
    o.version = encode_value(v.version, w);
    return o;
}
Hash decode(const MantisHashV1 &v, Reader &r) {
    r.prefix(v);
    Hash o;
    o.algorithm = decode_value<std::string>(v.algorithm, r);
    o.hex = decode_value<std::string>(v.hex, r);
    return o;
}
MantisHashV1 encode(const Hash &v, Writer &w) {
    MantisHashV1 o{};
    init(o);
    o.algorithm = encode_value(v.algorithm, w);
    o.hex = encode_value(v.hex, w);
    return o;
}
SemanticVersion decode(const MantisVersionV1 &v, Reader &r) {
    r.prefix(v);
    SemanticVersion o;
    o.major = decode_value<uint32_t>(v.major, r);
    o.minor = decode_value<uint32_t>(v.minor, r);
    o.patch = decode_value<uint32_t>(v.patch, r);
    return o;
}
MantisVersionV1 encode(const SemanticVersion &v, Writer &w) {
    MantisVersionV1 o{};
    init(o);
    o.major = encode_value(v.major, w);
    o.minor = encode_value(v.minor, w);
    o.patch = encode_value(v.patch, w);
    return o;
}
calibration::Reference decode(const MantisCalibrationReferenceV1 &v, Reader &r) {
    r.prefix(v);
    calibration::Reference o;
    o.id = decode_value<Id>(v.id, r);
    o.schema_version = decode_value<uint32_t>(v.schema_version, r);
    o.revision = decode_value<uint64_t>(v.revision, r);
    return o;
}
MantisCalibrationReferenceV1 encode(const calibration::Reference &v, Writer &w) {
    MantisCalibrationReferenceV1 o{};
    init(o);
    o.id = encode_value(v.id, w);
    o.schema_version = encode_value(v.schema_version, w);
    o.revision = encode_value(v.revision, w);
    return o;
}
time::ClockDomain decode(const MantisClockDomainV1 &v, Reader &r) {
    r.prefix(v);
    time::ClockDomain o;
    o.id = decode_value<Id>(v.id, r);
    o.name = decode_value<std::string>(v.name, r);
    return o;
}
MantisClockDomainV1 encode(const time::ClockDomain &v, Writer &w) {
    MantisClockDomainV1 o{};
    init(o);
    o.id = encode_value(v.id, w);
    o.name = encode_value(v.name, w);
    return o;
}
spatial::CoordinateFrame decode(const MantisCoordinateFrameV1 &v, Reader &r) {
    r.prefix(v);
    spatial::CoordinateFrame o;
    o.id = decode_value<Id>(v.id, r);
    o.name = decode_value<std::string>(v.name, r);
    return o;
}
MantisCoordinateFrameV1 encode(const spatial::CoordinateFrame &v, Writer &w) {
    MantisCoordinateFrameV1 o{};
    init(o);
    o.id = encode_value(v.id, w);
    o.name = encode_value(v.name, w);
    return o;
}
time::SyncGroup decode(const MantisSyncGroupV1 &v, Reader &r) {
    r.prefix(v);
    time::SyncGroup o;
    o.id = decode_value<Id>(v.id, r);
    o.trigger = decode_value<uint64_t>(v.trigger, r);
    return o;
}
MantisSyncGroupV1 encode(const time::SyncGroup &v, Writer &w) {
    MantisSyncGroupV1 o{};
    init(o);
    o.id = encode_value(v.id, w);
    o.trigger = encode_value(v.trigger, w);
    return o;
}
time::ClockMapping decode(const MantisClockMappingV1 &v, Reader &r) {
    r.prefix(v);
    time::ClockMapping o;
    o.source = decode_value<time::ClockDomain>(v.source, r);
    o.target = decode_value<time::ClockDomain>(v.target, r);
    o.scale = decode_value<double>(v.scale, r);
    o.offset_ns = decode_value<double>(v.offset_ns, r);
    o.uncertainty_ns = decode_value<double>(v.uncertainty_ns, r);
    return o;
}
MantisClockMappingV1 encode(const time::ClockMapping &v, Writer &w) {
    MantisClockMappingV1 o{};
    init(o);
    o.source = encode_value(v.source, w);
    o.target = encode_value(v.target, w);
    o.scale = encode_value(v.scale, w);
    o.offset_ns = encode_value(v.offset_ns, w);
    o.uncertainty_ns = encode_value(v.uncertainty_ns, w);
    return o;
}
data::StepInstance decode(const MantisStepInstanceV1 &v, Reader &r) {
    r.prefix(v);
    data::StepInstance o;
    o.run_id = decode_value<data::RunId>(v.run_id, r);
    o.repetition_index = decode_value<uint64_t>(v.repetition_index, r);
    o.step_index = decode_value<uint32_t>(v.step_index, r);
    return o;
}
MantisStepInstanceV1 encode(const data::StepInstance &v, Writer &w) {
    MantisStepInstanceV1 o{};
    init(o);
    o.run_id = encode_value(v.run_id, w);
    o.repetition_index = encode_value(v.repetition_index, w);
    o.step_index = encode_value(v.step_index, w);
    return o;
}
data::StreamIdentity decode(const MantisStreamIdentityV1 &v, Reader &r) {
    r.prefix(v);
    data::StreamIdentity o;
    o.id = decode_value<data::StreamId>(v.id, r);
    o.generation = decode_value<data::GenerationId>(v.generation, r);
    return o;
}
MantisStreamIdentityV1 encode(const data::StreamIdentity &v, Writer &w) {
    MantisStreamIdentityV1 o{};
    init(o);
    o.id = encode_value(v.id, w);
    o.generation = encode_value(v.generation, w);
    return o;
}
data::SourceFrameKey decode(const MantisSourceFrameKeyV1 &v, Reader &r) {
    r.prefix(v);
    data::SourceFrameKey o;
    o.camera = decode_value<data::ComponentId>(v.camera, r);
    o.stream = decode_value<data::StreamIdentity>(v.stream, r);
    o.native_sequence = decode_value<uint64_t>(v.native_sequence, r);
    return o;
}
MantisSourceFrameKeyV1 encode(const data::SourceFrameKey &v, Writer &w) {
    MantisSourceFrameKeyV1 o{};
    init(o);
    o.camera = encode_value(v.camera, w);
    o.stream = encode_value(v.stream, w);
    o.native_sequence = encode_value(v.native_sequence, w);
    return o;
}
data::FrameSetKey decode(const MantisFrameSetKeyV1 &v, Reader &r) {
    r.prefix(v);
    data::FrameSetKey o;
    o.run_id = decode_value<data::RunId>(v.run_id, r);
    o.stream = decode_value<data::StreamIdentity>(v.stream, r);
    o.sequence = decode_value<uint64_t>(v.sequence, r);
    return o;
}
MantisFrameSetKeyV1 encode(const data::FrameSetKey &v, Writer &w) {
    MantisFrameSetKeyV1 o{};
    init(o);
    o.run_id = encode_value(v.run_id, w);
    o.stream = encode_value(v.stream, w);
    o.sequence = encode_value(v.sequence, w);
    return o;
}
data::BundleKey decode(const MantisBundleKeyV1 &v, Reader &r) {
    r.prefix(v);
    data::BundleKey o;
    o.run_id = decode_value<data::RunId>(v.run_id, r);
    o.sequence = decode_value<data::BundleSequence>(v.sequence, r);
    return o;
}
MantisBundleKeyV1 encode(const data::BundleKey &v, Writer &w) {
    MantisBundleKeyV1 o{};
    init(o);
    o.run_id = encode_value(v.run_id, w);
    o.sequence = encode_value(v.sequence, w);
    return o;
}
data::EvidenceKey decode(const MantisEvidenceKeyV1 &v, Reader &r) {
    r.prefix(v);
    data::EvidenceKey o;
    o.run_id = decode_value<data::RunId>(v.run_id, r);
    o.ordinal = decode_value<data::CausalOrdinal>(v.ordinal, r);
    return o;
}
MantisEvidenceKeyV1 encode(const data::EvidenceKey &v, Writer &w) {
    MantisEvidenceKeyV1 o{};
    init(o);
    o.run_id = encode_value(v.run_id, w);
    o.ordinal = encode_value(v.ordinal, w);
    return o;
}
data::TriggerKey decode(const MantisTriggerKeyV1 &v, Reader &r) {
    r.prefix(v);
    data::TriggerKey o;
    o.run_id = decode_value<data::RunId>(v.run_id, r);
    o.source = decode_value<data::ComponentId>(v.source, r);
    o.controller_generation = decode_value<data::GenerationId>(v.controller_generation, r);
    o.sequence = decode_value<data::EventSequence>(v.sequence, r);
    return o;
}
MantisTriggerKeyV1 encode(const data::TriggerKey &v, Writer &w) {
    MantisTriggerKeyV1 o{};
    init(o);
    o.run_id = encode_value(v.run_id, w);
    o.source = encode_value(v.source, w);
    o.controller_generation = encode_value(v.controller_generation, w);
    o.sequence = encode_value(v.sequence, w);
    return o;
}
data::NativeTriggerIdentity decode(const MantisNativeTriggerIdentityV1 &v, Reader &r) {
    r.prefix(v);
    data::NativeTriggerIdentity o;
    o.controller = decode_value<data::ComponentId>(v.controller, r);
    o.generation = decode_value<data::GenerationId>(v.generation, r);
    o.value = decode_value<uint64_t>(v.value, r);
    return o;
}
MantisNativeTriggerIdentityV1 encode(const data::NativeTriggerIdentity &v, Writer &w) {
    MantisNativeTriggerIdentityV1 o{};
    init(o);
    o.controller = encode_value(v.controller, w);
    o.generation = encode_value(v.generation, w);
    o.value = encode_value(v.value, w);
    return o;
}
data::ContentReference decode(const MantisContentReferenceV1 &v, Reader &r) {
    r.prefix(v);
    data::ContentReference o;
    o.id = decode_value<Id>(v.id, r);
    o.type = decode_value<schema::DataTypeId>(v.type, r);
    o.hash = decode_evidence<Hash>(v.hash, r);
    o.revision = decode_value<uint64_t>(v.revision, r);
    return o;
}
MantisContentReferenceV1 encode(const data::ContentReference &v, Writer &w) {
    MantisContentReferenceV1 o{};
    init(o);
    o.id = encode_value(v.id, w);
    o.type = encode_value(v.type, w);
    o.hash = encode_evidence<MantisEvidenceHashV1>(v.hash, w);
    o.revision = encode_value(v.revision, w);
    return o;
}
data::ProgramReference decode(const MantisProgramReferenceV1 &v, Reader &r) {
    r.prefix(v);
    data::ProgramReference o;
    o.id = decode_value<data::ProgramId>(v.id, r);
    o.hash = decode_evidence<Hash>(v.hash, r);
    o.content = decode_evidence<data::ContentReference>(v.content, r);
    return o;
}
MantisProgramReferenceV1 encode(const data::ProgramReference &v, Writer &w) {
    MantisProgramReferenceV1 o{};
    init(o);
    o.id = encode_value(v.id, w);
    o.hash = encode_evidence<MantisEvidenceHashV1>(v.hash, w);
    o.content = encode_evidence<MantisEvidenceContentReferenceV1>(v.content, w);
    return o;
}
data::ExactCalibrationReference decode(const MantisExactCalibrationReferenceV1 &v, Reader &r) {
    r.prefix(v);
    data::ExactCalibrationReference o;
    o.calibration = decode_value<calibration::Reference>(v.calibration, r);
    o.content = decode_evidence<data::ContentReference>(v.content, r);
    return o;
}
MantisExactCalibrationReferenceV1 encode(const data::ExactCalibrationReference &v, Writer &w) {
    MantisExactCalibrationReferenceV1 o{};
    init(o);
    o.calibration = encode_value(v.calibration, w);
    o.content = encode_evidence<MantisEvidenceContentReferenceV1>(v.content, w);
    return o;
}
data::ClockIdentity decode(const MantisClockIdentityV1 &v, Reader &r) {
    r.prefix(v);
    data::ClockIdentity o;
    o.domain = decode_value<time::ClockDomain>(v.domain, r);
    o.generation = decode_value<data::GenerationId>(v.generation, r);
    return o;
}
MantisClockIdentityV1 encode(const data::ClockIdentity &v, Writer &w) {
    MantisClockIdentityV1 o{};
    init(o);
    o.domain = encode_value(v.domain, w);
    o.generation = encode_value(v.generation, w);
    return o;
}
data::SemanticTimestamp decode(const MantisSemanticTimestampV1 &v, Reader &r) {
    r.prefix(v);
    data::SemanticTimestamp o;
    o.nanoseconds = decode_value<int64_t>(v.nanoseconds, r);
    o.clock = decode_value<data::ClockIdentity>(v.clock, r);
    return o;
}
MantisSemanticTimestampV1 encode(const data::SemanticTimestamp &v, Writer &w) {
    MantisSemanticTimestampV1 o{};
    init(o);
    o.nanoseconds = encode_value(v.nanoseconds, w);
    o.clock = encode_value(v.clock, w);
    return o;
}
data::RuntimeTimestamp decode(const MantisRuntimeTimestampV1 &v, Reader &r) {
    r.prefix(v);
    data::RuntimeTimestamp o;
    o.time = decode_value<time::MonotonicTimestamp>(v.time, r);
    o.clock = decode_value<data::ClockIdentity>(v.clock, r);
    return o;
}
MantisRuntimeTimestampV1 encode(const data::RuntimeTimestamp &v, Writer &w) {
    MantisRuntimeTimestampV1 o{};
    init(o);
    o.time = encode_value(v.time, w);
    o.clock = encode_value(v.clock, w);
    return o;
}
data::TimeInterval decode(const MantisTimeIntervalV1 &v, Reader &r) {
    r.prefix(v);
    data::TimeInterval o;
    o.start = decode_value<data::SemanticTimestamp>(v.start, r);
    o.end = decode_value<data::SemanticTimestamp>(v.end, r);
    return o;
}
MantisTimeIntervalV1 encode(const data::TimeInterval &v, Writer &w) {
    MantisTimeIntervalV1 o{};
    init(o);
    o.start = encode_value(v.start, w);
    o.end = encode_value(v.end, w);
    return o;
}
data::ClockMappingEvidence decode(const MantisClockMappingEvidenceV1 &v, Reader &r) {
    r.prefix(v);
    data::ClockMappingEvidence o;
    o.mapping = decode_value<time::ClockMapping>(v.mapping, r);
    o.source_generation = decode_value<data::GenerationId>(v.source_generation, r);
    o.target_generation = decode_value<data::GenerationId>(v.target_generation, r);
    o.reference = decode_value<data::ContentReference>(v.reference, r);
    return o;
}
MantisClockMappingEvidenceV1 encode(const data::ClockMappingEvidence &v, Writer &w) {
    MantisClockMappingEvidenceV1 o{};
    init(o);
    o.mapping = encode_value(v.mapping, w);
    o.source_generation = encode_value(v.source_generation, w);
    o.target_generation = encode_value(v.target_generation, w);
    o.reference = encode_value(v.reference, w);
    return o;
}
data::EvidenceSource decode(const MantisEvidenceSourceV1 &v, Reader &r) {
    r.prefix(v);
    data::EvidenceSource o;
    o.source = decode_value<data::ComponentId>(v.source, r);
    o.method = decode_value<data::EvidenceMethod>(v.method, r);
    o.reference = decode_evidence<data::ContentReference>(v.reference, r);
    return o;
}
MantisEvidenceSourceV1 encode(const data::EvidenceSource &v, Writer &w) {
    MantisEvidenceSourceV1 o{};
    init(o);
    o.source = encode_value(v.source, w);
    o.method = encode_value(v.method, w);
    o.reference = encode_evidence<MantisEvidenceContentReferenceV1>(v.reference, w);
    return o;
}
data::Acknowledgement decode(const MantisAcknowledgementV1 &v, Reader &r) {
    r.prefix(v);
    data::Acknowledgement o;
    o.request = decode_value<data::RequestId>(v.request, r);
    o.stage = decode_required<data::AcknowledgementStage>(v.stage, r);
    o.result = decode_required<data::AcknowledgementResult>(v.result, r);
    o.evidence = decode_value<data::EvidenceSource>(v.evidence, r);
    o.time = decode_evidence<data::SemanticTimestamp>(v.time, r);
    o.scope = decode_value<data::EvidenceScope>(v.scope, r);
    return o;
}
MantisAcknowledgementV1 encode(const data::Acknowledgement &v, Writer &w) {
    MantisAcknowledgementV1 o{};
    init(o);
    o.request = encode_value(v.request, w);
    o.stage = encode_required<MantisEvidenceAcknowledgementStageV1>(v.stage, w);
    o.result = encode_required<MantisEvidenceAcknowledgementResultV1>(v.result, w);
    o.evidence = encode_value(v.evidence, w);
    o.time = encode_evidence<MantisEvidenceSemanticTimestampV1>(v.time, w);
    o.scope = encode_value(v.scope, w);
    return o;
}
data::EmitterCommand decode(const MantisEmitterCommandV1 &v, Reader &r) {
    r.prefix(v);
    data::EmitterCommand o;
    o.request = decode_value<data::RequestId>(v.request, r);
    o.target = decode_value<data::ComponentId>(v.target, r);
    o.state = decode_value<data::EmitterState>(v.state, r);
    o.dispatched = decode_value<data::RuntimeTimestamp>(v.dispatched, r);
    return o;
}
MantisEmitterCommandV1 encode(const data::EmitterCommand &v, Writer &w) {
    MantisEmitterCommandV1 o{};
    init(o);
    o.request = encode_value(v.request, w);
    o.target = encode_value(v.target, w);
    o.state = encode_value(v.state, w);
    o.dispatched = encode_value(v.dispatched, w);
    return o;
}
data::StateObservation decode(const MantisStateObservationV1 &v, Reader &r) {
    r.prefix(v);
    data::StateObservation o;
    o.state = decode_required<data::EmitterState>(v.state, r);
    o.scope = decode_value<data::EvidenceScope>(v.scope, r);
    o.evidence = decode_value<data::EvidenceSource>(v.evidence, r);
    o.time = decode_evidence<data::SemanticTimestamp>(v.time, r);
    o.coverage = decode_evidence<data::TimeInterval>(v.coverage, r);
    return o;
}
MantisStateObservationV1 encode(const data::StateObservation &v, Writer &w) {
    MantisStateObservationV1 o{};
    init(o);
    o.state = encode_required<MantisEvidenceEmitterStateV1>(v.state, w);
    o.scope = encode_value(v.scope, w);
    o.evidence = encode_value(v.evidence, w);
    o.time = encode_evidence<MantisEvidenceSemanticTimestampV1>(v.time, w);
    o.coverage = encode_evidence<MantisEvidenceTimeIntervalV1>(v.coverage, w);
    return o;
}
data::ExposureEvidence decode(const MantisExposureEvidenceV1 &v, Reader &r) {
    r.prefix(v);
    data::ExposureEvidence o;
    o.requested_duration = decode_evidence<data::Duration>(v.requested_duration, r);
    o.startup_readback_duration = decode_evidence<data::Duration>(v.startup_readback_duration, r);
    o.integration_duration = decode_evidence<data::Duration>(v.integration_duration, r);
    o.interval = decode_evidence<data::TimeInterval>(v.interval, r);
    o.evidence = decode_value<data::EvidenceSource>(v.evidence, r);
    o.uncertainty_ns = decode_evidence<double>(v.uncertainty_ns, r);
    return o;
}
MantisExposureEvidenceV1 encode(const data::ExposureEvidence &v, Writer &w) {
    MantisExposureEvidenceV1 o{};
    init(o);
    o.requested_duration = encode_evidence<MantisEvidenceDurationV1>(v.requested_duration, w);
    o.startup_readback_duration = encode_evidence<MantisEvidenceDurationV1>(v.startup_readback_duration, w);
    o.integration_duration = encode_evidence<MantisEvidenceDurationV1>(v.integration_duration, w);
    o.interval = encode_evidence<MantisEvidenceTimeIntervalV1>(v.interval, w);
    o.evidence = encode_value(v.evidence, w);
    o.uncertainty_ns = encode_evidence<MantisEvidenceFloat64V1>(v.uncertainty_ns, w);
    return o;
}
data::ExposureAssociation decode(const MantisExposureAssociationV1 &v, Reader &r) {
    r.prefix(v);
    data::ExposureAssociation o;
    o.frame = decode_value<data::SourceFrameKey>(v.frame, r);
    o.trigger = decode_value<data::TriggerKey>(v.trigger, r);
    o.method = decode_value<data::AssociationMethod>(v.method, r);
    o.evidence = decode_value<data::EvidenceSource>(v.evidence, r);
    o.native_trigger = decode_evidence<data::NativeTriggerIdentity>(v.native_trigger, r);
    return o;
}
MantisExposureAssociationV1 encode(const data::ExposureAssociation &v, Writer &w) {
    MantisExposureAssociationV1 o{};
    init(o);
    o.frame = encode_value(v.frame, w);
    o.trigger = encode_value(v.trigger, w);
    o.method = encode_value(v.method, w);
    o.evidence = encode_value(v.evidence, w);
    o.native_trigger = encode_evidence<MantisEvidenceNativeTriggerIdentityV1>(v.native_trigger, w);
    return o;
}
data::SyncEvidence decode(const MantisSyncEvidenceV1 &v, Reader &r) {
    r.prefix(v);
    data::SyncEvidence o;
    o.group = decode_value<time::SyncGroup>(v.group, r);
    o.quality = decode_value<time::SyncQuality>(v.quality, r);
    o.hardware_association = decode_evidence<data::ExposureAssociation>(v.hardware_association, r);
    return o;
}
MantisSyncEvidenceV1 encode(const data::SyncEvidence &v, Writer &w) {
    MantisSyncEvidenceV1 o{};
    init(o);
    o.group = encode_value(v.group, w);
    o.quality = encode_value(v.quality, w);
    o.hardware_association = encode_evidence<MantisEvidenceExposureAssociationV1>(v.hardware_association, w);
    return o;
}
data::CameraFrameEvidence decode(const MantisCameraFrameEvidenceV1 &v, Reader &r) {
    r.prefix(v);
    data::CameraFrameEvidence o;
    o.frame = decode_value<data::SourceFrameKey>(v.frame, r);
    o.camera_role = decode_value<std::string>(v.camera_role, r);
    o.width = decode_value<uint32_t>(v.width, r);
    o.height = decode_value<uint32_t>(v.height, r);
    o.source_timestamp = decode_evidence<data::SemanticTimestamp>(v.source_timestamp, r);
    o.host_received = decode_evidence<data::RuntimeTimestamp>(v.host_received, r);
    o.timestamp_meaning = decode_evidence<data::TimestampMeaning>(v.timestamp_meaning, r);
    o.exposure = decode_evidence<data::ExposureEvidence>(v.exposure, r);
    o.sync = decode_evidence<data::SyncEvidence>(v.sync, r);
    o.camera_calibration = decode_evidence<data::ExactCalibrationReference>(v.camera_calibration, r);
    o.rig_calibration = decode_evidence<data::ExactCalibrationReference>(v.rig_calibration, r);
    o.original_calibration = decode_evidence<data::ExactCalibrationReference>(v.original_calibration, r);
    return o;
}
MantisCameraFrameEvidenceV1 encode(const data::CameraFrameEvidence &v, Writer &w) {
    MantisCameraFrameEvidenceV1 o{};
    init(o);
    o.frame = encode_value(v.frame, w);
    o.camera_role = encode_value(v.camera_role, w);
    o.width = encode_value(v.width, w);
    o.height = encode_value(v.height, w);
    o.source_timestamp = encode_evidence<MantisEvidenceSemanticTimestampV1>(v.source_timestamp, w);
    o.host_received = encode_evidence<MantisEvidenceRuntimeTimestampV1>(v.host_received, w);
    o.timestamp_meaning = encode_evidence<MantisEvidenceTimestampMeaningV1>(v.timestamp_meaning, w);
    o.exposure = encode_evidence<MantisEvidenceExposureEvidenceV1>(v.exposure, w);
    o.sync = encode_evidence<MantisEvidenceSyncEvidenceV1>(v.sync, w);
    o.camera_calibration = encode_evidence<MantisEvidenceExactCalibrationReferenceV1>(v.camera_calibration, w);
    o.rig_calibration = encode_evidence<MantisEvidenceExactCalibrationReferenceV1>(v.rig_calibration, w);
    o.original_calibration = encode_evidence<MantisEvidenceExactCalibrationReferenceV1>(v.original_calibration, w);
    return o;
}
data::ExposureEffectiveState decode(const MantisExposureEffectiveStateV1 &v, Reader &r) {
    r.prefix(v);
    data::ExposureEffectiveState o;
    o.frame = decode_value<data::SourceFrameKey>(v.frame, r);
    o.state = decode_required<data::EmitterState>(v.state, r);
    o.scope = decode_value<data::EvidenceScope>(v.scope, r);
    o.evidence = decode_value<data::EvidenceSource>(v.evidence, r);
    o.coverage = decode_value<data::TimeInterval>(v.coverage, r);
    return o;
}
MantisExposureEffectiveStateV1 encode(const data::ExposureEffectiveState &v, Writer &w) {
    MantisExposureEffectiveStateV1 o{};
    init(o);
    o.frame = encode_value(v.frame, w);
    o.state = encode_required<MantisEvidenceEmitterStateV1>(v.state, w);
    o.scope = encode_value(v.scope, w);
    o.evidence = encode_value(v.evidence, w);
    o.coverage = encode_value(v.coverage, w);
    return o;
}
data::CameraEffectiveState decode(const MantisCameraEffectiveStateV1 &v, Reader &r) {
    r.prefix(v);
    data::CameraEffectiveState o;
    o.frame = decode_value<data::SourceFrameKey>(v.frame, r);
    o.state = decode_evidence<data::ExposureEffectiveState>(v.state, r);
    return o;
}
MantisCameraEffectiveStateV1 encode(const data::CameraEffectiveState &v, Writer &w) {
    MantisCameraEffectiveStateV1 o{};
    init(o);
    o.frame = encode_value(v.frame, w);
    o.state = encode_evidence<MantisEvidenceExposureEffectiveStateV1>(v.state, w);
    return o;
}
data::EmitterEvidence decode(const MantisEmitterEvidenceV1 &v, Reader &r) {
    r.prefix(v);
    data::EmitterEvidence o;
    o.emitter = decode_value<data::ComponentId>(v.emitter, r);
    o.commanded = decode_evidence<data::EmitterCommand>(v.commanded, r);
    o.acknowledged = decode_evidence<data::Acknowledgement>(v.acknowledged, r);
    o.observed = decode_evidence<data::StateObservation>(v.observed, r);
    o.exposure_effective = decode_array<data::CameraEffectiveState>(v.exposure_effective, v.exposure_effective_count, r, 16);
    return o;
}
MantisEmitterEvidenceV1 encode(const data::EmitterEvidence &v, Writer &w) {
    MantisEmitterEvidenceV1 o{};
    init(o);
    o.emitter = encode_value(v.emitter, w);
    o.commanded = encode_evidence<MantisEvidenceEmitterCommandV1>(v.commanded, w);
    o.acknowledged = encode_evidence<MantisEvidenceAcknowledgementV1>(v.acknowledged, w);
    o.observed = encode_evidence<MantisEvidenceStateObservationV1>(v.observed, w);
    o.exposure_effective_count = count(v.exposure_effective.size());
    o.exposure_effective = encode_array<MantisCameraEffectiveStateV1>(v.exposure_effective, w);
    return o;
}
data::CameraParticipant decode(const MantisCameraParticipantV1 &v, Reader &r) {
    r.prefix(v);
    data::CameraParticipant o;
    o.component = decode_value<data::ComponentId>(v.component, r);
    o.stream = decode_value<data::StreamId>(v.stream, r);
    o.role = decode_value<std::string>(v.role, r);
    return o;
}
MantisCameraParticipantV1 encode(const data::CameraParticipant &v, Writer &w) {
    MantisCameraParticipantV1 o{};
    init(o);
    o.component = encode_value(v.component, w);
    o.stream = encode_value(v.stream, w);
    o.role = encode_value(v.role, w);
    return o;
}
data::Participants decode(const MantisParticipantsV1 &v, Reader &r) {
    r.prefix(v);
    data::Participants o;
    o.cameras = decode_array<data::CameraParticipant>(v.cameras, v.cameras_count, r, 16);
    o.emitters = decode_array<data::ComponentId>(v.emitters, v.emitters_count, r, 64);
    o.controllers = decode_array<data::ComponentId>(v.controllers, v.controllers_count, r, 64);
    return o;
}
MantisParticipantsV1 encode(const data::Participants &v, Writer &w) {
    MantisParticipantsV1 o{};
    init(o);
    o.cameras_count = count(v.cameras.size());
    o.cameras = encode_array<MantisCameraParticipantV1>(v.cameras, w);
    o.emitters_count = count(v.emitters.size());
    o.emitters = encode_array<const char *>(v.emitters, w);
    o.controllers_count = count(v.controllers.size());
    o.controllers = encode_array<const char *>(v.controllers, w);
    return o;
}
data::TriggerIntent decode(const MantisTriggerIntentV1 &v, Reader &r) {
    r.prefix(v);
    data::TriggerIntent o;
    o.controller = decode_value<data::ComponentId>(v.controller, r);
    o.request = decode_value<data::RequestId>(v.request, r);
    o.endpoints = decode_array<data::ComponentId>(v.endpoints, v.endpoints_count, r, 64);
    return o;
}
MantisTriggerIntentV1 encode(const data::TriggerIntent &v, Writer &w) {
    MantisTriggerIntentV1 o{};
    init(o);
    o.controller = encode_value(v.controller, w);
    o.request = encode_value(v.request, w);
    o.endpoints_count = count(v.endpoints.size());
    o.endpoints = encode_array<const char *>(v.endpoints, w);
    return o;
}
data::CaptureIntent decode(const MantisCaptureIntentV1 &v, Reader &r) {
    r.prefix(v);
    data::CaptureIntent o;
    o.mode = decode_required<data::CaptureMode>(v.mode, r);
    o.cameras = decode_array<data::ComponentId>(v.cameras, v.cameras_count, r, 16);
    o.trigger = decode_optional<data::TriggerIntent>(v.trigger, r);
    return o;
}
MantisCaptureIntentV1 encode(const data::CaptureIntent &v, Writer &w) {
    MantisCaptureIntentV1 o{};
    init(o);
    o.mode = encode_required<MantisEvidenceCaptureModeV1>(v.mode, w);
    o.cameras_count = count(v.cameras.size());
    o.cameras = encode_array<const char *>(v.cameras, w);
    o.trigger = encode_optional<MantisTriggerIntentV1>(v.trigger, w);
    return o;
}
data::EmitterIntent decode(const MantisEmitterIntentV1 &v, Reader &r) {
    r.prefix(v);
    data::EmitterIntent o;
    o.emitter = decode_value<data::ComponentId>(v.emitter, r);
    o.state = decode_value<data::EmitterState>(v.state, r);
    return o;
}
MantisEmitterIntentV1 encode(const data::EmitterIntent &v, Writer &w) {
    MantisEmitterIntentV1 o{};
    init(o);
    o.emitter = encode_value(v.emitter, w);
    o.state = encode_value(v.state, w);
    return o;
}
data::AcquisitionStep decode(const MantisAcquisitionStepV1 &v, Reader &r) {
    r.prefix(v);
    data::AcquisitionStep o;
    o.index = decode_value<uint32_t>(v.index, r);
    o.label = decode_value<std::string>(v.label, r);
    o.emitters = decode_array<data::EmitterIntent>(v.emitters, v.emitters_count, r, 64);
    o.capture = decode_value<data::CaptureIntent>(v.capture, r);
    o.evidence_requirement = decode_required<data::EvidenceRequirement>(v.evidence_requirement, r);
    o.required_scope = decode_value<data::EvidenceScope>(v.required_scope, r);
    o.settle = decode_value<data::Duration>(v.settle, r);
    o.max_duration = decode_value<data::Duration>(v.max_duration, r);
    return o;
}
MantisAcquisitionStepV1 encode(const data::AcquisitionStep &v, Writer &w) {
    MantisAcquisitionStepV1 o{};
    init(o);
    o.index = encode_value(v.index, w);
    o.label = encode_value(v.label, w);
    o.emitters_count = count(v.emitters.size());
    o.emitters = encode_array<MantisEmitterIntentV1>(v.emitters, w);
    o.capture = encode_value(v.capture, w);
    o.evidence_requirement = encode_required<MantisEvidenceEvidenceRequirementV1>(v.evidence_requirement, w);
    o.required_scope = encode_value(v.required_scope, w);
    o.settle = encode_value(v.settle, w);
    o.max_duration = encode_value(v.max_duration, w);
    return o;
}
data::RunBounds decode(const MantisRunBoundsV1 &v, Reader &r) {
    r.prefix(v);
    data::RunBounds o;
    o.max_duration = decode_value<data::Duration>(v.max_duration, r);
    o.max_on_duration = decode_value<data::Duration>(v.max_on_duration, r);
    o.max_step_instances = decode_value<uint64_t>(v.max_step_instances, r);
    o.max_commands = decode_value<uint64_t>(v.max_commands, r);
    o.max_events = decode_value<uint64_t>(v.max_events, r);
    o.max_bytes = decode_value<uint64_t>(v.max_bytes, r);
    o.max_in_flight_captures = decode_value<uint64_t>(v.max_in_flight_captures, r);
    return o;
}
MantisRunBoundsV1 encode(const data::RunBounds &v, Writer &w) {
    MantisRunBoundsV1 o{};
    init(o);
    o.max_duration = encode_value(v.max_duration, w);
    o.max_on_duration = encode_value(v.max_on_duration, w);
    o.max_step_instances = encode_value(v.max_step_instances, w);
    o.max_commands = encode_value(v.max_commands, w);
    o.max_events = encode_value(v.max_events, w);
    o.max_bytes = encode_value(v.max_bytes, w);
    o.max_in_flight_captures = encode_value(v.max_in_flight_captures, w);
    return o;
}
data::AcquisitionProgram decode(const MantisAcquisitionProgramV1 &v, Reader &r) {
    r.prefix(v);
    data::AcquisitionProgram o;
    o.type = decode_value<schema::DataTypeId>(v.type, r);
    o.identity = decode_value<data::ProgramReference>(v.identity, r);
    o.participants = decode_value<data::Participants>(v.participants, r);
    o.steps = decode_array<data::AcquisitionStep>(v.steps, v.steps_count, r, 256);
    o.repetitions = decode_value<uint64_t>(v.repetitions, r);
    o.bounds = decode_value<data::RunBounds>(v.bounds, r);
    require(v.terminal_policy == MANTIS_TERMINAL_INHIBIT_AND_ALL_OFF, "Invalid terminal policy");
    return o;
}
MantisAcquisitionProgramV1 encode(const data::AcquisitionProgram &v, Writer &w) {
    MantisAcquisitionProgramV1 o{};
    init(o);
    o.type = encode_value(v.type, w);
    o.identity = encode_value(v.identity, w);
    o.participants = encode_value(v.participants, w);
    o.steps_count = count(v.steps.size());
    o.steps = encode_array<MantisAcquisitionStepV1>(v.steps, w);
    o.repetitions = encode_value(v.repetitions, w);
    o.bounds = encode_value(v.bounds, w);
    o.terminal_policy = MANTIS_TERMINAL_INHIBIT_AND_ALL_OFF;
    return o;
}
data::TriggerEvent decode(const MantisTriggerEventV1 &v, Reader &r) {
    r.prefix(v);
    data::TriggerEvent o;
    o.type = decode_value<schema::DataTypeId>(v.type, r);
    o.key = decode_value<data::TriggerKey>(v.key, r);
    o.step = decode_value<data::StepInstance>(v.step, r);
    o.request = decode_value<data::RequestId>(v.request, r);
    o.native_trigger = decode_evidence<data::NativeTriggerIdentity>(v.native_trigger, r);
    o.kind = decode_required<data::TriggerEvent::Kind>(v.kind, r);
    o.acknowledgement = decode_evidence<data::Acknowledgement>(v.acknowledgement, r);
    o.evidence = decode_value<data::EvidenceSource>(v.evidence, r);
    o.device_time = decode_evidence<data::SemanticTimestamp>(v.device_time, r);
    o.host_received = decode_evidence<data::RuntimeTimestamp>(v.host_received, r);
    o.host_dispatched = decode_evidence<data::RuntimeTimestamp>(v.host_dispatched, r);
    o.uncertainty_ns = decode_evidence<double>(v.uncertainty_ns, r);
    o.clock_mapping = decode_evidence<data::ClockMappingEvidence>(v.clock_mapping, r);
    o.intended_endpoints = decode_array<data::ComponentId>(v.intended_endpoints, v.intended_endpoints_count, r, 64);
    o.actual_endpoints = decode_evidence<std::vector<data::ComponentId>>(v.actual_endpoints, r);
    o.requested_exposure = decode_evidence<data::SourceFrameKey>(v.requested_exposure, r);
    o.exposure_association = decode_evidence<data::ExposureAssociation>(v.exposure_association, r);
    return o;
}
MantisTriggerEventV1 encode(const data::TriggerEvent &v, Writer &w) {
    MantisTriggerEventV1 o{};
    init(o);
    o.type = encode_value(v.type, w);
    o.key = encode_value(v.key, w);
    o.step = encode_value(v.step, w);
    o.request = encode_value(v.request, w);
    o.native_trigger = encode_evidence<MantisEvidenceNativeTriggerIdentityV1>(v.native_trigger, w);
    o.kind = encode_required<MantisEvidenceTriggerKindV1>(v.kind, w);
    o.acknowledgement = encode_evidence<MantisEvidenceAcknowledgementV1>(v.acknowledgement, w);
    o.evidence = encode_value(v.evidence, w);
    o.device_time = encode_evidence<MantisEvidenceSemanticTimestampV1>(v.device_time, w);
    o.host_received = encode_evidence<MantisEvidenceRuntimeTimestampV1>(v.host_received, w);
    o.host_dispatched = encode_evidence<MantisEvidenceRuntimeTimestampV1>(v.host_dispatched, w);
    o.uncertainty_ns = encode_evidence<MantisEvidenceFloat64V1>(v.uncertainty_ns, w);
    o.clock_mapping = encode_evidence<MantisEvidenceClockMappingEvidenceV1>(v.clock_mapping, w);
    o.intended_endpoints_count = count(v.intended_endpoints.size());
    o.intended_endpoints = encode_array<const char *>(v.intended_endpoints, w);
    o.actual_endpoints = encode_evidence<MantisEvidenceComponentListV1>(v.actual_endpoints, w);
    o.requested_exposure = encode_evidence<MantisEvidenceSourceFrameKeyV1>(v.requested_exposure, w);
    o.exposure_association = encode_evidence<MantisEvidenceExposureAssociationV1>(v.exposure_association, w);
    return o;
}
data::ImplementationIdentity decode(const MantisImplementationIdentityV1 &v, Reader &r) {
    r.prefix(v);
    data::ImplementationIdentity o;
    o.implementation = decode_value<Id>(v.implementation, r);
    o.version = decode_value<SemanticVersion>(v.version, r);
    o.build = decode_value<std::string>(v.build, r);
    o.configuration = decode_evidence<data::ContentReference>(v.configuration, r);
    return o;
}
MantisImplementationIdentityV1 encode(const data::ImplementationIdentity &v, Writer &w) {
    MantisImplementationIdentityV1 o{};
    init(o);
    o.implementation = encode_value(v.implementation, w);
    o.version = encode_value(v.version, w);
    o.build = encode_value(v.build, w);
    o.configuration = encode_evidence<MantisEvidenceContentReferenceV1>(v.configuration, w);
    return o;
}
data::LossAccounting decode(const MantisLossAccountingV1 &v, Reader &r) {
    r.prefix(v);
    data::LossAccounting o;
    o.kind = decode_value<data::LossKind>(v.kind, r);
    o.source = decode_value<data::ComponentId>(v.source, r);
    o.count = decode_evidence<uint64_t>(v.count, r);
    o.reason = decode_value<data::AcquisitionReason>(v.reason, r);
    o.frame = decode_evidence<data::SourceFrameKey>(v.frame, r);
    o.request = decode_evidence<data::RequestId>(v.request, r);
    return o;
}
MantisLossAccountingV1 encode(const data::LossAccounting &v, Writer &w) {
    MantisLossAccountingV1 o{};
    init(o);
    o.kind = encode_value(v.kind, w);
    o.source = encode_value(v.source, w);
    o.count = encode_evidence<MantisEvidenceUInt64V1>(v.count, w);
    o.reason = encode_value(v.reason, w);
    o.frame = encode_evidence<MantisEvidenceSourceFrameKeyV1>(v.frame, w);
    o.request = encode_evidence<MantisEvidenceRequestIdV1>(v.request, w);
    return o;
}
data::UnresolvedRequest decode(const MantisUnresolvedRequestV1 &v, Reader &r) {
    r.prefix(v);
    data::UnresolvedRequest o;
    o.request = decode_value<data::RequestId>(v.request, r);
    o.target = decode_value<data::ComponentId>(v.target, r);
    o.reason = decode_value<data::AcquisitionReason>(v.reason, r);
    return o;
}
MantisUnresolvedRequestV1 encode(const data::UnresolvedRequest &v, Writer &w) {
    MantisUnresolvedRequestV1 o{};
    init(o);
    o.request = encode_value(v.request, w);
    o.target = encode_value(v.target, w);
    o.reason = encode_value(v.reason, w);
    return o;
}
data::AcquisitionEvidence decode(const MantisAcquisitionEvidenceV1 &v, Reader &r) {
    r.prefix(v);
    data::AcquisitionEvidence o;
    o.type = decode_value<schema::DataTypeId>(v.type, r);
    o.key = decode_value<data::EvidenceKey>(v.key, r);
    o.program = decode_value<data::ProgramReference>(v.program, r);
    o.step = decode_evidence<data::StepInstance>(v.step, r);
    o.causal_predecessors = decode_array<data::EvidenceKey>(v.causal_predecessors, v.causal_predecessors_count, r, 4096);
    o.participants = decode_value<data::Participants>(v.participants, r);
    o.implementations = decode_array<data::ImplementationIdentity>(v.implementations, v.implementations_count, r, 64);
    o.emitters = decode_array<data::EmitterEvidence>(v.emitters, v.emitters_count, r, 64);
    o.frameset = decode_evidence<data::FrameSetKey>(v.frameset, r);
    o.frames = decode_array<data::CameraFrameEvidence>(v.frames, v.frames_count, r, 16);
    o.triggers = decode_array<data::TriggerKey>(v.triggers, v.triggers_count, r, 4096);
    o.clock_mappings = decode_array<data::ClockMappingEvidence>(v.clock_mappings, v.clock_mappings_count, r, 64);
    o.rig_calibration = decode_evidence<data::ExactCalibrationReference>(v.rig_calibration, r);
    o.disposition = decode_required<data::AcquisitionDisposition>(v.disposition, r);
    o.reason = decode_value<data::AcquisitionReason>(v.reason, r);
    o.unresolved_requests = decode_array<data::UnresolvedRequest>(v.unresolved_requests, v.unresolved_requests_count, r, 4096);
    o.losses = decode_array<data::LossAccounting>(v.losses, v.losses_count, r, 4096);
    o.diagnostic = decode_value<std::string>(v.diagnostic, r);
    return o;
}
MantisAcquisitionEvidenceV1 encode(const data::AcquisitionEvidence &v, Writer &w) {
    MantisAcquisitionEvidenceV1 o{};
    init(o);
    o.type = encode_value(v.type, w);
    o.key = encode_value(v.key, w);
    o.program = encode_value(v.program, w);
    o.step = encode_evidence<MantisEvidenceStepInstanceV1>(v.step, w);
    o.causal_predecessors_count = count(v.causal_predecessors.size());
    o.causal_predecessors = encode_array<MantisEvidenceKeyV1>(v.causal_predecessors, w);
    o.participants = encode_value(v.participants, w);
    o.implementations_count = count(v.implementations.size());
    o.implementations = encode_array<MantisImplementationIdentityV1>(v.implementations, w);
    o.emitters_count = count(v.emitters.size());
    o.emitters = encode_array<MantisEmitterEvidenceV1>(v.emitters, w);
    o.frameset = encode_evidence<MantisEvidenceFrameSetKeyV1>(v.frameset, w);
    o.frames_count = count(v.frames.size());
    o.frames = encode_array<MantisCameraFrameEvidenceV1>(v.frames, w);
    o.triggers_count = count(v.triggers.size());
    o.triggers = encode_array<MantisTriggerKeyV1>(v.triggers, w);
    o.clock_mappings_count = count(v.clock_mappings.size());
    o.clock_mappings = encode_array<MantisClockMappingEvidenceV1>(v.clock_mappings, w);
    o.rig_calibration = encode_evidence<MantisEvidenceExactCalibrationReferenceV1>(v.rig_calibration, w);
    o.disposition = encode_required<MantisEvidenceAcquisitionDispositionV1>(v.disposition, w);
    o.reason = encode_value(v.reason, w);
    o.unresolved_requests_count = count(v.unresolved_requests.size());
    o.unresolved_requests = encode_array<MantisUnresolvedRequestV1>(v.unresolved_requests, w);
    o.losses_count = count(v.losses.size());
    o.losses = encode_array<MantisLossAccountingV1>(v.losses, w);
    o.diagnostic = encode_value(v.diagnostic, w);
    return o;
}
data::EmitterPatternIdentity decode(const MantisEmitterPatternIdentityV1 &v, Reader &r) {
    r.prefix(v);
    data::EmitterPatternIdentity o;
    o.emitter = decode_value<data::ComponentId>(v.emitter, r);
    o.pattern = decode_value<data::PatternId>(v.pattern, r);
    o.revision = decode_value<uint64_t>(v.revision, r);
    return o;
}
MantisEmitterPatternIdentityV1 encode(const data::EmitterPatternIdentity &v, Writer &w) {
    MantisEmitterPatternIdentityV1 o{};
    init(o);
    o.emitter = encode_value(v.emitter, w);
    o.pattern = encode_value(v.pattern, w);
    o.revision = encode_value(v.revision, w);
    return o;
}
data::LineIdentity decode(const MantisLineIdentityV1 &v, Reader &r) {
    r.prefix(v);
    data::LineIdentity o;
    o.emitter = decode_value<data::ComponentId>(v.emitter, r);
    o.pattern = decode_value<data::PatternId>(v.pattern, r);
    o.pattern_revision = decode_value<uint64_t>(v.pattern_revision, r);
    o.local_line = decode_value<data::LineId>(v.local_line, r);
    return o;
}
MantisLineIdentityV1 encode(const data::LineIdentity &v, Writer &w) {
    MantisLineIdentityV1 o{};
    init(o);
    o.emitter = encode_value(v.emitter, w);
    o.pattern = encode_value(v.pattern, w);
    o.pattern_revision = encode_value(v.pattern_revision, w);
    o.local_line = encode_value(v.local_line, w);
    return o;
}
data::ProgramCorrelation decode(const MantisProgramCorrelationV1 &v, Reader &r) {
    r.prefix(v);
    data::ProgramCorrelation o;
    o.program = decode_value<data::ProgramReference>(v.program, r);
    o.step = decode_value<data::StepInstance>(v.step, r);
    return o;
}
MantisProgramCorrelationV1 encode(const data::ProgramCorrelation &v, Writer &w) {
    MantisProgramCorrelationV1 o{};
    init(o);
    o.program = encode_value(v.program, w);
    o.step = encode_value(v.step, w);
    return o;
}
data::PreprocessingTransform decode(const MantisPreprocessingTransformV1 &v, Reader &r) {
    r.prefix(v);
    data::PreprocessingTransform o;
    o.original_from_processed = decode_matrix(v.original_from_processed);
    o.reference = decode_evidence<data::ContentReference>(v.reference, r);
    return o;
}
MantisPreprocessingTransformV1 encode(const data::PreprocessingTransform &v, Writer &w) {
    MantisPreprocessingTransformV1 o{};
    init(o);
    std::copy(v.original_from_processed.begin(), v.original_from_processed.end(), o.original_from_processed);
    o.reference = encode_evidence<MantisEvidenceContentReferenceV1>(v.reference, w);
    return o;
}
data::ObservationKey decode(const MantisObservationKeyV1 &v, Reader &r) {
    r.prefix(v);
    data::ObservationKey o;
    o.run_id = decode_evidence<data::RunId>(v.run_id, r);
    o.producer_stream = decode_value<data::ProducerStreamId>(v.producer_stream, r);
    o.producer_generation = decode_value<data::GenerationId>(v.producer_generation, r);
    o.sequence = decode_value<data::ObservationSequence>(v.sequence, r);
    return o;
}
MantisObservationKeyV1 encode(const data::ObservationKey &v, Writer &w) {
    MantisObservationKeyV1 o{};
    init(o);
    o.run_id = encode_evidence<MantisEvidenceRunIdV1>(v.run_id, w);
    o.producer_stream = encode_value(v.producer_stream, w);
    o.producer_generation = encode_value(v.producer_generation, w);
    o.sequence = encode_value(v.sequence, w);
    return o;
}
data::LaserObservationContext decode(const MantisLaserObservationContextV1 &v, Reader &r) {
    r.prefix(v);
    data::LaserObservationContext o;
    o.source = decode_value<data::CameraFrameEvidence>(v.source, r);
    o.frameset = decode_evidence<data::FrameSetKey>(v.frameset, r);
    o.bundle = decode_evidence<data::BundleKey>(v.bundle, r);
    o.raw_input = decode_evidence<data::ContentReference>(v.raw_input, r);
    o.optical_frame = decode_value<spatial::CoordinateFrame>(v.optical_frame, r);
    o.preprocessing = decode_evidence<data::PreprocessingTransform>(v.preprocessing, r);
    o.requested_emitters = decode_array<data::ComponentId>(v.requested_emitters, v.requested_emitters_count, r, 64);
    o.emitter_evidence = decode_array<data::EmitterEvidence>(v.emitter_evidence, v.emitter_evidence_count, r, 64);
    o.emitter_patterns = decode_array<data::EmitterPatternIdentity>(v.emitter_patterns, v.emitter_patterns_count, r, 4096);
    o.correlation = decode_evidence<data::ProgramCorrelation>(v.correlation, r);
    o.acquisition_evidence = decode_evidence<data::EvidenceKey>(v.acquisition_evidence, r);
    o.triggers = decode_array<data::TriggerKey>(v.triggers, v.triggers_count, r, 4096);
    o.clock_mappings = decode_array<data::ClockMappingEvidence>(v.clock_mappings, v.clock_mappings_count, r, 64);
    o.producer = decode_value<data::ImplementationIdentity>(v.producer, r);
    o.parameters = decode_evidence<data::ContentReference>(v.parameters, r);
    o.exact_inputs = decode_array<data::ContentReference>(v.exact_inputs, v.exact_inputs_count, r, 4096);
    o.origin = decode_required<data::ObservationOrigin>(v.origin, r);
    o.producer_completed = decode_evidence<data::RuntimeTimestamp>(v.producer_completed, r);
    o.packet_quality_flags = decode_value<uint32_t>(v.packet_quality_flags, r);
    return o;
}
MantisLaserObservationContextV1 encode(const data::LaserObservationContext &v, Writer &w) {
    MantisLaserObservationContextV1 o{};
    init(o);
    o.source = encode_value(v.source, w);
    o.frameset = encode_evidence<MantisEvidenceFrameSetKeyV1>(v.frameset, w);
    o.bundle = encode_evidence<MantisEvidenceBundleKeyV1>(v.bundle, w);
    o.raw_input = encode_evidence<MantisEvidenceContentReferenceV1>(v.raw_input, w);
    o.optical_frame = encode_value(v.optical_frame, w);
    o.preprocessing = encode_evidence<MantisEvidencePreprocessingTransformV1>(v.preprocessing, w);
    o.requested_emitters_count = count(v.requested_emitters.size());
    o.requested_emitters = encode_array<const char *>(v.requested_emitters, w);
    o.emitter_evidence_count = count(v.emitter_evidence.size());
    o.emitter_evidence = encode_array<MantisEmitterEvidenceV1>(v.emitter_evidence, w);
    o.emitter_patterns_count = count(v.emitter_patterns.size());
    o.emitter_patterns = encode_array<MantisEmitterPatternIdentityV1>(v.emitter_patterns, w);
    o.correlation = encode_evidence<MantisEvidenceProgramCorrelationV1>(v.correlation, w);
    o.acquisition_evidence = encode_evidence<MantisEvidenceEvidenceKeyV1>(v.acquisition_evidence, w);
    o.triggers_count = count(v.triggers.size());
    o.triggers = encode_array<MantisTriggerKeyV1>(v.triggers, w);
    o.clock_mappings_count = count(v.clock_mappings.size());
    o.clock_mappings = encode_array<MantisClockMappingEvidenceV1>(v.clock_mappings, w);
    o.producer = encode_value(v.producer, w);
    o.parameters = encode_evidence<MantisEvidenceContentReferenceV1>(v.parameters, w);
    o.exact_inputs_count = count(v.exact_inputs.size());
    o.exact_inputs = encode_array<MantisContentReferenceV1>(v.exact_inputs, w);
    o.origin = encode_required<MantisEvidenceObservationOriginV1>(v.origin, w);
    o.producer_completed = encode_evidence<MantisEvidenceRuntimeTimestampV1>(v.producer_completed, w);
    o.packet_quality_flags = encode_value(v.packet_quality_flags, w);
    return o;
}
data::LaserObservation decode(const MantisLaserObservationV1 &v, Reader &r) {
    r.prefix(v);
    data::LaserObservation o;
    o.type = decode_value<schema::DataTypeId>(v.type, r);
    o.key = decode_value<data::ObservationKey>(v.key, r);
    o.context = decode_value<data::LaserObservationContext>(v.context, r);
    o.disposition = decode_required<data::ObservationDisposition>(v.disposition, r);
    o.sample_count = decode_value<uint64_t>(v.sample_count, r);
    o.emitter_dictionary = decode_array<data::ComponentId>(v.emitter_dictionary, v.emitter_dictionary_count, r, 4096);
    o.line_dictionary = decode_array<data::LineIdentity>(v.line_dictionary, v.line_dictionary_count, r, 4096);
    o.attributes = decode_array<data::Attribute>(v.attributes, v.attributes_count, r, 128);
    o.confidence_interpretation = decode_evidence<data::ImplementationIdentity>(v.confidence_interpretation, r);
    o.diagnostic = decode_value<std::string>(v.diagnostic, r);
    return o;
}
MantisLaserObservationV1 encode(const data::LaserObservation &v, Writer &w) {
    MantisLaserObservationV1 o{};
    init(o);
    o.type = encode_value(v.type, w);
    o.key = encode_value(v.key, w);
    o.context = encode_value(v.context, w);
    o.disposition = encode_required<MantisEvidenceObservationDispositionV1>(v.disposition, w);
    o.sample_count = encode_value(v.sample_count, w);
    o.emitter_dictionary_count = count(v.emitter_dictionary.size());
    o.emitter_dictionary = encode_array<const char *>(v.emitter_dictionary, w);
    o.line_dictionary_count = count(v.line_dictionary.size());
    o.line_dictionary = encode_array<MantisLineIdentityV1>(v.line_dictionary, w);
    o.attributes_count = count(v.attributes.size());
    o.attributes = encode_array<MantisAttributeV1>(v.attributes, w);
    o.confidence_interpretation = encode_evidence<MantisEvidenceImplementationIdentityV1>(v.confidence_interpretation, w);
    o.diagnostic = encode_value(v.diagnostic, w);
    return o;
}
time::DeviceTimestamp decode(const MantisDeviceTimestampV1 &v, Reader &r) {
    r.prefix(v);
    time::DeviceTimestamp o;
    o.nanoseconds = decode_value<int64_t>(v.nanoseconds, r);
    o.domain = decode_value<time::ClockDomain>(v.domain, r);
    return o;
}
MantisDeviceTimestampV1 encode(const time::DeviceTimestamp &v, Writer &w) {
    MantisDeviceTimestampV1 o{};
    init(o);
    o.nanoseconds = encode_value(v.nanoseconds, w);
    o.domain = encode_value(v.domain, w);
    return o;
}
data::Header decode(const MantisPacketHeaderV1 &v, Reader &r) {
    r.prefix(v);
    data::Header o;
    o.sequence = decode_value<time::SequenceNumber>(v.sequence, r);
    o.timestamp = decode_value<time::DeviceTimestamp>(v.timestamp, r);
    o.received = decode_value<time::MonotonicTimestamp>(v.received, r);
    o.sync = decode_value<time::SyncGroup>(v.sync, r);
    o.sync_quality = decode_value<time::SyncQuality>(v.sync_quality, r);
    o.calibration = decode_value<calibration::Reference>(v.calibration, r);
    o.frame = decode_value<spatial::CoordinateFrame>(v.frame, r);
    o.metadata = decode_value<data::Metadata>(v.metadata, r);
    return o;
}
MantisPacketHeaderV1 encode(const data::Header &v, Writer &w) {
    MantisPacketHeaderV1 o{};
    init(o);
    o.sequence = encode_value(v.sequence, w);
    o.timestamp = encode_value(v.timestamp, w);
    o.received = encode_value(v.received, w);
    o.sync = encode_value(v.sync, w);
    o.sync_quality = encode_value(v.sync_quality, w);
    o.calibration = encode_value(v.calibration, w);
    o.frame = encode_value(v.frame, w);
    o.metadata = encode_value(v.metadata, w);
    return o;
}
