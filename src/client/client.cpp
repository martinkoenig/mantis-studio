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
void Client::stop_capture(const std::string &id) const {
    wire::v1::Request r;
    r.mutable_capture_stop()->set_id(id);
    (void)call(r);
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
} // namespace mantis::client
