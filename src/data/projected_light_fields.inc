// Explicit storage-v1 field and enum catalog. Order is normative; independent of ABI.
template <> struct Fields<Id> {
    template <class A, class V> static void apply(A &a, V &v) { a(v.value); }
};
template <> struct Fields<Hash> {
    template <class A, class V> static void apply(A &a, V &v) { a(v.algorithm, v.hex); }
};
template <> struct Fields<SemanticVersion> {
    template <class A, class V> static void apply(A &a, V &v) { a(v.major, v.minor, v.patch); }
};
template <> struct Fields<schema::DataTypeId> {
    template <class A, class V> static void apply(A &a, V &v) { a(v.name, v.version); }
};
template <> struct Fields<calibration::Reference> {
    template <class A, class V> static void apply(A &a, V &v) { a(v.id, v.schema_version, v.revision); }
};
template <> struct Fields<time::ClockDomain> {
    template <class A, class V> static void apply(A &a, V &v) { a(v.id, v.name); }
};
template <> struct Fields<time::MonotonicTimestamp> {
    template <class A, class V> static void apply(A &a, V &v) { a(v.nanoseconds); }
};
template <> struct Fields<time::SyncGroup> {
    template <class A, class V> static void apply(A &a, V &v) { a(v.id, v.trigger); }
};
template <> struct Fields<time::ClockMapping> {
    template <class A, class V> static void apply(A &a, V &v) {
        a(v.source, v.target, v.scale, v.offset_ns, v.uncertainty_ns);
    }
};
template <> struct Fields<StepInstance> {
    template <class A, class V> static void apply(A &a, V &v) {
        a(v.run_id, v.repetition_index, v.step_index);
    }
};
template <> struct Fields<StreamIdentity> {
    template <class A, class V> static void apply(A &a, V &v) { a(v.id, v.generation); }
};
template <> struct Fields<SourceFrameKey> {
    template <class A, class V> static void apply(A &a, V &v) { a(v.camera, v.stream, v.native_sequence); }
};
template <> struct Fields<FrameSetKey> {
    template <class A, class V> static void apply(A &a, V &v) { a(v.run_id, v.stream, v.sequence); }
};
template <> struct Fields<BundleKey> {
    template <class A, class V> static void apply(A &a, V &v) { a(v.run_id, v.sequence); }
};
template <> struct Fields<EvidenceKey> {
    template <class A, class V> static void apply(A &a, V &v) { a(v.run_id, v.ordinal); }
};
template <> struct Fields<TriggerKey> {
    template <class A, class V> static void apply(A &a, V &v) {
        a(v.run_id, v.source, v.controller_generation, v.sequence);
    }
};
template <> struct Fields<NativeTriggerIdentity> {
    template <class A, class V> static void apply(A &a, V &v) { a(v.controller, v.generation, v.value); }
};
template <> struct Fields<ContentReference> {
    template <class A, class V> static void apply(A &a, V &v) { a(v.id, v.type, v.hash, v.revision); }
};
template <> struct Fields<ProgramReference> {
    template <class A, class V> static void apply(A &a, V &v) { a(v.id, v.hash, v.content); }
};
template <> struct Fields<ExactCalibrationReference> {
    template <class A, class V> static void apply(A &a, V &v) { a(v.calibration, v.content); }
};
template <> struct Fields<ClockIdentity> {
    template <class A, class V> static void apply(A &a, V &v) { a(v.domain, v.generation); }
};
template <> struct Fields<SemanticTimestamp> {
    template <class A, class V> static void apply(A &a, V &v) { a(v.nanoseconds, v.clock); }
};
template <> struct Fields<RuntimeTimestamp> {
    template <class A, class V> static void apply(A &a, V &v) { a(v.time, v.clock); }
};
template <> struct Fields<TimeInterval> {
    template <class A, class V> static void apply(A &a, V &v) { a(v.start, v.end); }
};
template <> struct Fields<ClockMappingEvidence> {
    template <class A, class V> static void apply(A &a, V &v) {
        a(v.mapping, v.source_generation, v.target_generation, v.reference);
    }
};
template <> struct Fields<EvidenceSource> {
    template <class A, class V> static void apply(A &a, V &v) { a(v.source, v.method, v.reference); }
};
template <> struct Fields<Acknowledgement> {
    template <class A, class V> static void apply(A &a, V &v) {
        a(v.request, v.stage, v.result, v.evidence, v.time, v.scope);
    }
};
template <> struct Fields<EmitterCommand> {
    template <class A, class V> static void apply(A &a, V &v) {
        a(v.request, v.target, v.state, v.dispatched);
    }
};
template <> struct Fields<StateObservation> {
    template <class A, class V> static void apply(A &a, V &v) {
        a(v.state, v.scope, v.evidence, v.time, v.coverage);
    }
};
template <> struct Fields<ExposureEvidence> {
    template <class A, class V> static void apply(A &a, V &v) {
        a(v.requested_duration, v.startup_readback_duration, v.integration_duration, v.interval, v.evidence,
          v.uncertainty_ns);
    }
};
template <> struct Fields<ExposureAssociation> {
    template <class A, class V> static void apply(A &a, V &v) {
        a(v.frame, v.trigger, v.method, v.evidence, v.native_trigger);
    }
};
template <> struct Fields<SyncEvidence> {
    template <class A, class V> static void apply(A &a, V &v) {
        a(v.group, v.quality, v.hardware_association);
    }
};
template <> struct Fields<CameraFrameEvidence> {
    template <class A, class V> static void apply(A &a, V &v) {
        a(v.frame, v.camera_role, v.width, v.height, v.source_timestamp, v.host_received, v.timestamp_meaning,
          v.exposure, v.sync, v.camera_calibration, v.rig_calibration, v.original_calibration);
    }
};
template <> struct Fields<ExposureEffectiveState> {
    template <class A, class V> static void apply(A &a, V &v) {
        a(v.frame, v.state, v.scope, v.evidence, v.coverage);
    }
};
template <> struct Fields<CameraEffectiveState> {
    template <class A, class V> static void apply(A &a, V &v) { a(v.frame, v.state); }
};
template <> struct Fields<EmitterEvidence> {
    template <class A, class V> static void apply(A &a, V &v) {
        a(v.emitter, v.commanded, v.acknowledged, v.observed, v.exposure_effective);
    }
};
template <> struct Fields<CameraParticipant> {
    template <class A, class V> static void apply(A &a, V &v) { a(v.component, v.stream, v.role); }
};
template <> struct Fields<Participants> {
    template <class A, class V> static void apply(A &a, V &v) { a(v.cameras, v.emitters, v.controllers); }
};
template <> struct Fields<TriggerIntent> {
    template <class A, class V> static void apply(A &a, V &v) { a(v.controller, v.request, v.endpoints); }
};
template <> struct Fields<CaptureIntent> {
    template <class A, class V> static void apply(A &a, V &v) { a(v.mode, v.cameras, v.trigger); }
};
template <> struct Fields<EmitterIntent> {
    template <class A, class V> static void apply(A &a, V &v) { a(v.emitter, v.state); }
};
template <> struct Fields<AcquisitionStep> {
    template <class A, class V> static void apply(A &a, V &v) {
        a(v.index, v.label, v.emitters, v.capture, v.evidence_requirement, v.required_scope, v.settle,
          v.max_duration);
    }
};
template <> struct Fields<RunBounds> {
    template <class A, class V> static void apply(A &a, V &v) {
        a(v.max_duration, v.max_on_duration, v.max_step_instances, v.max_commands, v.max_events, v.max_bytes,
          v.max_in_flight_captures);
    }
};
template <> struct Fields<AcquisitionProgram> {
    template <class A, class V> static void apply(A &a, V &v) {
        a(v.type, v.identity, v.participants, v.steps, v.repetitions, v.bounds);
    }
};
template <> struct Fields<TriggerEvent> {
    template <class A, class V> static void apply(A &a, V &v) {
        a(v.type, v.key, v.step, v.request, v.native_trigger, v.kind, v.acknowledgement, v.evidence,
          v.device_time, v.host_received, v.host_dispatched, v.uncertainty_ns, v.clock_mapping,
          v.intended_endpoints, v.actual_endpoints, v.requested_exposure, v.exposure_association);
    }
};
template <> struct Fields<ImplementationIdentity> {
    template <class A, class V> static void apply(A &a, V &v) {
        a(v.implementation, v.version, v.build, v.configuration);
    }
};
template <> struct Fields<LossAccounting> {
    template <class A, class V> static void apply(A &a, V &v) {
        a(v.kind, v.source, v.count, v.reason, v.frame, v.request);
    }
};
template <> struct Fields<UnresolvedRequest> {
    template <class A, class V> static void apply(A &a, V &v) { a(v.request, v.target, v.reason); }
};
template <> struct Fields<AcquisitionEvidence> {
    template <class A, class V> static void apply(A &a, V &v) {
        a(v.type, v.key, v.program, v.step, v.causal_predecessors, v.participants, v.implementations,
          v.emitters, v.frameset, v.frames, v.triggers, v.clock_mappings, v.rig_calibration, v.disposition,
          v.reason, v.unresolved_requests, v.losses, v.diagnostic);
    }
};
template <> struct Fields<RecordedRunConfig> {
    template <class A, class V> static void apply(A &a, V &v) {
        a(v.queue_capacity, v.operation_timeout_ms, v.abort_timeout_ms, v.cleanup_timeout_ms,
          v.publication_timeout_ms, v.max_correlation_entries);
    }
};
template <> struct Fields<ProjectedCaptureHeader> {
    template <class A, class V> static void apply(A &a, V &v) { a(v.program, v.run, v.generation, v.config); }
};
template <> struct Tags<EvidenceMethod> {
    static constexpr std::array values{
        EvidenceMethod::software_dispatch, EvidenceMethod::controller_report,
        EvidenceMethod::register_readback, EvidenceMethod::electrical_readback,
        EvidenceMethod::optical_sensor,    EvidenceMethod::validated_executor,
        EvidenceMethod::camera_metadata,   EvidenceMethod::software_association,
        EvidenceMethod::imported};
};
template <> struct Tags<EvidenceScope> {
    static constexpr std::array values{EvidenceScope::controller_register, EvidenceScope::electrical_enable,
                                       EvidenceScope::optical_emission};
};
template <> struct Tags<EmitterState> {
    static constexpr std::array values{EmitterState::off, EmitterState::on};
};
template <> struct Tags<AcknowledgementStage> {
    static constexpr std::array values{AcknowledgementStage::acceptance, AcknowledgementStage::completion};
};
template <> struct Tags<AcknowledgementResult> {
    static constexpr std::array values{AcknowledgementResult::success, AcknowledgementResult::rejected,
                                       AcknowledgementResult::failed};
};
template <> struct Tags<TimestampMeaning> {
    static constexpr std::array values{TimestampMeaning::exposure_start, TimestampMeaning::exposure_end,
                                       TimestampMeaning::driver_delivery, TimestampMeaning::device_event};
};
template <> struct Tags<AssociationMethod> {
    static constexpr std::array values{
        AssociationMethod::native_trigger, AssociationMethod::validated_executor,
        AssociationMethod::software_correspondence, AssociationMethod::imported};
};
template <> struct Tags<time::SyncQuality> {
    static constexpr std::array values{time::SyncQuality::unknown, time::SyncQuality::software,
                                       time::SyncQuality::hardware};
};
template <> struct Tags<CaptureMode> {
    static constexpr std::array values{CaptureMode::none, CaptureMode::free_running,
                                       CaptureMode::hardware_trigger};
};
template <> struct Tags<EvidenceRequirement> {
    static constexpr std::array values{EvidenceRequirement::commanded_only,
                                       EvidenceRequirement::controller_acknowledged,
                                       EvidenceRequirement::exposure_effective};
};
template <> struct Tags<TriggerEvent::Kind> {
    static constexpr std::array values{TriggerEvent::Kind::requested,
                                       TriggerEvent::Kind::acknowledged_accepted,
                                       TriggerEvent::Kind::acknowledged_completed,
                                       TriggerEvent::Kind::rejected,
                                       TriggerEvent::Kind::observed,
                                       TriggerEvent::Kind::timed_out,
                                       TriggerEvent::Kind::cancelled};
};
template <> struct Tags<AcquisitionDisposition> {
    static constexpr std::array values{
        AcquisitionDisposition::startup,      AcquisitionDisposition::captured,
        AcquisitionDisposition::control_only, AcquisitionDisposition::completed,
        AcquisitionDisposition::failed,       AcquisitionDisposition::stopped,
        AcquisitionDisposition::cancelled};
};
template <> struct Tags<AcquisitionReason> {
    static constexpr std::array values{AcquisitionReason::none,
                                       AcquisitionReason::timeout,
                                       AcquisitionReason::rejected,
                                       AcquisitionReason::device_failure,
                                       AcquisitionReason::transport_failure,
                                       AcquisitionReason::evidence_missing,
                                       AcquisitionReason::contradictory_evidence,
                                       AcquisitionReason::resource_limit,
                                       AcquisitionReason::user_stop,
                                       AcquisitionReason::user_cancel,
                                       AcquisitionReason::cleanup_failure};
};
template <> struct Tags<LossKind> {
    static constexpr std::array values{LossKind::command,       LossKind::trigger, LossKind::frame,
                                       LossKind::exposure,      LossKind::bundle,  LossKind::record,
                                       LossKind::excluded_frame};
};
template <> struct Fields<Error> {
    template <class A, class V> static void apply(A &a, V &v) { a(v.code, v.message, v.component); }
};
template <> struct Fields<RecordedExecutorError> {
    template <class A, class V> static void apply(A &a, V &v) { a(v.category, v.code); }
};
template <> struct Fields<RecordedAbortOutcome> {
    template <class A, class V> static void apply(A &a, V &v) {
        a(v.run, v.fenced_generation, v.inhibited, v.stale_work_fenced, v.off_requested, v.emitters, v.error);
    }
};
template <> struct Fields<ProjectedRunOutcome> {
    template <class A, class V> static void apply(A &a, V &v) {
        a(v.run, v.generation, v.disposition, v.reason, v.initiating_error, v.abort_error, v.stop_error,
          v.close_error, v.abort_outcome, v.diagnostic);
    }
};
template <> struct Fields<ProjectedCaptureOutcome> {
    template <class A, class V> static void apply(A &a, V &v) { a(v.bundle_count, v.outcome); }
};
template <> struct Tags<RecordedRunDisposition> {
    static constexpr std::array values{RecordedRunDisposition::completed, RecordedRunDisposition::cancelled,
                                       RecordedRunDisposition::failed};
};
template <> struct Tags<Status> {
    static constexpr std::array values{Status::ok,           Status::invalid_argument, Status::not_found,
                                       Status::incompatible, Status::cancelled,        Status::io,
                                       Status::busy,         Status::plugin_failed,    Status::unsupported,
                                       Status::corrupt};
};
