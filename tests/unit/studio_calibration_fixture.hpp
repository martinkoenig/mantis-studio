#pragma once
#include "calibration_controller.hpp"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace calibration_test {
namespace w = mantis::wire::v1;
// Only fixture workers wait here. Tests observe actual entry before switching projects.
struct Gate {
    std::atomic<bool> entered{}, exited{};
    std::mutex mutex;
    std::condition_variable condition;
    bool released{};
    void wait() {
        std::unique_lock lock(mutex);
        entered = true;
        condition.wait(lock, [&] { return released; });
        exited = true;
    }
    void release() {
        std::lock_guard lock(mutex);
        released = true;
        condition.notify_all();
    }
};
struct ReleaseGates {
    std::shared_ptr<Gate> refresh, operation;
    ~ReleaseGates() {
        if (refresh)
            refresh->release();
        if (operation)
            operation->release();
    }
};
inline w::CalibrationInfo info(const std::string &id, const std::string &type) {
    w::CalibrationInfo i;
    auto *e = i.mutable_entry();
    e->mutable_artifact()->set_id(id);
    e->mutable_artifact()->set_type("org.mantis." + type);
    e->mutable_artifact()->set_state("FINALIZED");
    e->mutable_reference()->set_id("logical." + id);
    e->mutable_reference()->set_revision(2);
    return i;
}
class Fake final : public CalibrationClient {
  public:
    mutable std::mutex mutex;
    mutable std::map<std::string, w::CalibrationInfo> infos;
    mutable w::Response current;
    mutable w::CalibrationTargetSpecification created;
    mutable w::CalibrationDatasetBuild built;
    mutable w::CalibrationRigSolve solvedRig;
    mutable std::map<std::string, w::CalibrationCameraSolve> solvedCameras;
    mutable std::optional<w::ActiveCalibrationBinding> binding;
    mutable std::string cancelled;
    mutable std::atomic<int> activations{};
    mutable bool rejectActivation{};
    mutable std::shared_ptr<Gate> listGate, datasetGate;
    mutable std::atomic<int> datasetCalls{}, snapshots{}, infoCalls{};
    mutable bool disconnected{}, failDataset{}, failList{}, corruptBroken{true};
    Fake() {
        current.set_project_path("/fixture/A/project.mantis");
        auto t = calibration_test::info("T", "CalibrationTarget");
        auto *s = t.mutable_target_info()->mutable_target();
        s->set_squares_x(8);
        s->set_squares_y(6);
        s->set_nominal_square_size_mm(40);
        s->mutable_charuco()->set_pattern_layout("black_square_at_origin");
        infos["T"] = t;
        auto d = calibration_test::info("D", "CalibrationDataset");
        d.mutable_dataset_info()->mutable_target()->set_id("T");
        infos["D"] = d;
        for (auto role : {"left", "right"}) {
            std::string id = std::string(role) == "left" ? "L" : "R";
            auto c = calibration_test::info(id, "CameraCalibration");
            c.mutable_camera_info()->mutable_dataset()->set_id("D");
            c.mutable_camera_info()->mutable_target()->set_id("T");
            c.mutable_camera_info()->mutable_camera()->set_role(role);
            infos[id] = c;
        }
        auto other = infos["L"];
        other.mutable_entry()->mutable_artifact()->set_id("cross-dataset");
        other.mutable_camera_info()->mutable_dataset()->set_id("other");
        infos["cross-dataset"] = other;
        auto r = calibration_test::info("G", "RigCalibration");
        auto *rig = r.mutable_rig_info();
        rig->mutable_dataset()->set_id("D");
        rig->mutable_target()->set_id("T");
        rig->mutable_left_camera()->set_id("L");
        rig->mutable_right_camera()->set_id("R");
        infos["G"] = r;
        auto incomplete = calibration_test::info("open", "CalibrationTarget");
        incomplete.mutable_entry()->mutable_artifact()->set_state("OPEN");
        infos["open"] = incomplete;
        infos["broken"] = calibration_test::info("broken", "CalibrationTarget");
        auto *dev = current.add_devices();
        dev->set_id("third-party");
        dev->set_name("Generic scanner");
        dev->add_capabilities("org.mantis.camera.frameset-stream.v1");
        auto *child = current.add_devices();
        child->set_id("sensor");
        child->set_parent("third-party");
        child->add_capabilities("org.mantis.camera.image-stream.v1");
        for (int n = 0; n < 1025; ++n) {
            auto *a = current.add_artifacts();
            a->set_id("raw" + std::to_string(n));
            a->set_type("org.mantis.RawCapture");
            a->set_schema_version(2);
            a->set_state("FINALIZED");
        }
        auto *a = current.add_artifacts();
        a->set_id("not-finalized");
        a->set_type("org.mantis.RawCapture");
        a->set_schema_version(2);
        a->set_state("OPEN");
    }
    std::vector<w::CalibrationEntry> list() const override {
        std::unique_lock lock(mutex);
        auto gate = std::exchange(listGate, {});
        const auto fail = failList;
        std::vector<w::CalibrationEntry> out;
        for (const auto &[id, i] : infos) {
            (void)id;
            out.push_back(i.entry());
        }
        lock.unlock();
        if (gate)
            gate->wait();
        if (fail)
            mantis::fail(mantis::Status::io, "Old project refresh failed", "fixture.list");
        return out;
    }
    w::CalibrationInfo info(const std::string &id) const override {
        std::lock_guard lock(mutex);
        ++infoCalls;
        if (id == "open")
            throw std::runtime_error("Inspected non-finalized artifact");
        if (id == "broken" && corruptBroken)
            mantis::fail(mantis::Status::corrupt, "Broken immutable graph", "calibration.inspection");
        return infos.at(id);
    }
    w::Response snapshot() const override {
        std::lock_guard lock(mutex);
        ++snapshots;
        if (disconnected)
            mantis::fail(mantis::Status::io, "Fixture transport unavailable", "daemon.connection");
        return current;
    }
    w::CalibrationEntry create(const w::CalibrationTargetSpecification &s) const override {
        std::lock_guard lock(mutex);
        created = s;
        auto t = calibration_test::info("new-target", "CalibrationTarget");
        *t.mutable_target_info()->mutable_target() = s;
        infos["new-target"] = t;
        return t.entry();
    }
    std::string dataset(const w::CalibrationDatasetBuild &r) const override {
        std::unique_lock lock(mutex);
        ++datasetCalls;
        built = r;
        auto gate = std::exchange(datasetGate, {});
        const auto fail = failDataset;
        lock.unlock();
        if (gate)
            gate->wait();
        if (fail)
            mantis::fail(mantis::Status::incompatible, "Old project operation failed", "fixture.dataset");
        return "dataset-job";
    }
    std::string camera(const w::CalibrationCameraSolve &r) const override {
        std::lock_guard lock(mutex);
        solvedCameras[r.camera_role()] = r;
        return r.camera_role() + "-job";
    }
    std::string rig(const w::CalibrationRigSolve &r) const override {
        std::lock_guard lock(mutex);
        solvedRig = r;
        return "rig-job";
    }
    std::optional<w::ActiveCalibrationBinding> active(const std::string &) const override {
        std::lock_guard lock(mutex);
        return binding;
    }
    void activate(const std::string &d, const std::string &r) const override {
        std::lock_guard lock(mutex);
        if (rejectActivation)
            mantis::fail(mantis::Status::incompatible, "Physical camera identity / image geometry differs",
                         "calibration.activation");
        ++activations;
        binding.emplace();
        binding->set_logical_device_id(d);
        binding->mutable_artifact()->set_id(r);
        *binding->mutable_reference() = infos.at(r).entry().reference();
    }
    void clear(const std::string &) const override {
        std::lock_guard lock(mutex);
        binding.reset();
    }
    std::string start(const std::string &) const override {
        return "capture";
    }
    void stop(const std::string &) const override {}
    void cancel(const std::string &id) const override {
        std::lock_guard lock(mutex);
        cancelled = id;
    }
    void project(const std::string &path, uint64_t revision) {
        std::lock_guard lock(mutex);
        current.set_project_path(path);
        failList = false;
        corruptBroken = false;
        infos["broken"].mutable_target_info()->mutable_target()->mutable_checkerboard();
        current.clear_jobs();
        current.clear_captures();
        binding.reset();
        for (auto &[id, i] : infos) {
            i.mutable_entry()->mutable_reference()->set_revision(revision);
            if (i.has_target_info())
                i.mutable_target_info()->mutable_target()->set_nominal_square_size_mm(double(revision));
        }
    }
    void job(const std::string &id, const std::string &state, const std::string &result = {},
             double progress = 0) {
        std::lock_guard lock(mutex);
        auto *j = current.add_jobs();
        j->set_id(id);
        j->set_state(state);
        j->set_progress(progress);
        j->set_status("solver status");
        j->set_diagnostics("useful diagnostics");
        j->set_result_artifact(result);
    }
};
} // namespace calibration_test
