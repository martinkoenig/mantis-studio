#include "calibration_controller.hpp"
#include "studio_calibration_fixture.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QPointer>
#include <QThread>
#include <algorithm>
#include <functional>
#include <iostream>
#include <mutex>
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(std::string("studio_calibration.cpp:") + std::to_string(__LINE__) +     \
                                     ": " + #x);                                                             \
    } while (false)
namespace w = mantis::wire::v1;
void until(const QString &phase, const std::function<bool()> &condition,
           const std::function<QString()> &details) {
    QElapsedTimer timer;
    timer.start();
    while (!condition()) {
        QCoreApplication::processEvents();
        QThread::msleep(1);
        if (timer.elapsed() > 10000)
            throw std::runtime_error(
                QString("Presentation timeout: %1 (%2)").arg(phase, details()).toStdString());
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
QVariantMap jobIn(const QVariantMap &state, const QString &slot) {
    return state.value("jobs").toMap().value(slot).toMap();
}
QString describeJob(const QVariantMap &job) {
    return QString("id=%1 state=%2 diagnostics=%3 result=%4")
        .arg(job.value("id").toString().left(48), job.value("state").toString().left(24),
             job.value("diagnostics").toString().left(48), job.value("result").toString().left(48));
}
void waitFor(const CalibrationController &c, const QString &slot, const QString &phase,
             const std::function<bool()> &condition, const std::function<QString()> &extra = {}) {
    until(phase, condition, [&] {
        const auto state = c.state();
        return QString("generation=%1 confirmed=%2 pending=%3 error=%4 %5 %6")
            .arg(state.value("projectGeneration").toString())
            .arg(state.value("projectConfirmed").toBool())
            .arg(state.value("pending").toMap().value(slot).toBool())
            .arg(state.value("errors").toMap().value(slot).toMap().value("status").toString().left(32),
                 describeJob(jobIn(state, slot)), extra ? extra() : QString{});
    });
}
using namespace calibration_test;
std::string checkSubmittedJob(const CalibrationController &c, const Fake &fake, const char *slot) {
    const auto id = fake.submittedJob(slot);
    CHECK(!id.empty());
    CHECK(jobIn(c.state(), slot).value("id") == QString::fromStdString(id));
    return id;
}

// React to the actual Queued publication, before its automatic refresh or any other event can run.
// Both deliveries use the same snapshot; only the control copy lacks the predecessor's terminal row.
std::string submitWithPredecessor(CalibrationController &c, const Fake &fake, const char *slot,
                                  const std::string &predecessor, const char *terminal,
                                  const std::function<void()> &submit,
                                  const std::function<QString()> &requestDetails) {
    QVariantMap before, afterControl, afterStale;
    int predecessorRows{};
    std::string predecessorState;
    QObject observer;
    QObject::connect(
        &c, &CalibrationController::changed, &observer,
        [&] {
            if (!before.empty()) // observeSnapshot emits changed synchronously, too.
                return;
            const auto state = c.state();
            if (jobIn(state, slot).value("state") != "Queued")
                return;
            before = state;
            const auto snapshot = fake.snapshot();
            auto control = snapshot;
            auto *rows = control.mutable_jobs();
            for (int i = rows->size(); i-- > 0;)
                if (rows->Get(i).id() == predecessor) {
                    ++predecessorRows;
                    predecessorState = rows->Get(i).state();
                    rows->DeleteSubrange(i, 1);
                }
            c.observeSnapshot(control);
            afterControl = c.state();
            c.observeSnapshot(snapshot);
            afterStale = c.state();
        },
        Qt::DirectConnection);
    const QString phase = QString("%1 retry: publish Queued before stale %2 row").arg(slot, terminal);
    CHECK(jobIn(c.state(), slot).value("state") == terminal);
    submit();
    waitFor(c, slot, phase, [&] { return !before.empty(); }, requestDetails);

    const auto id = fake.submittedJob(slot);
    const auto queued = jobIn(before, slot);
    const auto control = jobIn(afterControl, slot);
    const auto stale = jobIn(afterStale, slot);
    CHECK(predecessorRows == 1 && predecessorState == terminal);
    CHECK(!id.empty() && queued.value("id") == QString::fromStdString(id));
    CHECK(queued.value("state") == "Queued");
    CHECK(before.value("projectConfirmed").toBool() && !before.value("pending").toMap().value(slot).toBool());
    CHECK(queued.value("diagnostics").toString().isEmpty() && queued.value("result").toString().isEmpty());
    const auto unchanged = [&](const QVariantMap &observed) {
        return jobIn(observed, slot) == queued && observed.value("selected") == before.value("selected") &&
               observed.value("errors") == before.value("errors");
    };
    CHECK(unchanged(afterControl));
    const auto describe = [&](const QVariantMap &state, const QVariantMap &job) {
        return describeJob(job) +
               " selected=" + state.value("selected").toMap().value(slot).toString().left(48);
    };
    const auto receipt =
        QString("%1: predecessor=%2; before [%3]; no-row [%4]; retained [%5]; %6")
            .arg(slot, QString::fromStdString(predecessor), describe(before, queued),
                 describe(afterControl, control), describe(afterStale, stale), requestDetails());
    if (!unchanged(afterStale))
        throw std::runtime_error(("Stale predecessor changed Queued presentation: " + receipt).toStdString());
    std::cout << "PASS: " << receipt.toStdString() << '\n';
    return id;
}

void loaded(CalibrationController &c, int revision) {
    waitFor(c, "connection", QString("load artifact revision %1").arg(revision), [&] {
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
    waitFor(c, "activation", "generation setup: rig G active",
            [&] { return c.state()["active"].toMap()["id"] == "G"; });
    c.startCapture();
    waitFor(c, "captures", "generation setup: capture started",
            [&] { return c.state()["captureId"] == "capture"; });
    c.buildDataset(40);
    waitFor(c, "dataset", "generation setup: dataset job published",
            [&] { return jobs(c).contains("dataset"); });
    fake->job(checkSubmittedJob(c, *fake, "dataset"), "Failed");
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
    waitFor(c, "connection", "old project refresh gate entered", [&] { return refresh->entered.load(); });
    c.buildDataset(40);
    waitFor(c, "dataset", "old project dataset gate entered", [&] { return operation->entered.load(); });
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
    waitFor(
        c, "dataset", "retire old project dataset request", [&] { return changes > beforeCompletion; },
        [&] { return QString("workerExited=%1").arg(operation->exited.load()); });
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
        waitFor(c, "connection", "disconnect error published",
                [&] { return errors(c).contains("connection"); });
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
        waitFor(c, "connection", "reconnect confirms project",
                [&] { return c.state()["projectConfirmed"].toBool(); });
        CHECK(c.state()["projectGeneration"] == generation && selected(c) == selection);
        c.buildDataset(40);
        waitFor(c, "dataset", "reconnect admits dataset job", [&] { return jobs(c).contains("dataset"); });
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
        waitFor(
            *controller, "dataset", "lifetime: both worker gates entered",
            [&] { return refresh->entered && operation->entered; },
            [&] {
                return QString("refreshEntered=%1 operationEntered=%2")
                    .arg(refresh->entered.load())
                    .arg(operation->entered.load());
            });
        throw std::runtime_error("intentional held-worker assertion unwind");
    } catch (const std::runtime_error &e) {
        if (std::string(e.what()) != "intentional held-worker assertion unwind")
            throw;
        expectedFailure = true;
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
        waitFor(
            c, "connection", "initial calibration inventory has 8 entries",
            [&] { return c.artifacts().size() == 8; },
            [&] { return QString("artifacts=%1").arg(c.artifacts().size()); });
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
        waitFor(
            c, "target", "created target selected", [&] { return selected(c)["target"] == "new-target"; },
            [&] { return QString("target=%1").arg(selected(c)["target"].toString().left(48)); });
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
        waitFor(
            c, "dataset", "first dataset job published", [&] { return jobs(c).contains("dataset"); },
            [&] { return QString("datasetCalls=%1").arg(fake->datasetCalls.load()); });
        const auto firstDataset = checkSubmittedJob(c, *fake, "dataset");
        {
            std::lock_guard lock(fake->mutex);
            CHECK(fake->built.target_artifact_id() == "T");
            CHECK(fake->built.raw_capture_artifact_ids_size() == 1024);
            CHECK(fake->built.camera_roles(0) == "left" && fake->built.camera_roles(1) == "right");
            CHECK(fake->datasetCalls == 1);
        }
        fake->job(firstDataset, "Running", {}, .42);
        c.observeSnapshot(fake->snapshot());
        CHECK(jobs(c)["dataset"].toMap()["progress"].toDouble() == .42);
        fake->job(firstDataset, "Completed", "D", 1);
        c.observeSnapshot(fake->snapshot());
        CHECK(jobs(c)["dataset"].toMap()["state"] == "Completed");
        CHECK(jobs(c)["dataset"].toMap()["result"] == "D");
        CHECK(selected(c)["dataset"] == "D");
        CHECK(c.choices("left").size() == 1);
        c.solveBoth(3);
        waitFor(
            c, "right", "both independent camera jobs published",
            [&] { return jobs(c).contains("left") && jobs(c).contains("right"); },
            [&] { return QString("left: %1").arg(describeJob(jobs(c)["left"].toMap())); });
        const auto leftJob = checkSubmittedJob(c, *fake, "left");
        const auto firstRight = checkSubmittedJob(c, *fake, "right");
        {
            std::lock_guard lock(fake->mutex);
            for (auto role : {"left", "right"}) {
                const auto &request = fake->solvedCameras.at(role);
                CHECK(request.camera_role() == role && request.dataset_artifact_id() == "D");
                CHECK(request.heldout_per_camera() == 3);
            }
        }
        fake->job(leftJob, "Completed", "L", 1);
        fake->job(firstRight, "Failed");
        c.observeSnapshot(fake->snapshot());
        CHECK(selected(c)["left"] == "L");
        CHECK(selected(c)["right"].toString().isEmpty());
        CHECK(jobs(c)["right"].toMap()["state"] == "Failed");
        CHECK(jobs(c)["right"].toMap()["result"].toString().isEmpty());
        CHECK(jobs(c)["right"].toMap()["diagnostics"] == "useful diagnostics");
        const auto rightRetry = submitWithPredecessor(
            c, *fake, "right", firstRight, "Failed", [&] { c.solveCamera("right", 4); },
            [&] {
                std::lock_guard lock(fake->mutex);
                return QString("right heldout=%1").arg(fake->solvedCameras.at("right").heldout_per_camera());
            });
        {
            std::lock_guard lock(fake->mutex);
            const auto &retry = fake->solvedCameras.at("right");
            CHECK(retry.camera_role() == "right" && retry.dataset_artifact_id() == "D");
            CHECK(retry.heldout_per_camera() == 4);
            CHECK(fake->solvedCameras.at("left").heldout_per_camera() == 3);
        }
        fake->job(rightRetry, "Running");
        c.observeSnapshot(fake->snapshot());
        CHECK(checkSubmittedJob(c, *fake, "right") == rightRetry);
        CHECK(jobs(c)["right"].toMap()["state"] == "Running");
        c.cancelJob("right");
        const auto cancelled = [&] {
            std::lock_guard lock(fake->mutex);
            return fake->cancelled;
        };
        waitFor(
            c, "cancel", "cancel targets the right retry", [&] { return cancelled() == rightRetry; },
            [&] {
                return QString("expected=%1 actual=%2")
                    .arg(QString::fromStdString(rightRetry), QString::fromStdString(cancelled()).left(48));
            });
        CHECK(cancelled() == rightRetry && cancelled() != firstRight);
        fake->job(rightRetry, "Cancelled");
        c.observeSnapshot(fake->snapshot());
        CHECK(jobs(c)["right"].toMap()["state"] == "Cancelled");
        CHECK(selected(c)["left"] == "L");
        CHECK(selected(c)["dataset"] == "D");
        c.selectArtifact("right", "R");
        c.solveRig(3, "explicit.frame", "Generic rig");
        waitFor(c, "rig", "rig job published", [&] { return jobs(c).contains("rig"); });
        const auto rigJob = checkSubmittedJob(c, *fake, "rig");
        {
            std::lock_guard lock(fake->mutex);
            CHECK(fake->solvedRig.left_camera_artifact_id() == "L");
            CHECK(fake->solvedRig.right_camera_artifact_id() == "R");
            CHECK(fake->solvedRig.dataset_artifact_id() == "D");
            CHECK(fake->solvedRig.rig_frame_id() == "explicit.frame");
        }
        fake->job(rigJob, "Completed", "G");
        c.observeSnapshot(fake->snapshot());
        CHECK(selected(c)["rig"] == "G");
        CHECK(fake->activations == 0);
        c.selectDevice("third-party");
        waitFor(c, "activation", "device selected with no pending activation",
                [&] { return !c.state()["pending"].toMap()["activation"].toBool(); });
        CHECK(c.state()["rigFrameId"].toString().isEmpty());
        c.activate("stale", "G");
        CHECK(fake->activations == 0);
        CHECK(errors(c)["activation"].toMap()["status"] == "invalid_argument");
        c.activate("third-party", "G");
        waitFor(
            c, "activation", "rig G bound once",
            [&] { return fake->activations == 1 && c.state()["active"].toMap()["id"] == "G"; },
            [&] {
                return QString("activations=%1 active=%2")
                    .arg(fake->activations.load())
                    .arg(c.state()["active"].toMap()["id"].toString().left(48));
            });
        CHECK(c.state()["active"].toMap()["revision"].toULongLong() == 2);
        {
            std::lock_guard lock(fake->mutex);
            fake->rejectActivation = true;
        }
        c.activate("third-party", "G");
        waitFor(c, "activation", "incompatible activation reported",
                [&] { return errors(c)["activation"].toMap()["status"] == "incompatible"; });
        CHECK(errors(c)["activation"].toMap()["component"] == "calibration.activation");
        CHECK(errors(c)["activation"].toMap()["message"].toString().contains("geometry"));
        c.clearActive("third-party");
        waitFor(
            c, "activation", "active rig binding cleared",
            [&] { return c.state()["active"].toMap().isEmpty(); },
            [&] { return QString("active=%1").arg(c.state()["active"].toMap()["id"].toString().left(48)); });
        CHECK(selected(c)["rig"] == "G");
        c.selectArtifact("target", "T");
        c.selectArtifact("rig", "G");
        CHECK(selected(c)["dataset"] == "D" && selected(c)["left"] == "L" && selected(c)["right"] == "R");
        c.startCapture();
        waitFor(
            c, "captures", "capture started before finalization",
            [&] { return c.state()["captureId"] == "capture"; },
            [&] { return QString("capture=%1").arg(c.state()["captureId"].toString().left(48)); });
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
        // Clear D while keeping T as the next request's input, so stale auto-binding is observable.
        c.selectArtifact("target", "new-target");
        c.selectArtifact("target", "T");
        CHECK(selected(c)["target"] == "T" && selected(c)["dataset"].toString().isEmpty());
        // A result for superseded inputs stays available without changing new selections.
        const auto secondDataset = submitWithPredecessor(
            c, *fake, "dataset", firstDataset, "Completed", [&] { c.buildDataset(40); },
            [&] { return QString("datasetCalls=%1").arg(fake->datasetCalls.load()); });
        {
            std::lock_guard lock(fake->mutex);
            CHECK(fake->datasetCalls == 2 && fake->built.target_artifact_id() == "T");
            const auto &raw = fake->built.raw_capture_artifact_ids();
            CHECK(std::find(raw.begin(), raw.end(), "not-finalized") != raw.end());
            CHECK(std::find(raw.begin(), raw.end(), "raw0") == raw.end());
        }
        c.selectArtifact("target", "new-target");
        CHECK(selected(c)["target"] == "new-target" && selected(c)["dataset"].toString().isEmpty());
        const auto currentSelection = selected(c);
        {
            std::lock_guard lock(fake->mutex);
            auto result = calibration_test::info("D2", "CalibrationDataset");
            result.mutable_dataset_info()->mutable_target()->set_id("T");
            fake->infos["D2"] = std::move(result);
        }
        fake->job(secondDataset, "Completed", "D2", 1);
        c.observeSnapshot(fake->snapshot());
        CHECK(checkSubmittedJob(c, *fake, "dataset") == secondDataset);
        CHECK(jobs(c)["dataset"].toMap()["state"] == "Completed");
        CHECK(jobs(c)["dataset"].toMap()["result"] == "D2");
        CHECK(selected(c) == currentSelection);
        c.refresh();
        waitFor(c, "dataset", "superseded dataset D2 available in choices", [&] {
            const auto choices = c.choices("dataset");
            return std::any_of(choices.begin(), choices.end(),
                               [](const auto &choice) { return choice.toMap()["id"] == "D2"; });
        });
        CHECK(jobs(c)["dataset"].toMap()["result"] == "D2");
        CHECK(selected(c) == currentSelection);
        CHECK(selected(c)["dataset"].toString().isEmpty());
        CHECK(!c.choices("dataset").empty());
        std::cout << "Studio calibration presentation contracts passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
