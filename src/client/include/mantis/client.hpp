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
    // Control-only calibration API. Jobs never wait implicitly.
    wire::v1::CalibrationEntry create_calibration_target(const wire::v1::CalibrationTargetSpecification &,
                                                         const std::string &series_id = {}) const;
    std::string build_calibration_dataset(const std::string &target, const std::vector<std::string> &raw_captures,
                                         const std::vector<std::string> &roles, uint32_t max_samples,
                                         const std::string &series_id = {}) const;
    std::string solve_camera_calibration(const std::string &dataset, const std::string &role, uint32_t heldout,
                                         const std::string &series_id = {}) const;
    std::string solve_rig_calibration(const std::string &dataset, const std::string &left, const std::string &right,
                                      uint32_t heldout, const std::string &rig_frame_id, const std::string &rig_frame_name,
                                      const std::string &series_id = {}) const;
    std::vector<wire::v1::CalibrationEntry> calibrations() const;
    wire::v1::CalibrationInfo calibration_info(const std::string &) const;
    std::optional<wire::v1::ActiveCalibrationBinding> active_calibration(const std::string &device) const;
    void activate_calibration(const std::string &device, const std::string &rig) const;
    void clear_calibration(const std::string &device) const;
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
