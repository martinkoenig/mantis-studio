#pragma once
#include <mantis/data.hpp>
#include <mantis/protocol.hpp>
namespace mantis::client {
struct Endpoint {
    uint16_t port{47321};
    std::string token;
    static Endpoint environment();
};
class Client {
    Endpoint endpoint_;

  public:
    explicit Client(Endpoint endpoint = Endpoint::environment()) : endpoint_(std::move(endpoint)) {}
    wire::v1::Response call(wire::v1::Request) const;
    wire::v1::Response snapshot() const;
    std::vector<wire::v1::Device> devices() const;
    std::string start_capture(const std::vector<std::string> &) const;
    wire::v1::Capture capture_status(const std::string &) const;
    wire::v1::Response captures() const;
    wire::v1::Device device_info(const std::string &) const;
    data::Published preview(const std::string &capture_or_replay) const;
    std::string replay(const std::string &artifact, bool real_time = false, bool verify = false) const;
    void stop_capture(const std::string &) const;
    wire::v1::Artifact recover_artifact(const std::string &) const;
    std::string run_pipeline(const std::string &capture, const std::string &recipe = "example",
                             const std::string &raw_artifact = {}) const;
    wire::v1::Job wait(const std::string &job, std::chrono::milliseconds timeout = std::chrono::seconds(30),
                       const CancellationToken &token = {}) const;
    std::string export_artifact(const std::string &, const std::filesystem::path &) const;
    data::Published data(const std::string &artifact) const;
};
} // namespace mantis::client
