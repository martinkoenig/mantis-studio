#include <cstdlib>
#include <mantis/client.hpp>
#include <mantis/data_io.hpp>
#include <thread>
namespace mantis::client {
Endpoint Endpoint::environment() {
    Endpoint e;
    if (auto p = std::getenv("MANTIS_PORT")) {
        auto n = std::stoul(p);
        if (n == 0 || n > 65535)
            fail(Status::invalid_argument, "Invalid MANTIS_PORT");
        e.port = static_cast<uint16_t>(n);
    }
    if (auto p = std::getenv("MANTIS_TOKEN"))
        e.token = p;
    return e;
}
wire::v1::Response Client::call(wire::v1::Request request) const {
    if (endpoint_.token.empty())
        fail(Status::invalid_argument, "Set MANTIS_TOKEN to the daemon's local access token");
    request.set_protocol_version(protocol::version);
    request.set_request_id(Id::random().value);
    request.set_token(endpoint_.token);
    auto socket = platform::Socket::connect(endpoint_.port);
    protocol::send(socket, request);
    wire::v1::Response response;
    protocol::receive(socket, response);
    if (response.request_id() != request.request_id() || response.protocol_version() != protocol::version)
        fail(Status::incompatible, "Response correlation/version mismatch");
    if (response.has_error())
        throw Failure({static_cast<Status>(response.error().code()), response.error().message(),
                       response.error().component()});
    return response;
}
wire::v1::Response Client::snapshot() const {
    wire::v1::Request r;
    r.mutable_snapshot();
    return call(r);
}
std::vector<wire::v1::Device> Client::devices() const {
    wire::v1::Request r;
    r.mutable_devices_list();
    auto result = call(r);
    return {result.devices().begin(), result.devices().end()};
}
std::string Client::start_capture(const std::vector<std::string> &devices) const {
    wire::v1::Request r;
    for (auto &id : devices)
        r.mutable_capture_start()->add_devices(id);
    return call(r).result_id();
}
wire::v1::Capture Client::capture_status(const std::string &id) const {
    wire::v1::Request request; request.mutable_capture_status()->set_id(id); return call(request).captures(0);
}
wire::v1::Response Client::captures() const {
    wire::v1::Request request; request.mutable_captures_list(); return call(request);
}
wire::v1::Device Client::device_info(const std::string &id) const {
    wire::v1::Request request; request.mutable_devices_info()->set_id(id); return call(request).devices(0);
}
std::string Client::replay(const std::string &id, bool realtime, bool verify) const {
    wire::v1::Request request; auto *replay = request.mutable_replay();
    replay->set_artifact_id(id); replay->set_real_time(realtime); replay->set_verify(verify);
    return call(request).result_id();
}
data::Published Client::preview(const std::string &id) const {
    wire::v1::Request request; request.mutable_preview()->set_id(id);
    wire::v1::Response result;
    try { result = call(request); }
    catch (const Failure &e) { if (e.error.code == Status::busy) return {}; throw; }
    const auto &ref = result.data();
    auto release = [&] { wire::v1::Request r; r.mutable_preview_release()->set_id(ref.lease_id()); (void)call(r); };
    try {
        if (ref.transport() != "local-mapped-file" || ref.format_version() != 2 || ref.lease_id().empty())
            fail(Status::incompatible, "Unsupported preview reference");
        auto packet = data::read_packet(ref.locator()); release(); return packet;
    } catch (...) { try { release(); } catch (...) {} throw; }
}
void Client::stop_capture(const std::string &id) const {
    wire::v1::Request r;
    r.mutable_capture_stop()->set_id(id);
    auto result = call(r);
    if (result.captures_size() && !result.captures(0).finalization_job_id().empty())
        (void)wait(result.captures(0).finalization_job_id(), std::chrono::hours(1));
}
wire::v1::Artifact Client::recover_artifact(const std::string &id) const {
    wire::v1::Request request;
    request.mutable_artifact_recover_async()->set_id(id);
    (void)wait(call(request).result_id(), std::chrono::hours(1));
    request.Clear(); request.mutable_artifacts_list();
    auto response = call(request);
    for (const auto &artifact : response.artifacts())
        if (artifact.id() == id) return artifact;
    fail(Status::not_found, "Recovered artifact not found");
}
std::string Client::run_pipeline(const std::string &capture, const std::string &recipe,
                                 const std::string &raw) const {
    wire::v1::Request r;
    auto *p = r.mutable_pipeline_run();
    p->set_capture_id(capture);
    p->set_recipe(recipe);
    p->set_raw_artifact_id(raw);
    return call(r).result_id();
}
wire::v1::Job Client::wait(const std::string &id, std::chrono::milliseconds timeout,
                           const CancellationToken &token) const {
    auto end = std::chrono::steady_clock::now() + timeout;
    while (true) {
        token.check();
        auto s = snapshot();
        for (auto &j : s.jobs())
            if (j.id() == id) {
                if (j.state() == "Completed")
                    return j;
                if (j.state() == "Failed")
                    fail(Status::plugin_failed, j.diagnostics(), "job");
                if (j.state() == "Cancelled")
                    fail(Status::cancelled, "Job cancelled");
            }
        if (std::chrono::steady_clock::now() >= end)
            fail(Status::busy, "Job wait timeout; job remains runtime-owned");
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
}
std::string Client::export_artifact(const std::string &id, const std::filesystem::path &path) const {
    wire::v1::Request r;
    r.mutable_export_artifact()->set_artifact_id(id);
    r.mutable_export_artifact()->set_path(std::filesystem::absolute(path).string());
    return call(r).result_id();
}
data::Published Client::data(const std::string &artifact) const {
    wire::v1::Request r;
    r.mutable_artifact_data()->set_id(artifact);
    auto reply = call(r);
    if (reply.data().transport() != "local-mapped-file" || reply.data().format_version() != 1)
        fail(Status::unsupported, "Unsupported data-plane transport");
    return data::read_packet(reply.data().locator());
}
wire::v1::CalibrationEntry Client::create_calibration_target(const wire::v1::CalibrationTargetSpecification &target,
                                                            const std::string &series) const {
    wire::v1::Request r; *r.mutable_calibration_target_create()->mutable_target() = target;
    r.mutable_calibration_target_create()->set_series_id(series);
    return call(r).calibration_info().entry();
}
std::string Client::build_calibration_dataset(const std::string &target, const std::vector<std::string> &raw,
                                             const std::vector<std::string> &roles, uint32_t max_samples,
                                             const std::string &series) const {
    wire::v1::Request r; auto *d = r.mutable_calibration_dataset_build(); d->set_target_artifact_id(target);
    for (const auto &id : raw) d->add_raw_capture_artifact_ids(id);
    for (const auto &role : roles) d->add_camera_roles(role);
    d->set_max_selected_per_camera(max_samples); d->set_series_id(series); return call(r).result_id();
}
std::string Client::solve_camera_calibration(const std::string &dataset, const std::string &role, uint32_t heldout,
                                             const std::string &series) const {
    wire::v1::Request r; auto *c = r.mutable_calibration_camera_solve(); c->set_dataset_artifact_id(dataset);
    c->set_camera_role(role); c->set_heldout_per_camera(heldout); c->set_series_id(series); return call(r).result_id();
}
std::string Client::solve_rig_calibration(const std::string &dataset, const std::string &left, const std::string &right,
                                         uint32_t heldout, const std::string &frame_id, const std::string &frame_name,
                                         const std::string &series) const {
    wire::v1::Request r; auto *c = r.mutable_calibration_rig_solve(); c->set_dataset_artifact_id(dataset);
    c->set_left_camera_artifact_id(left); c->set_right_camera_artifact_id(right); c->set_heldout_pairs(heldout);
    c->set_rig_frame_id(frame_id); c->set_rig_frame_name(frame_name); c->set_series_id(series); return call(r).result_id();
}
std::vector<wire::v1::CalibrationEntry> Client::calibrations() const {
    wire::v1::Request r; r.mutable_calibrations_list(); auto out = call(r);
    return {out.calibrations().begin(), out.calibrations().end()};
}
wire::v1::CalibrationInfo Client::calibration_info(const std::string &id) const {
    wire::v1::Request r; r.mutable_calibration_info()->set_id(id); return call(r).calibration_info();
}
std::optional<wire::v1::ActiveCalibrationBinding> Client::active_calibration(const std::string &device) const {
    wire::v1::Request r; r.mutable_calibration_active()->set_id(device); auto out = call(r);
    if (out.active_calibration().has_binding()) return out.active_calibration().binding();
    return {};
}
void Client::activate_calibration(const std::string &device, const std::string &rig) const {
    wire::v1::Request r; r.mutable_calibration_activate()->set_logical_device_id(device);
    r.mutable_calibration_activate()->set_rig_artifact_id(rig); (void)call(r);
}
void Client::clear_calibration(const std::string &device) const {
    wire::v1::Request r; r.mutable_calibration_clear()->set_id(device); (void)call(r);
}
} // namespace mantis::client
