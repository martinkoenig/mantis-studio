#include "calibration_controller.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <atomic>
#include <iostream>
#include <mutex>
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(#x);                                                                    \
    } while (false)
namespace w = mantis::wire::v1;
void until(const std::function<bool()> &condition) {
    QElapsedTimer timer;
    timer.start();
    while (!condition()) {
        QCoreApplication::processEvents();
        QThread::msleep(1);
        if (timer.elapsed() > 10000)
            throw std::runtime_error("Presentation timeout");
    }
    QCoreApplication::processEvents();
}
QVariantMap selected(const CalibrationController &c) {
    return c.state()["selected"].toMap();
}
QVariantMap jobs(const CalibrationController &c) {
    return c.state()["jobs"].toMap();
}
QVariantMap errors(const CalibrationController &c) {
    return c.state()["errors"].toMap();
}
w::CalibrationInfo info(const std::string &id, const std::string &type) {
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
    Fake() {
        auto t = ::info("T", "CalibrationTarget");
        auto *s = t.mutable_target_info()->mutable_target();
        s->set_squares_x(8);
        s->set_squares_y(6);
        s->set_nominal_square_size_mm(40);
        s->mutable_charuco()->set_pattern_layout("black_square_at_origin");
        infos["T"] = t;
        auto d = ::info("D", "CalibrationDataset");
        d.mutable_dataset_info()->mutable_target()->set_id("T");
        infos["D"] = d;
        for (auto role : {"left", "right"}) {
            std::string id = std::string(role) == "left" ? "L" : "R";
            auto c = ::info(id, "CameraCalibration");
            c.mutable_camera_info()->mutable_dataset()->set_id("D");
            c.mutable_camera_info()->mutable_target()->set_id("T");
            c.mutable_camera_info()->mutable_camera()->set_role(role);
            infos[id] = c;
        }
        auto other = infos["L"];
        other.mutable_entry()->mutable_artifact()->set_id("cross-dataset");
        other.mutable_camera_info()->mutable_dataset()->set_id("other");
        infos["cross-dataset"] = other;
        auto r = ::info("G", "RigCalibration");
        auto *rig = r.mutable_rig_info();
        rig->mutable_dataset()->set_id("D");
        rig->mutable_target()->set_id("T");
        rig->mutable_left_camera()->set_id("L");
        rig->mutable_right_camera()->set_id("R");
        infos["G"] = r;
        auto incomplete = ::info("open", "CalibrationTarget");
        incomplete.mutable_entry()->mutable_artifact()->set_state("OPEN");
        infos["open"] = incomplete;
        infos["broken"] = ::info("broken", "CalibrationTarget");
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
        std::lock_guard lock(mutex);
        std::vector<w::CalibrationEntry> out;
        for (const auto &[id, i] : infos) {
            (void)id;
            out.push_back(i.entry());
        }
        return out;
    }
    w::CalibrationInfo info(const std::string &id) const override {
        std::lock_guard lock(mutex);
        CHECK(id != "open");
        if (id == "broken")
            mantis::fail(mantis::Status::corrupt, "Broken immutable graph", "calibration.inspection");
        return infos.at(id);
    }
    w::Response snapshot() const override {
        std::lock_guard lock(mutex);
        return current;
    }
    w::CalibrationEntry create(const w::CalibrationTargetSpecification &s) const override {
        std::lock_guard lock(mutex);
        created = s;
        auto t = ::info("new-target", "CalibrationTarget");
        *t.mutable_target_info()->mutable_target() = s;
        infos["new-target"] = t;
        return t.entry();
    }
    std::string dataset(const w::CalibrationDatasetBuild &r) const override {
        std::lock_guard lock(mutex);
        built = r;
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
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QVariantMap form{{"pattern", "charuco"},
                         {"squaresX", "8"},
                         {"squaresY", 6},
                         {"squareMm", "40"},
                         {"markerMm", "25"},
                         {"dictionary", "DICT_6X6_250"},
                         {"layout", "white_square_at_origin_even_rows"},
                         {"measuredWidth", "330.4"},
                         {"measuredHeight", "243.6"}};
        auto spec = CalibrationController::targetSpecification(form);
        CHECK(spec.charuco().pattern_layout() == "white_square_at_origin_even_rows");
        CHECK(spec.charuco().dictionary() == "DICT_6X6_250");
        CHECK(spec.squares_x() == 8 && spec.squares_y() == 6);
        CHECK(spec.measurement().active_width_mm() == 330.4 &&
              spec.measurement().active_height_mm() == 243.6);
        CHECK(!spec.measurement().has_provenance());
        form["provenancePresent"] = true;
        spec = CalibrationController::targetSpecification(form);
        CHECK(spec.measurement().has_provenance());
        CHECK(!spec.measurement().provenance().has_instrument());
        form["instrument"] = "";
        form["widthUncertainty"] = 0.2;
        spec = CalibrationController::targetSpecification(form);
        CHECK(spec.measurement().provenance().has_instrument());
        CHECK(spec.measurement().provenance().instrument().empty());
        CHECK(spec.measurement().provenance().width_uncertainty_mm() == 0.2);
        auto checker = form;
        checker["pattern"] = "checkerboard";
        CHECK(CalibrationController::targetSpecification(checker).has_checkerboard());
        auto fake = std::make_shared<Fake>();
        CalibrationController c(fake);
        c.refresh();
        until([&] { return c.artifacts().size() == 8; });
        CHECK(c.devices().size() == 1);
        CHECK(c.choices("target").size() == 1);
        CHECK(c.stages().size() == 7);
        bool inspectionError{};
        for (const auto &artifact : c.artifacts())
            if (artifact.toMap()["id"] == "broken") {
                CHECK(artifact.toMap()["error"].toMap()["component"] == "calibration.inspection");
                inspectionError = true;
            }
        CHECK(inspectionError);
        CHECK(errors(c).empty());
        c.createTarget(form);
        until([&] { return selected(c)["target"] == "new-target"; });
        {
            std::lock_guard lock(fake->mutex);
            CHECK(fake->created.SerializeAsString() == spec.SerializeAsString());
        }
        c.selectArtifact("target", "T");
        c.setCaptureSelected("not-finalized", true);
        CHECK(errors(c)["captures"].toMap()["status"] == "incompatible");
        CHECK(c.state()["rawIds"].toStringList().empty());
        for (int n = 0; n < 1025; ++n)
            c.setCaptureSelected(QString("raw%1").arg(n), true);
        c.setCaptureSelected("raw1", true);
        CHECK(c.state()["rawIds"].toStringList().size() == 1024);
        c.buildDataset(40);
        until([&] { return jobs(c)["dataset"].toMap()["id"] == "dataset-job"; });
        {
            std::lock_guard lock(fake->mutex);
            CHECK(fake->built.target_artifact_id() == "T");
            CHECK(fake->built.raw_capture_artifact_ids_size() == 1024);
            CHECK(fake->built.camera_roles(0) == "left" && fake->built.camera_roles(1) == "right");
        }
        fake->job("dataset-job", "Running", {}, .42);
        c.observeSnapshot(fake->snapshot());
        CHECK(jobs(c)["dataset"].toMap()["progress"].toDouble() == .42);
        fake->job("dataset-job", "Completed", "D", 1);
        c.observeSnapshot(fake->snapshot());
        CHECK(selected(c)["dataset"] == "D");
        CHECK(c.choices("left").size() == 1);
        c.solveBoth(3);
        until([&] { return jobs(c).contains("left") && jobs(c).contains("right"); });
        fake->job("left-job", "Completed", "L", 1);
        fake->job("right-job", "Failed");
        c.observeSnapshot(fake->snapshot());
        CHECK(selected(c)["left"] == "L");
        CHECK(selected(c)["right"].toString().isEmpty());
        CHECK(jobs(c)["right"].toMap()["diagnostics"] == "useful diagnostics");
        c.solveCamera("right", 4);
        until([&] { return jobs(c)["right"].toMap()["state"] == "Queued"; });
        fake->job("right-job", "Running");
        c.observeSnapshot(fake->snapshot());
        c.cancelJob("right");
        until([&] {
            std::lock_guard lock(fake->mutex);
            return fake->cancelled == "right-job";
        });
        fake->job("right-job", "Cancelled");
        c.observeSnapshot(fake->snapshot());
        CHECK(jobs(c)["right"].toMap()["state"] == "Cancelled");
        CHECK(selected(c)["left"] == "L");
        CHECK(selected(c)["dataset"] == "D");
        c.selectArtifact("right", "R");
        c.solveRig(3, "explicit.frame", "Generic rig");
        until([&] { return jobs(c)["rig"].toMap()["id"] == "rig-job"; });
        {
            std::lock_guard lock(fake->mutex);
            CHECK(fake->solvedRig.left_camera_artifact_id() == "L");
            CHECK(fake->solvedRig.right_camera_artifact_id() == "R");
            CHECK(fake->solvedRig.dataset_artifact_id() == "D");
            CHECK(fake->solvedRig.rig_frame_id() == "explicit.frame");
        }
        fake->job("rig-job", "Completed", "G");
        c.observeSnapshot(fake->snapshot());
        CHECK(selected(c)["rig"] == "G");
        CHECK(fake->activations == 0);
        c.selectDevice("third-party");
        until([&] { return !c.state()["pending"].toMap()["activation"].toBool(); });
        CHECK(c.state()["rigFrameId"].toString().isEmpty());
        c.activate("stale", "G");
        CHECK(fake->activations == 0);
        CHECK(errors(c)["activation"].toMap()["status"] == "invalid_argument");
        c.activate("third-party", "G");
        until([&] { return fake->activations == 1 && c.state()["active"].toMap()["id"] == "G"; });
        CHECK(c.state()["active"].toMap()["revision"].toULongLong() == 2);
        {
            std::lock_guard lock(fake->mutex);
            fake->rejectActivation = true;
        }
        c.activate("third-party", "G");
        until([&] { return errors(c)["activation"].toMap()["status"] == "incompatible"; });
        CHECK(errors(c)["activation"].toMap()["component"] == "calibration.activation");
        CHECK(errors(c)["activation"].toMap()["message"].toString().contains("geometry"));
        c.clearActive("third-party");
        until([&] { return c.state()["active"].toMap().isEmpty(); });
        CHECK(selected(c)["rig"] == "G");
        c.selectArtifact("target", "T");
        c.selectArtifact("rig", "G");
        CHECK(selected(c)["dataset"] == "D" && selected(c)["left"] == "L" && selected(c)["right"] == "R");
        c.startCapture();
        until([&] { return c.state()["captureId"] == "capture"; });
        {
            std::lock_guard lock(fake->mutex);
            auto *capture = fake->current.add_captures();
            capture->set_id("capture");
            capture->set_raw_artifact("not-finalized");
            capture->set_finalization_job_id("finalize-job");
        }
        c.observeSnapshot(fake->snapshot());
        CHECK(!c.state()["recording"].toBool());
        CHECK(c.state()["captureState"] == "Finalizing RawCapture");
        c.setCaptureSelected("raw0", false);
        {
            std::lock_guard lock(fake->mutex);
            for (auto &a : *fake->current.mutable_artifacts())
                if (a.id() == "not-finalized")
                    a.set_state("FINALIZED");
        }
        c.observeSnapshot(fake->snapshot());
        CHECK(c.state()["captureState"].toString().contains("finalized"));
        c.setCaptureSelected("not-finalized", true);
        CHECK(c.state()["rawIds"].toStringList().contains("not-finalized"));
        // A result for superseded inputs stays available without changing new selections.
        c.buildDataset(40);
        until([&] { return jobs(c)["dataset"].toMap()["state"] == "Queued"; });
        c.selectArtifact("target", "new-target");
        fake->job("dataset-job", "Completed", "D");
        c.observeSnapshot(fake->snapshot());
        CHECK(selected(c)["dataset"].toString().isEmpty());
        CHECK(!c.choices("dataset").empty());
        std::cout << "Studio calibration presentation contracts passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
