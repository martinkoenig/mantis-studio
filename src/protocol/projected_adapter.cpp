#include "projected_adapter.hpp"
namespace mantis::protocol {
namespace {
namespace w = wire::v1;
namespace d = data;
w::ProjectedPresence encode_Presence(d::Presence v) {
    switch (v) {
    case d::Presence::unknown:
        return w::PROJECTED_UNKNOWN;
    case d::Presence::unavailable:
        return w::PROJECTED_UNAVAILABLE;
    case d::Presence::established:
        return w::PROJECTED_ESTABLISHED;
    }
    fail(Status::invalid_argument, "Invalid projected enum");
}
d::Presence decode_Presence(w::ProjectedPresence v) {
    switch (v) {
    case w::PROJECTED_UNKNOWN:
        return d::Presence::unknown;
    case w::PROJECTED_UNAVAILABLE:
        return d::Presence::unavailable;
    case w::PROJECTED_ESTABLISHED:
        return d::Presence::established;
    default:
        fail(Status::invalid_argument, "Invalid projected enum tag");
    }
}
w::ProjectedEmitterState encode_EmitterState(d::EmitterState v) {
    switch (v) {
    case d::EmitterState::off:
        return w::PROJECTED_OFF;
    case d::EmitterState::on:
        return w::PROJECTED_ON;
    }
    fail(Status::invalid_argument, "Invalid projected enum");
}
d::EmitterState decode_EmitterState(w::ProjectedEmitterState v) {
    switch (v) {
    case w::PROJECTED_OFF:
        return d::EmitterState::off;
    case w::PROJECTED_ON:
        return d::EmitterState::on;
    default:
        fail(Status::invalid_argument, "Invalid projected enum tag");
    }
}
w::ProjectedCaptureMode encode_CaptureMode(d::CaptureMode v) {
    switch (v) {
    case d::CaptureMode::none:
        return w::PROJECTED_CAPTURE_NONE;
    case d::CaptureMode::free_running:
        return w::PROJECTED_FREE_RUNNING;
    case d::CaptureMode::hardware_trigger:
        return w::PROJECTED_HARDWARE_TRIGGER;
    }
    fail(Status::invalid_argument, "Invalid projected enum");
}
d::CaptureMode decode_CaptureMode(w::ProjectedCaptureMode v) {
    switch (v) {
    case w::PROJECTED_CAPTURE_NONE:
        return d::CaptureMode::none;
    case w::PROJECTED_FREE_RUNNING:
        return d::CaptureMode::free_running;
    case w::PROJECTED_HARDWARE_TRIGGER:
        return d::CaptureMode::hardware_trigger;
    default:
        fail(Status::invalid_argument, "Invalid projected enum tag");
    }
}
w::ProjectedEvidenceRequirement encode_EvidenceRequirement(d::EvidenceRequirement v) {
    switch (v) {
    case d::EvidenceRequirement::commanded_only:
        return w::PROJECTED_COMMANDED_ONLY;
    case d::EvidenceRequirement::controller_acknowledged:
        return w::PROJECTED_CONTROLLER_ACKNOWLEDGED;
    case d::EvidenceRequirement::exposure_effective:
        return w::PROJECTED_EXPOSURE_EFFECTIVE;
    }
    fail(Status::invalid_argument, "Invalid projected enum");
}
d::EvidenceRequirement decode_EvidenceRequirement(w::ProjectedEvidenceRequirement v) {
    switch (v) {
    case w::PROJECTED_COMMANDED_ONLY:
        return d::EvidenceRequirement::commanded_only;
    case w::PROJECTED_CONTROLLER_ACKNOWLEDGED:
        return d::EvidenceRequirement::controller_acknowledged;
    case w::PROJECTED_EXPOSURE_EFFECTIVE:
        return d::EvidenceRequirement::exposure_effective;
    default:
        fail(Status::invalid_argument, "Invalid projected enum tag");
    }
}
w::ProjectedEvidenceScope encode_EvidenceScope(d::EvidenceScope v) {
    switch (v) {
    case d::EvidenceScope::controller_register:
        return w::PROJECTED_CONTROLLER_REGISTER;
    case d::EvidenceScope::electrical_enable:
        return w::PROJECTED_ELECTRICAL_ENABLE;
    case d::EvidenceScope::optical_emission:
        return w::PROJECTED_OPTICAL_EMISSION;
    }
    fail(Status::invalid_argument, "Invalid projected enum");
}
d::EvidenceScope decode_EvidenceScope(w::ProjectedEvidenceScope v) {
    switch (v) {
    case w::PROJECTED_CONTROLLER_REGISTER:
        return d::EvidenceScope::controller_register;
    case w::PROJECTED_ELECTRICAL_ENABLE:
        return d::EvidenceScope::electrical_enable;
    case w::PROJECTED_OPTICAL_EMISSION:
        return d::EvidenceScope::optical_emission;
    default:
        fail(Status::invalid_argument, "Invalid projected enum tag");
    }
}
w::ProjectedEvidenceMethod encode_EvidenceMethod(d::EvidenceMethod v) {
    switch (v) {
    case d::EvidenceMethod::software_dispatch:
        return w::PROJECTED_SOFTWARE_DISPATCH;
    case d::EvidenceMethod::controller_report:
        return w::PROJECTED_CONTROLLER_REPORT;
    case d::EvidenceMethod::register_readback:
        return w::PROJECTED_REGISTER_READBACK;
    case d::EvidenceMethod::electrical_readback:
        return w::PROJECTED_ELECTRICAL_READBACK;
    case d::EvidenceMethod::optical_sensor:
        return w::PROJECTED_OPTICAL_SENSOR;
    case d::EvidenceMethod::validated_executor:
        return w::PROJECTED_VALIDATED_EXECUTOR;
    case d::EvidenceMethod::camera_metadata:
        return w::PROJECTED_CAMERA_METADATA;
    case d::EvidenceMethod::software_association:
        return w::PROJECTED_SOFTWARE_ASSOCIATION;
    case d::EvidenceMethod::imported:
        return w::PROJECTED_IMPORTED;
    }
    fail(Status::invalid_argument, "Invalid projected enum");
}
w::ProjectedRunState encode_RunState(device::ProjectedState v) {
    switch (v) {
    case device::ProjectedState::validating:
        return w::PROJECTED_VALIDATING;
    case device::ProjectedState::ready:
        return w::PROJECTED_READY;
    case device::ProjectedState::running:
        return w::PROJECTED_RUNNING;
    case device::ProjectedState::stopping:
        return w::PROJECTED_STOPPING;
    case device::ProjectedState::completed:
        return w::PROJECTED_COMPLETED;
    case device::ProjectedState::cancelled:
        return w::PROJECTED_CANCELLED;
    case device::ProjectedState::failed:
        return w::PROJECTED_FAILED;
    }
    fail(Status::invalid_argument, "Invalid projected enum");
}
w::ProjectedReason encode_Reason(d::AcquisitionReason v) {
    switch (v) {
    case d::AcquisitionReason::none:
        return w::PROJECTED_REASON_NONE;
    case d::AcquisitionReason::timeout:
        return w::PROJECTED_TIMEOUT;
    case d::AcquisitionReason::rejected:
        return w::PROJECTED_REJECTED;
    case d::AcquisitionReason::device_failure:
        return w::PROJECTED_DEVICE_FAILURE;
    case d::AcquisitionReason::transport_failure:
        return w::PROJECTED_TRANSPORT_FAILURE;
    case d::AcquisitionReason::evidence_missing:
        return w::PROJECTED_EVIDENCE_MISSING;
    case d::AcquisitionReason::contradictory_evidence:
        return w::PROJECTED_CONTRADICTORY_EVIDENCE;
    case d::AcquisitionReason::resource_limit:
        return w::PROJECTED_RESOURCE_LIMIT;
    case d::AcquisitionReason::user_stop:
        return w::PROJECTED_USER_STOP;
    case d::AcquisitionReason::user_cancel:
        return w::PROJECTED_USER_CANCEL;
    case d::AcquisitionReason::cleanup_failure:
        return w::PROJECTED_CLEANUP_FAILURE;
    }
    fail(Status::invalid_argument, "Invalid projected enum");
}
w::ProjectedParticipantKind encode_ParticipantKind(device::ParticipantKind v) {
    switch (v) {
    case device::ParticipantKind::parent:
        return w::PROJECTED_PARENT;
    case device::ParticipantKind::image:
        return w::PROJECTED_IMAGE;
    case device::ParticipantKind::emitter:
        return w::PROJECTED_EMITTER;
    case device::ParticipantKind::controller:
        return w::PROJECTED_CONTROLLER;
    }
    fail(Status::invalid_argument, "Invalid projected enum");
}
services::ProjectedStopMode decode_StopMode(w::ProjectedStopMode v) {
    switch (v) {
    case w::PROJECTED_NORMAL_STOP:
        return services::ProjectedStopMode::normal_stop;
    case w::PROJECTED_CANCEL:
        return services::ProjectedStopMode::cancel;
    default:
        fail(Status::invalid_argument, "Invalid projected enum tag");
    }
}
w::ProjectedStorageState encode_StorageState(artifact::ArtifactState v) {
    switch (v) {
    case artifact::ArtifactState::open:
        return w::PROJECTED_STORAGE_OPEN;
    case artifact::ArtifactState::finalizing:
        return w::PROJECTED_STORAGE_FINALIZING;
    case artifact::ArtifactState::finalized:
        return w::PROJECTED_STORAGE_FINALIZED;
    case artifact::ArtifactState::recoverable:
        return w::PROJECTED_STORAGE_RECOVERABLE;
    }
    fail(Status::invalid_argument, "Invalid projected enum");
}
w::ProjectedAcknowledgementStage encode_AcknowledgementStage(d::AcknowledgementStage v) {
    switch (v) {
    case d::AcknowledgementStage::acceptance:
        return w::PROJECTED_ACCEPTANCE;
    case d::AcknowledgementStage::completion:
        return w::PROJECTED_ACK_COMPLETION;
    }
    fail(Status::invalid_argument, "Invalid projected enum");
}
w::ProjectedAcknowledgementResult encode_AcknowledgementResult(d::AcknowledgementResult v) {
    switch (v) {
    case d::AcknowledgementResult::success:
        return w::PROJECTED_ACK_SUCCESS;
    case d::AcknowledgementResult::rejected:
        return w::PROJECTED_ACK_REJECTED;
    case d::AcknowledgementResult::failed:
        return w::PROJECTED_ACK_FAILED;
    }
    fail(Status::invalid_argument, "Invalid projected enum");
}
w::ProjectedTriggerMode encode_TriggerMode(d::CaptureMode v) {
    switch (v) {
    case d::CaptureMode::none:
        return w::PROJECTED_TRIGGER_NONE;
    case d::CaptureMode::free_running:
        return w::PROJECTED_TRIGGER_FREE_RUNNING;
    case d::CaptureMode::hardware_trigger:
        return w::PROJECTED_HARDWARE_TRIGGER_MODE;
    }
    fail(Status::invalid_argument, "Invalid projected enum");
}
void check_presence(d::Presence p, bool value) {
    if ((p == d::Presence::established) != value)
        fail(Status::invalid_argument, "Presence/value mismatch");
}
template <class T, class W, class F> d::Evidence<T> evidence(const W &w, F convert) {
    auto p = decode_Presence(w.presence());
    check_presence(p, w.has_value());
    if (p == d::Presence::unknown)
        return d::Unknown{};
    if (p == d::Presence::unavailable)
        return d::Unavailable{};
    return convert(w.value());
}
template <class T, class W, class F> void evidence(W *w, const d::Evidence<T> &v, F convert) {
    w->set_presence(encode_Presence(v.presence()));
    if (auto p = v.get())
        convert(w, *p);
}
d::Evidence<Hash> hash(const w::ProjectedHashEvidence &v) {
    return evidence<Hash>(v, [](const auto &h) { return Hash{h.algorithm(), h.hex()}; });
}
void hash(w::ProjectedHashEvidence *out, const d::Evidence<Hash> &h) {
    evidence(out, h, [](auto *o, const auto &v) {
        o->mutable_value()->set_algorithm(v.algorithm);
        o->mutable_value()->set_hex(v.hex);
    });
}
d::ProgramReference reference(const w::ProjectedProgramReference &v) {
    return {{{v.id()}}, hash(v.hash()), evidence<d::ContentReference>(v.content(), [](const auto &c) {
                return d::ContentReference{
                    {c.id()}, {c.type(), c.schema_version()}, hash(c.hash()), c.revision()};
            })};
}
void reference(w::ProjectedProgramReference *out, const d::ProgramReference &v) {
    out->set_id(v.id.id.value);
    hash(out->mutable_hash(), v.hash);
    evidence(out->mutable_content(), v.content, [](auto *o, const auto &c) {
        auto *content = o->mutable_value();
        content->set_id(c.id.value);
        content->set_type(c.type.name);
        content->set_schema_version(c.type.version);
        hash(content->mutable_hash(), c.hash);
        content->set_revision(c.revision);
    });
}
d::RunBounds bounds(const w::ProjectedRunBounds &v) {
    return {d::Duration{v.max_duration_ns()},
            d::Duration{v.max_on_duration_ns()},
            v.max_step_instances(),
            v.max_commands(),
            v.max_events(),
            v.max_bytes(),
            v.max_in_flight_captures()};
}
void bounds(w::ProjectedRunBounds *o, const d::RunBounds &v) {
    o->set_max_duration_ns(v.max_duration.count());
    o->set_max_on_duration_ns(v.max_on_duration.count());
    o->set_max_step_instances(v.max_step_instances);
    o->set_max_commands(v.max_commands);
    o->set_max_events(v.max_events);
    o->set_max_bytes(v.max_bytes);
    o->set_max_in_flight_captures(v.max_in_flight_captures);
}
void boolean(w::ProjectedBoolEvidence *o, const d::Evidence<bool> &v) {
    evidence(o, v, [](auto *out, bool b) { out->set_value(b); });
}
template <class T> void string(w::ProjectedStringEvidence *o, const d::Evidence<T> &v) {
    evidence(o, v, [](auto *out, const auto &id) { out->set_value(id.id.value); });
}
void limits(w::ProjectedLimits *o, const device::ProjectedLimits &v) {
    o->set_max_components(v.max_components);
    o->set_max_steps(v.max_steps);
    o->set_max_bundle_members(v.max_bundle_members);
    o->set_max_cameras(v.max_cameras);
    bounds(o->mutable_run(), v.bounds);
    o->set_max_step_duration_ns(v.max_step_duration.count());
    o->set_max_pending_bundles(v.max_pending_bundles);
    o->set_max_call_timeout_ms(v.max_call_timeout_ms);
    boolean(o->mutable_watchdog(), v.watchdog);
    boolean(o->mutable_interlock(), v.interlock);
    boolean(o->mutable_fail_off(), v.fail_off);
}
void error(w::Error *o, const Error &v) {
    o->set_code(static_cast<uint32_t>(v.code));
    o->set_message(v.message);
    o->set_component(v.component);
}
void contract(w::ProjectedContractError *o, const device::ContractError &v) {
    o->set_category(v.category);
    o->set_code(v.code);
}
void validation(w::ProjectedValidation *o, const device::ProjectedValidation &v) {
    o->set_accepted(v.accepted);
    limits(o->mutable_limits(), v.limits);
    if (v.host_error)
        error(o->mutable_host_error(), *v.host_error);
    if (v.executor_error)
        error(o->mutable_executor_error(), *v.executor_error);
    if (v.close_error)
        error(o->mutable_close_error(), *v.close_error);
    if (v.executor_validation) {
        auto *e = o->mutable_executor_validation();
        e->set_accepted(v.executor_validation->accepted);
        contract(e->mutable_error(), v.executor_validation->error);
        e->set_diagnostic(v.executor_validation->diagnostic);
    }
}
services::ProjectedCaptureRequest request(const w::ProjectedCaptureRequest &v) {
    services::ProjectedCaptureRequest o;
    o.plugin_id = v.plugin_id();
    o.parent = {v.parent_id()};
    switch (v.program().source_case()) {
    case w::ProjectedProgramSource::kInlineProgram:
        o.program = read_projected_program(v.program().inline_program());
        break;
    case w::ProjectedProgramSource::kRawCaptureArtifactId:
        o.program = Id{v.program().raw_capture_artifact_id()};
        break;
    default:
        fail(Status::invalid_argument, "Explicit projected program source required");
    }
    const auto &c = v.config();
    if (c.has_queue_capacity())
        o.config.queue_capacity = c.queue_capacity();
    if (c.has_operation_timeout_ms())
        o.config.operation_timeout_ms = c.operation_timeout_ms();
    if (c.has_abort_timeout_ms())
        o.config.abort_timeout_ms = c.abort_timeout_ms();
    if (c.has_cleanup_timeout_ms())
        o.config.cleanup_timeout_ms = c.cleanup_timeout_ms();
    if (c.has_publication_timeout_ms())
        o.config.publication_timeout_ms = c.publication_timeout_ms();
    if (c.has_max_correlation_entries())
        o.config.max_correlation_entries = c.max_correlation_entries();
    return o;
}
void device(w::ProjectedDevice *o, const services::ProjectedDeviceInfo &v) {
    o->set_plugin_id(v.plugin_id);
    o->set_parent_id(v.graph.parent.value);
    limits(o->mutable_limits(), v.graph.limits);
    string(o->mutable_frameset_stream(), v.graph.frameset_stream);
    for (const auto &c : v.graph.components) {
        auto *p = o->add_components();
        p->set_id(c.descriptor.id.value);
        p->set_parent_id(c.descriptor.parent.value);
        p->set_name(c.descriptor.name);
        p->set_role(c.role);
        p->set_kind(encode_ParticipantKind(c.kind));
        for (const auto &x : c.descriptor.capabilities)
            p->add_capabilities(x);
        for (const auto &x : c.controls)
            p->add_controls(x.value);
        for (const auto &x : c.participants)
            p->add_participants(x.value);
        for (const auto &x : c.trigger_endpoints)
            p->add_trigger_endpoints(x.value);
        for (auto x : c.emitter_states)
            p->add_emitter_states(encode_EmitterState(x));
        for (auto x : c.capture_modes)
            p->add_capture_modes(encode_CaptureMode(x));
        for (auto x : c.trigger_modes)
            p->add_trigger_modes(encode_TriggerMode(x));
        for (auto x : c.evidence_methods)
            p->add_evidence_methods(encode_EvidenceMethod(x));
        for (auto x : c.evidence_scopes)
            p->add_evidence_scopes(encode_EvidenceScope(x));
        string(p->mutable_pattern(), c.pattern);
        evidence(p->mutable_pattern_revision(), c.pattern_revision,
                 [](auto *out, uint64_t value) { out->set_value(value); });
        if (c.image_source) {
            auto *i = p->mutable_image_source();
            i->set_stream_id(c.image_source->stream.id.value);
            i->set_physical_identity(c.image_source->physical_identity);
            i->set_role(c.role);
            i->set_width(c.image_source->width);
            i->set_height(c.image_source->height);
        }
    }
}
void emitter(w::ProjectedEmitterSummary *o, const d::EmitterEvidence &e) {
    o->set_emitter_id(e.emitter.id.value);
    o->set_commanded_presence(encode_Presence(e.commanded.presence()));
    if (auto c = e.commanded.get())
        o->set_commanded_state(encode_EmitterState(c->state));
    o->set_acknowledgement_presence(encode_Presence(e.acknowledged.presence()));
    if (auto a = e.acknowledged.get()) {
        o->set_acknowledgement_stage(encode_AcknowledgementStage(*a->stage));
        o->set_acknowledgement_result(encode_AcknowledgementResult(*a->result));
    }
    o->set_observed_presence(encode_Presence(e.observed.presence()));
    if (auto v = e.observed.get()) {
        o->set_observed_state(encode_EmitterState(*v->state));
        o->set_observed_scope(encode_EvidenceScope(v->scope));
    }
    for (const auto &v : e.exposure_effective) {
        switch (v.state.presence()) {
        case d::Presence::established:
            o->set_effective_established(o->effective_established() + 1);
            break;
        case d::Presence::unknown:
            o->set_effective_unknown(o->effective_unknown() + 1);
            break;
        case d::Presence::unavailable:
            o->set_effective_unavailable(o->effective_unavailable() + 1);
            break;
        }
    }
}
void capture(w::ProjectedCapture *o, const services::ProjectedCaptureInfo &v) {
    o->set_id(v.id.value);
    o->set_plugin_id(v.plugin_id);
    o->set_parent_id(v.parent.value);
    o->set_run_id(v.run.identity.run.id.value);
    o->set_generation_id(v.run.identity.generation.id.value);
    o->set_raw_artifact_id(v.raw_artifact.value);
    reference(o->mutable_program(), v.program);
    o->set_state(encode_RunState(v.run.state));
    o->set_cleanup_resolved(v.run.cleanup_resolved);
    o->set_active(v.active);
    o->set_storage_state(encode_StorageState(v.storage_state));
    o->set_committed_bundles(v.committed);
    o->set_raw_capture_bytes(v.bytes);
    o->set_finalization_job_id(v.finalization_job.value);
    if (v.recording_error)
        error(o->mutable_recording_error(), *v.recording_error);
    if (v.last_evidence_step) {
        auto *s = o->mutable_last_evidence_step();
        s->set_run_id(v.last_evidence_step->run_id.id.value);
        s->set_repetition_index(v.last_evidence_step->repetition_index);
        s->set_step_index(v.last_evidence_step->step_index);
    }
    if (v.latest_bundle_sequence)
        o->set_latest_bundle_sequence(v.latest_bundle_sequence->value);
    auto *q = o->mutable_queue();
    q->set_produced(v.run.queue.produced);
    q->set_consumed(v.run.queue.consumed);
    q->set_occupancy(v.run.queue.occupancy);
    q->set_capacity(v.run.queue.capacity);
    q->set_high_water(v.run.queue.high_water);
    q->set_saturation_failures(v.run.queue.saturation_failures);
    const auto &t = v.run.terminal;
    o->set_reason(encode_Reason(t.reason));
    if (t.initiating_error)
        error(o->mutable_initiating_error(), *t.initiating_error);
    if (t.abort_error)
        error(o->mutable_abort_error(), *t.abort_error);
    if (t.stop_error)
        error(o->mutable_stop_error(), *t.stop_error);
    if (t.close_error)
        error(o->mutable_close_error(), *t.close_error);
    if (t.abort_outcome) {
        const auto &a = *t.abort_outcome;
        auto *b = o->mutable_abort_outcome();
        string(b->mutable_run(), a.run);
        string(b->mutable_fenced_generation(), a.fenced_generation);
        boolean(b->mutable_inhibited(), a.inhibited);
        boolean(b->mutable_stale_work_fenced(), a.stale_work_fenced);
        boolean(b->mutable_off_requested(), a.off_requested);
        contract(b->mutable_executor_error(), a.error);
        for (const auto &e : a.emitters)
            emitter(b->add_emitters(), e);
    }
    auto *e = o->mutable_evidence();
    e->set_bundles(v.evidence.bundles);
    e->set_trigger_events(v.evidence.trigger_events);
    e->set_evidence_only(v.evidence.evidence_only);
    e->set_captured(v.evidence.captured);
    e->set_unresolved_request_records(v.evidence.unresolved_request_records);
    e->set_loss_records(v.evidence.loss_records);
    e->set_commanded_established(v.evidence.commanded_established);
    e->set_commanded_unknown(v.evidence.commanded_unknown);
    e->set_commanded_unavailable(v.evidence.commanded_unavailable);
    e->set_acknowledgement_success(v.evidence.acknowledgement_success);
    e->set_acknowledgement_rejected(v.evidence.acknowledgement_rejected);
    e->set_acknowledgement_failed(v.evidence.acknowledgement_failed);
    e->set_acknowledgement_unknown(v.evidence.acknowledgement_unknown);
    e->set_acknowledgement_unavailable(v.evidence.acknowledgement_unavailable);
    e->set_effective_established(v.evidence.effective_established);
    e->set_effective_unknown(v.evidence.effective_unknown);
    e->set_effective_unavailable(v.evidence.effective_unavailable);
    e->set_late_evidence(v.run.late_evidence);
}
} // namespace
data::AcquisitionProgram read_projected_program(const w::ProjectedAcquisitionProgram &v) {
    if (v.ByteSizeLong() > services::projected_inline_bytes)
        fail(Status::invalid_argument, "Inline program exceeds 512 KiB");
    d::AcquisitionProgram o;
    o.type = {v.type(), v.schema_version()};
    o.identity = reference(v.identity());
    for (const auto &c : v.participants().cameras())
        o.participants.cameras.push_back({{{c.component()}}, {{c.stream()}}, c.role()});
    for (const auto &c : v.participants().emitters())
        o.participants.emitters.push_back({{c}});
    for (const auto &c : v.participants().controllers())
        o.participants.controllers.push_back({{c}});
    for (const auto &s : v.steps()) {
        d::AcquisitionStep step;
        step.index = s.index();
        step.label = s.label();
        for (const auto &e : s.emitters())
            step.emitters.push_back({{{e.emitter()}}, decode_EmitterState(e.state())});
        if (s.capture().has_mode())
            step.capture.mode = decode_CaptureMode(s.capture().mode());
        for (const auto &c : s.capture().cameras())
            step.capture.cameras.push_back({{c}});
        if (s.capture().has_trigger()) {
            const auto &t = s.capture().trigger();
            d::TriggerIntent trigger{{{t.controller()}}, {{t.request()}}, {}};
            for (const auto &e : t.endpoints())
                trigger.endpoints.push_back({{e}});
            step.capture.trigger = std::move(trigger);
        }
        if (s.has_evidence_requirement())
            step.evidence_requirement = decode_EvidenceRequirement(s.evidence_requirement());
        step.required_scope = decode_EvidenceScope(s.required_scope());
        step.settle = d::Duration{s.settle_ns()};
        step.max_duration = d::Duration{s.max_duration_ns()};
        o.steps.push_back(std::move(step));
    }
    o.repetitions = v.repetitions();
    o.bounds = bounds(v.bounds());
    // Expected semantic rejection is returned by pure L3 validation, not a fabricated run.
    return o;
}
void write_projected_program(w::ProjectedAcquisitionProgram *o, const d::AcquisitionProgram &v) {
    o->Clear();
    o->set_type(v.type.name);
    o->set_schema_version(v.type.version);
    reference(o->mutable_identity(), v.identity);
    auto *p = o->mutable_participants();
    for (const auto &c : v.participants.cameras) {
        auto *i = p->add_cameras();
        i->set_component(c.component.id.value);
        i->set_stream(c.stream.id.value);
        i->set_role(c.role);
    }
    for (const auto &c : v.participants.emitters)
        p->add_emitters(c.id.value);
    for (const auto &c : v.participants.controllers)
        p->add_controllers(c.id.value);
    for (const auto &s : v.steps) {
        auto *i = o->add_steps();
        i->set_index(s.index);
        i->set_label(s.label);
        for (const auto &e : s.emitters) {
            auto *a = i->add_emitters();
            a->set_emitter(e.emitter.id.value);
            a->set_state(encode_EmitterState(e.state));
        }
        auto *c = i->mutable_capture();
        if (s.capture.mode)
            c->set_mode(encode_CaptureMode(*s.capture.mode));
        for (const auto &x : s.capture.cameras)
            c->add_cameras(x.id.value);
        if (s.capture.trigger) {
            auto *t = c->mutable_trigger();
            t->set_controller(s.capture.trigger->controller.id.value);
            t->set_request(s.capture.trigger->request.id.value);
            for (const auto &x : s.capture.trigger->endpoints)
                t->add_endpoints(x.id.value);
        }
        if (s.evidence_requirement)
            i->set_evidence_requirement(encode_EvidenceRequirement(*s.evidence_requirement));
        i->set_required_scope(encode_EvidenceScope(s.required_scope));
        i->set_settle_ns(s.settle.count());
        i->set_max_duration_ns(s.max_duration.count());
    }
    o->set_repetitions(v.repetitions);
    bounds(o->mutable_bounds(), v.bounds);
}
bool dispatch_projected(services::Runtime &runtime, const w::Request &q, w::Response &out) {
    using R = w::Request;
    switch (q.command_case()) {
    case R::kProjectedDevicesList:
        for (const auto &v : runtime.projected_devices())
            device(out.add_projected_devices(), v);
        break;
    case R::kProjectedValidate:
        validation(out.mutable_projected_validation(),
                   runtime.validate_projected(request(q.projected_validate())));
        break;
    case R::kProjectedStart:
        capture(out.add_projected_captures(),
                runtime.start_projected(request(q.projected_start()), q.request_id()));
        break;
    case R::kProjectedStatus:
        capture(out.add_projected_captures(), runtime.projected_status({q.projected_status().id()}));
        break;
    case R::kProjectedCapturesList:
        for (const auto &v : runtime.projected_captures())
            capture(out.add_projected_captures(), v);
        break;
    case R::kProjectedStop: {
        const auto &s = q.projected_stop();
        capture(out.add_projected_captures(), runtime.stop_projected({{s.capture_id()},
                                                                      {{s.expected_run_id()}},
                                                                      {{s.expected_generation_id()}},
                                                                      decode_StopMode(s.mode())}));
        break;
    }
    case R::kProjectedBundle: {
        auto ref = runtime.projected_bundle({q.projected_bundle().id()});
        auto *d = out.mutable_data();
        d->set_transport("local-mapped-file");
        d->set_locator(ref.path.string());
        d->set_format_version(3);
        d->set_lease_id(ref.lease.value);
        d->set_lease_seconds(60);
        break;
    }
    default:
        return false;
    }
    return true;
}
} // namespace mantis::protocol
