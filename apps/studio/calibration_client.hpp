#pragma once
#include <mantis/client.hpp>

// Studio presentation seam. Production methods compose only the public Client API;
// tests substitute control DTOs, never a service/runtime/store.
class CalibrationClient {
  public:
    virtual ~CalibrationClient() = default;
    virtual std::vector<mantis::wire::v1::CalibrationEntry> list() const = 0;
    virtual mantis::wire::v1::CalibrationInfo info(const std::string &) const = 0;
    virtual mantis::wire::v1::Response snapshot() const = 0;
    virtual mantis::wire::v1::CalibrationEntry
    create(const mantis::wire::v1::CalibrationTargetSpecification &) const = 0;
    virtual std::string dataset(const mantis::wire::v1::CalibrationDatasetBuild &) const = 0;
    virtual std::string camera(const mantis::wire::v1::CalibrationCameraSolve &) const = 0;
    virtual std::string rig(const mantis::wire::v1::CalibrationRigSolve &) const = 0;
    virtual std::optional<mantis::wire::v1::ActiveCalibrationBinding> active(const std::string &) const = 0;
    virtual void activate(const std::string &, const std::string &) const = 0;
    virtual void clear(const std::string &) const = 0;
    virtual std::string start(const std::string &) const = 0;
    virtual void stop(const std::string &) const = 0;
    virtual void cancel(const std::string &) const = 0;
};
class PublicCalibrationClient final : public CalibrationClient {
    mantis::client::Client client_;

  public:
    explicit PublicCalibrationClient(mantis::client::Client client = mantis::client::Client{})
        : client_(std::move(client)) {}
    std::vector<mantis::wire::v1::CalibrationEntry> list() const override {
        return client_.calibrations();
    }
    mantis::wire::v1::CalibrationInfo info(const std::string &id) const override {
        return client_.calibration_info(id);
    }
    mantis::wire::v1::Response snapshot() const override {
        return client_.snapshot();
    }
    mantis::wire::v1::CalibrationEntry
    create(const mantis::wire::v1::CalibrationTargetSpecification &s) const override {
        return client_.create_calibration_target(s);
    }
    std::string dataset(const mantis::wire::v1::CalibrationDatasetBuild &r) const override {
        return client_.build_calibration_dataset(
            r.target_artifact_id(),
            {r.raw_capture_artifact_ids().begin(), r.raw_capture_artifact_ids().end()},
            {r.camera_roles().begin(), r.camera_roles().end()}, r.max_selected_per_camera());
    }
    std::string camera(const mantis::wire::v1::CalibrationCameraSolve &r) const override {
        return client_.solve_camera_calibration(r.dataset_artifact_id(), r.camera_role(),
                                                r.heldout_per_camera());
    }
    std::string rig(const mantis::wire::v1::CalibrationRigSolve &r) const override {
        return client_.solve_rig_calibration(r.dataset_artifact_id(), r.left_camera_artifact_id(),
                                             r.right_camera_artifact_id(), r.heldout_pairs(),
                                             r.rig_frame_id(), r.rig_frame_name());
    }
    std::optional<mantis::wire::v1::ActiveCalibrationBinding> active(const std::string &id) const override {
        return client_.active_calibration(id);
    }
    void activate(const std::string &d, const std::string &r) const override {
        client_.activate_calibration(d, r);
    }
    void clear(const std::string &d) const override {
        client_.clear_calibration(d);
    }
    std::string start(const std::string &d) const override {
        return client_.start_capture({d});
    }
    void stop(const std::string &id) const override {
        client_.stop_capture(id);
    }
    void cancel(const std::string &id) const override {
        mantis::wire::v1::Request r;
        r.mutable_job_cancel()->set_id(id);
        (void)client_.call(r);
    }
};
