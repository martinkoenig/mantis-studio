#include <mantis/service_adapter.hpp>
#include "calibration_adapter.hpp"
namespace mantis::protocol {
namespace {
void device(wire::v1::Response &r, const device::Descriptor &d) {
    auto *o = r.add_devices();
    o->set_id(d.id.value);
    o->set_name(d.name);
    o->set_plugin_id(d.plugin_id);
    o->set_parent(d.parent.value);
    for (const auto &[key, value] : d.metadata) (*o->mutable_metadata())[key] = value;
    for (auto &c : d.capabilities)
        o->add_capabilities(c);
    for (auto &c : d.children)
        o->add_children(c.value);
}
void capture(wire::v1::Response &r, const services::CaptureInfo &c) {
    auto *o = r.add_captures();
    o->set_id(c.id.value);
    for (auto &id : c.devices)
        o->add_devices(id.value);
    o->set_raw_artifact(c.raw_artifact.value);
    o->set_active(c.active);
    o->set_frames(c.frames);
    o->set_dropped(c.dropped);
    o->set_queue_high_water(c.queue_high_water);
    o->set_error(c.error); o->set_finalization_job_id(c.finalization_job.value);
    o->set_framesets_produced(c.produced); o->set_framesets_committed(c.committed);
    o->set_queue_depth(c.queue_depth); o->set_queue_capacity(c.queue_capacity);
    o->set_queue_saturation(c.queue_saturation); o->set_preview_drops(c.preview_drops);
    o->set_total_bytes(c.total_bytes); o->set_duration_seconds(c.duration);
    o->set_writer_mb_s(c.writer_mb_s); o->set_writer_mib_s(c.writer_mib_s);
    for (const auto &[key, value] : c.diagnostics) (*o->mutable_diagnostics())[key] = value;
}
void artifact(wire::v1::Response &r, const artifact::ArtifactDescriptor &a) {
    write_artifact(r.add_artifacts(), a);
}
void plugin(wire::v1::Response &r, const services::PluginInfo &p) {
    auto *o = r.add_plugins();
    o->set_id(p.id);
    o->set_version(p.version);
    o->set_kind(p.kind);
    o->set_execution(p.execution);
    o->set_state(p.state);
    o->set_diagnostic(p.diagnostic);
    for (auto &permission : p.permissions)
        o->add_permissions(permission);
}
void job(wire::v1::Response &r, const jobs::Snapshot &j) {
    auto *o = r.add_jobs();
    o->set_id(j.id.value);
    o->set_name(j.name);
    o->set_state(jobs::state_name(j.state));
    o->set_progress(j.progress);
    o->set_status(j.status);
    o->set_diagnostics(j.diagnostics);
    if (j.result)
        o->set_result_artifact(j.result->id.value);
}
void events(wire::v1::Response &r, const std::vector<services::Event> &log) {
    for (auto &e : log) {
        auto *o = r.add_events();
        o->set_sequence(e.sequence);
        o->set_kind(e.kind);
        o->set_component(e.component);
        o->set_message(e.message);
    }
}
} // namespace
void write_artifact(wire::v1::Artifact *o, const artifact::ArtifactDescriptor &a) {
    o->set_id(a.id.value);
    o->set_type(a.type.name);
    o->set_schema_version(a.type.schema_version);
    o->set_state(artifact::state_name(a.state));
    o->set_producer(a.provenance.producer);
    for (auto &id : a.provenance.inputs)
        o->add_inputs(id.value);
    o->set_hash(a.hash.algorithm + ":" + a.hash.hex);
    o->set_bytes(a.bytes);
    o->set_chunks(a.chunks);
}
wire::v1::Response dispatch(services::Runtime &runtime, const wire::v1::Request &request) {
    wire::v1::Response out;
    out.set_protocol_version(version);
    out.set_request_id(request.request_id());
    try {
        if (request.protocol_version() != version)
            fail(Status::incompatible, "Unsupported control protocol version");
        using R = wire::v1::Request;
        switch (request.command_case()) {
        case R::kSnapshot:
            out.set_project_path(runtime.project());
            for (auto &d : runtime.devices())
                device(out, d);
            for (auto &c : runtime.captures())
                capture(out, c);
            for (auto &a : runtime.artifacts())
                artifact(out, a);
            for (auto &j : runtime.jobs())
                job(out, j);
            for (auto &p : runtime.plugins())
                plugin(out, p);
            events(out, runtime.events(0));
            break;
        case R::kDevicesList:
            for (auto &d : runtime.devices())
                device(out, d);
            break;
        case R::kCaptureStart: {
            std::vector<Id> ids;
            for (auto &id : request.capture_start().devices())
                ids.push_back({id});
            auto c = runtime.start_capture(ids);
            capture(out, c);
            out.set_result_id(c.id.value);
            break;
        }
        case R::kCaptureStop:
            capture(out, runtime.stop_capture({request.capture_stop().id()}));
            break;
        case R::kPipelineRun:
            out.set_result_id(runtime
                                  .run_pipeline({request.pipeline_run().capture_id()},
                                                request.pipeline_run().recipe(),
                                                {request.pipeline_run().raw_artifact_id()})
                                  .value);
            break;
        case R::kProjectOpen:
            out.set_project_path(
                runtime.open_project(request.project_open().path(), request.project_open().create()));
            break;
        case R::kArtifactsList:
            for (auto &a : runtime.artifacts())
                artifact(out, a);
            break;
        case R::kExportArtifact:
            out.set_result_id(runtime
                                  .export_artifact({request.export_artifact().artifact_id()},
                                                   request.export_artifact().path())
                                  .value);
            break;
        case R::kJobCancel:
            runtime.cancel_job({request.job_cancel().id()});
            break;
        case R::kPluginsList:
            for (auto &p : runtime.plugins())
                plugin(out, p);
            break;
        case R::kEvents:
            events(out, runtime.events(request.events().after()));
            break;
        case R::kArtifactData: {
            auto *d = out.mutable_data();
            d->set_artifact_id(request.artifact_data().id());
            d->set_transport("local-mapped-file");
            d->set_locator(runtime.data_reference({request.artifact_data().id()}).string());
            d->set_format_version(1);
            break;
        }
        case R::kArtifactRecover:
            artifact(out, runtime.recover_artifact({request.artifact_recover().id()}));
            break;
        case R::kArtifactRecoverAsync:
            out.set_result_id(runtime.recover_artifact_job({request.artifact_recover_async().id()}).value);
            break;
        case R::kPluginEnable:
            runtime.enable_plugin(request.plugin_enable().id(), request.plugin_enable().enabled());
            break;
        case R::kDevicesInfo: {
            bool found = false;
            for (const auto &d : runtime.devices()) if (d.id.value == request.devices_info().id()) { device(out, d); found = true; }
            if (!found) fail(Status::not_found, "Device not found");
            break;
        }
        case R::kCaptureStatus: {
            bool found = false;
            for (const auto &c : runtime.captures()) if (c.id.value == request.capture_status().id()) { capture(out, c); found = true; }
            if (!found) fail(Status::not_found, "Capture handle not found; after restart inspect its RawCapture artifact");
            break;
        }
        case R::kCapturesList:
            for (const auto &c : runtime.captures()) capture(out, c);
            for (const auto &a : runtime.artifacts()) if (a.type.name == "org.mantis.RawCapture") artifact(out, a);
            break;
        case R::kPreview: {
            auto ref = runtime.preview({request.preview().id()}); auto *d = out.mutable_data();
            d->set_transport("local-mapped-file"); d->set_locator(ref.path.string());
            d->set_format_version(2); d->set_lease_id(ref.lease.value); d->set_lease_seconds(60);
            break;
        }
        case R::kPreviewRelease:
            runtime.release_preview({request.preview_release().id()}); break;
        case R::kReplay:
            out.set_result_id(runtime.replay_capture({request.replay().artifact_id()}, request.replay().real_time(), request.replay().verify()).value);
            break;
        case R::kShutdown:
            break;
        default:
            if (!dispatch_calibration(runtime, request, out))
                fail(Status::invalid_argument, "Missing or unknown command");
        }
    } catch (const Failure &e) {
        out.mutable_error()->set_code(static_cast<uint32_t>(e.error.code));
        out.mutable_error()->set_message(e.what());
        out.mutable_error()->set_component(e.error.component);
    } catch (const std::exception &e) {
        out.mutable_error()->set_code(static_cast<uint32_t>(Status::io));
        out.mutable_error()->set_message(e.what());
    }
    if (out.ByteSizeLong() > max_control_bytes) {
        out.Clear(); out.set_protocol_version(version); out.set_request_id(request.request_id());
        out.mutable_error()->set_code(static_cast<uint32_t>(Status::busy));
        out.mutable_error()->set_message("Control response exceeds 4 MiB; inspect individual artifacts");
    }
    return out;
}
} // namespace mantis::protocol
