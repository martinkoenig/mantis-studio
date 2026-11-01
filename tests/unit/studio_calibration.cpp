#include "calibration_controller.hpp"
#include "studio_calibration_fixture.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QPointer>
#include <QThread>
#include <atomic>
#include <iostream>
#include <mutex>
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(std::string("studio_calibration.cpp:") + std::to_string(__LINE__) +     \
                                     ": " + #x);                                                             \
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
using namespace calibration_test;
void loaded(CalibrationController &c, int revision) {
    until([&] {
        return !c.artifacts().empty() && c.artifacts().front().toMap()["revision"].toInt() == revision;
    });
}
void cleared(const CalibrationController &c) {
    for (auto slot : {"target", "dataset", "left", "right", "rig"}) {
        CHECK(selected(c)[slot].toString().isEmpty());
        CHECK(c.state()[slot].toMap().isEmpty());
    }
    CHECK(c.artifacts().empty());
    CHECK(c.state()["rawIds"].toStringList().empty());
    CHECK(c.state()["captureId"].toString().isEmpty());
    CHECK(c.state()["deviceId"].toString().isEmpty());
    CHECK(c.state()["active"].toMap().empty());
    CHECK(jobs(c).empty());
    CHECK(errors(c).empty());
    CHECK(!c.state()["canBuild"].toBool());
}
void projectGenerations(bool returnToA, bool failOperation) {
    auto fake = std::make_shared<Fake>();
    CalibrationController c(fake);
    auto refresh = std::make_shared<Gate>(), operation = std::make_shared<Gate>();
    // Must unwind before c's destructor joins either future, including failed CHECKs.
    ReleaseGates release{refresh, operation};
    bool wrongThread{}, stalePublication{};
    int changes{};
    QString expectedGeneration;
    QObject::connect(&c, &CalibrationController::changed, &c, [&] {
        ++changes;
        wrongThread |= QThread::currentThread() != c.thread();
        if (!expectedGeneration.isEmpty()) {
            stalePublication |= c.state()["projectGeneration"] != expectedGeneration;
            stalePublication |= !c.state()["projectConfirmed"].toBool();
            stalePublication |= errors(c).contains("connection");
            for (const auto &v : c.artifacts())
                stalePublication |= v.toMap()["revision"].toInt() == 2;
            stalePublication |= c.state()["active"].toMap()["revision"].toInt() == 2;
        }
    });
    c.refresh();
    loaded(c, 2);
    c.selectDevice("third-party");
    c.selectArtifact("rig", "G");
    c.setCaptureSelected("raw0", true);
    c.activate("third-party", "G");
    until([&] { return c.state()["active"].toMap()["id"] == "G"; });
    c.startCapture();
    until([&] { return c.state()["captureId"] == "capture"; });
    c.buildDataset(40);
    until([&] { return jobs(c).contains("dataset"); });
    fake->job("dataset-job", "Failed");
    c.observeSnapshot(fake->snapshot());
    c.setCaptureSelected("not-finalized", true);
    CHECK(!errors(c).empty());
    CHECK(selected(c)["target"] == "T" && selected(c)["dataset"] == "D");
    CHECK(selected(c)["left"] == "L" && selected(c)["right"] == "R");
    CHECK(selected(c)["rig"] == "G");
    {
        std::lock_guard lock(fake->mutex);
        fake->listGate = refresh;
        fake->datasetGate = operation;
        fake->failDataset = failOperation;
        fake->failList = failOperation;
    }
    c.refresh();
    until([&] { return refresh->entered.load(); });
    c.buildDataset(40);
    until([&] { return operation->entered.load(); });
    const auto before = c.state()["projectGeneration"].toString();
    fake->project("/fixture/B/project.mantis", 22);
    c.observeSnapshot(fake->snapshot());
    cleared(c);
    CHECK(c.state()["projectGeneration"] != before);
    // A stale dialog's IDs must not admit a command after project confirmation.
    c.activate("third-party", "G");
    CHECK(fake->activations == 1);
    if (returnToA) {
        fake->project("/fixture/A/project.mantis", 33);
        c.observeSnapshot(fake->snapshot());
        cleared(c);
    }
    expectedGeneration = c.state()["projectGeneration"].toString();
    c.buildDataset(40);
    CHECK(fake->datasetCalls == 2);
    CHECK(errors(c)["dataset"].toMap()["status"] == "busy");
    c.refresh(); // Coalesces an explicit current-generation read after old work retires.
    refresh->release();
    loaded(c, returnToA ? 33 : 22);
    CHECK(refresh->exited);
    CHECK(!stalePublication);
    CHECK(c.state()["active"].toMap().empty());
    CHECK(jobs(c).empty());
    for (const auto &v : c.artifacts())
        CHECK(v.toMap()["error"].toMap().empty());
    c.selectArtifact("dataset", "D"); // Clear B's own busy diagnostic through current-project input.
    c.selectArtifact("rig", "G");
    CHECK(c.state()["target"].toMap()["squareMm"].toInt() == (returnToA ? 33 : 22));
    CHECK(c.state()["rig"].toMap()["revision"].toInt() == (returnToA ? 33 : 22));
    const auto freshSelection = selected(c);
    const auto freshErrors = errors(c);
    const auto beforeCompletion = changes;
    operation->release(); // Old A operation is genuinely completed, but retired last.
    until([&] { return changes > beforeCompletion; });
    CHECK(operation->exited);
    CHECK(jobs(c).empty());
    CHECK(errors(c) == freshErrors);
    CHECK(selected(c) == freshSelection);
    CHECK(fake->datasetCalls == 2); // No automatic replay on B or the new A generation.
    CHECK(!stalePublication && !wrongThread);
    std::cout << "PASS: held refresh/" << (failOperation ? "failure" : "success")
              << " completion fenced across " << (returnToA ? "A->B->A" : "A->B") << '\n';
}
void refreshDiscoversProject() {
    auto fake = std::make_shared<Fake>();
    CalibrationController c(fake);
    c.refresh();
    loaded(c, 2);
    c.selectArtifact("rig", "G");
    c.setCaptureSelected("raw0", true);
    const auto before = fake->infoCalls.load();
    fake->project("/fixture/B/project.mantis", 22);
    c.refresh();
    loaded(c, 22);
    CHECK(fake->infoCalls > before);
    CHECK(selected(c)["rig"].toString().isEmpty());
    CHECK(c.state()["rawIds"].toStringList().empty());
    c.selectArtifact("rig", "G");
    CHECK(c.state()["target"].toMap()["squareMm"].toInt() == 22);
    CHECK(c.choices("target").size() == 2); // B's same-ID formerly corrupt artifact was inspected afresh.
    std::cout << "PASS: refresh-discovered identity rejects same-ID cached project-A info\n";
}
void disconnectAndLifetime() {
    auto fake = std::make_shared<Fake>();
    {
        CalibrationController c(fake);
        c.refresh();
        loaded(c, 2);
        c.selectArtifact("rig", "G");
        c.setCaptureSelected("raw0", true);
        const auto selection = selected(c);
        const auto generation = c.state()["projectGeneration"];
        {
            std::lock_guard lock(fake->mutex);
            fake->disconnected = true;
        }
        c.refresh();
        until([&] { return errors(c).contains("connection"); });
        CHECK(!c.state()["projectConfirmed"].toBool());
        CHECK(selected(c) == selection && c.state()["rawIds"].toStringList().size() == 1);
        CHECK(c.state()["projectGeneration"] == generation);
        c.buildDataset(40);
        CHECK(fake->datasetCalls == 0);
        CHECK(errors(c)["dataset"].toMap()["status"] == "incompatible");
        {
            std::lock_guard lock(fake->mutex);
            fake->disconnected = false;
        }
        c.refresh();
        until([&] { return c.state()["projectConfirmed"].toBool(); });
        CHECK(c.state()["projectGeneration"] == generation && selected(c) == selection);
        c.buildDataset(40);
        until([&] { return jobs(c).contains("dataset"); });
        CHECK(fake->datasetCalls == 1);
        CHECK(errors(c).empty());
        // Missing identity is unconfirmed authority, not a different project.
        c.observeSnapshot(w::Response{});
        CHECK(c.state()["projectGeneration"] == generation && selected(c) == selection);
        CHECK(!c.state()["projectConfirmed"].toBool());
    }
    auto refresh = std::make_shared<Gate>(), operation = std::make_shared<Gate>();
    QPointer<CalibrationController> lifetime;
    bool expectedFailure{};
    try {
        auto controller = std::make_unique<CalibrationController>(fake);
        lifetime = controller.get();
        ReleaseGates release{refresh, operation};
        controller->observeSnapshot(fake->snapshot());
        {
            std::lock_guard lock(fake->mutex);
            fake->listGate = refresh;
            fake->datasetGate = operation;
        }
        controller->refresh();
        controller->buildDataset(40);
        until([&] { return refresh->entered && operation->entered; });
        throw std::runtime_error("intentional held-worker assertion unwind");
    } catch (const std::runtime_error &e) {
        expectedFailure = std::string(e.what()) == "intentional held-worker assertion unwind";
    }
    CHECK(expectedFailure && lifetime.isNull());
    CHECK(refresh->exited && operation->exited);
    QCoreApplication::processEvents(); // Queued watcher notifications cannot access the dead controller.
    std::cout << "PASS: disconnect fail-closed admission and held-worker GUI lifetime cleanup\n";
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        projectGenerations(false, false);
        projectGenerations(true, false);
        projectGenerations(true, true);
        refreshDiscoversProject();
        disconnectAndLifetime();
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
