#include "calibration_controller.hpp"
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <cmath>
#include <utility>

namespace {
namespace w = mantis::wire::v1;
QString q(const std::string &s) {
    return QString::fromStdString(s);
}
QVariantMap failure(const mantis::Error &e) {
    static const QStringList names{
        "ok",   "invalid_argument", "not_found",   "incompatible", "cancelled", "io",
        "busy", "plugin_failed",    "unsupported", "corrupt"};
    auto code = static_cast<int>(e.code);
    return {{"component", q(e.component)},
            {"status", code >= 0 && code < names.size() ? names[code] : QString::number(code)},
            {"message", q(e.message)}};
}
QVariantMap entry(const w::CalibrationEntry &e) {
    return {{"id", q(e.artifact().id())},
            {"logicalId", q(e.reference().id())},
            {"revision", QString::number(e.reference().revision())},
            {"type", q(e.artifact().type())},
            {"state", q(e.artifact().state())},
            {"label", QString("%1 · r%2 · %3 · %4")
                          .arg(q(e.reference().id()).left(12))
                          .arg(e.reference().revision())
                          .arg(q(e.artifact().id()).left(12), q(e.artifact().state()))}};
}
QVariantMap residual(const w::ResidualSummary &r) {
    return {{"rms", r.rms_px()},
            {"median", r.median_px()},
            {"p95", r.p95_px()},
            {"max", r.max_px()},
            {"points", QVariant::fromValue(r.point_count())}};
}
QVariantMap coverage(const w::CoverageEvidence &c) {
    return {{"minX", c.min_x()},
            {"minY", c.min_y()},
            {"maxX", c.max_x()},
            {"maxY", c.max_y()},
            {"area", c.bounding_box_area()}};
}
QVariantMap mono(const w::MonoStageInfo &s) {
    auto m = residual(s.residuals());
    m["count"] = QVariant::fromValue(s.sample_count());
    m["coverage"] = coverage(s.coverage());
    if (s.has_opencv_solver_rms_px())
        m["opencvRms"] = s.opencv_solver_rms_px();
    return m;
}
QVariantMap stereo(const w::StereoStageInfo &s) {
    auto m = residual(s.residuals());
    m["count"] = QVariant::fromValue(s.pair_count());
    if (s.has_opencv_solver_rms_px())
        m["opencvRms"] = s.opencv_solver_rms_px();
    return m;
}
QVariantMap camera(const w::CalibrationCamera &c) {
    return {{"role", q(c.role())},
            {"identity", q(c.physical_camera_id())},
            {"width", c.image_width()},
            {"height", c.image_height()}};
}
QVariantMap implementation(const w::CalibrationSolverImplementation &s) {
    return {
        {"opencv", q(s.opencv_version())}, {"mantis", q(s.mantis_version())}, {"build", q(s.mantis_build())}};
}
QString transform(const w::Transform &t) {
    QString out = q(t.target().id()) + " ← " + q(t.source().id()) + "\n";
    for (int i = 0; i < t.matrix_size(); ++i)
        out += QString::number(t.matrix(i), 'g', 10) + (i % 4 == 3 ? "\n" : "  ");
    return out;
}
bool hasCapability(const w::Device &d, const char *cap) {
    return std::find(d.capabilities().begin(), d.capabilities().end(), cap) != d.capabilities().end();
}
bool calibrationCapable(const w::Device &d, const w::Response &s) {
    if (!hasCapability(d, "org.mantis.camera.frameset-stream.v1"))
        return false;
    return std::any_of(s.devices().begin(), s.devices().end(), [&](const auto &child) {
        return child.parent() == d.id() && hasCapability(child, "org.mantis.camera.image-stream.v1");
    });
}
uint32_t positive(int n) {
    if (n <= 0)
        mantis::fail(mantis::Status::invalid_argument, "Enter a positive sample count", "studio.calibration");
    return static_cast<uint32_t>(n);
}
double number(const QVariantMap &m, const QString &key) {
    bool ok{};
    auto value = m.value(key).toDouble(&ok);
    if (!ok || !std::isfinite(value))
        mantis::fail(mantis::Status::invalid_argument, "Enter a finite number for " + key.toStdString(),
                     "studio.calibration.target");
    return value;
}
} // namespace
CalibrationController::CalibrationController(QObject *p)
    : CalibrationController(std::make_shared<PublicCalibrationClient>(), p) {}
CalibrationController::CalibrationController(std::shared_ptr<const CalibrationClient> c, QObject *p)
    : QObject(p), client_(std::move(c)) {
    connect(&refresh_, &QFutureWatcher<Reply>::finished, this, [this] {
        const auto r = refresh_.result();
        if (!r.error.isEmpty()) {
            errors_["connection"] = r.error;
            emit changed();
            if (std::exchange(refreshAgain_, false))
                refresh();
            return;
        }
        errors_.remove("connection");
        entries_ = r.entries;
        infos_ = r.infos;
        inspectionErrors_ = r.inspectionErrors;
        if (r.device == device_) {
            active_.clear();
            if (r.active)
                active_ = {{"device", q(r.active->logical_device_id())},
                           {"id", q(r.active->artifact().id())},
                           {"logicalId", q(r.active->reference().id())},
                           {"revision", QString::number(r.active->reference().revision())}};
        }
        observeSnapshot(r.snapshot);
        bindResults();
        emit changed();
        if (std::exchange(refreshAgain_, false))
            refresh();
    });
    timer_.setInterval(2000);
    connect(&timer_, &QTimer::timeout, this, &CalibrationController::refresh);
}
CalibrationController::~CalibrationController() {
    timer_.stop();
    refresh_.waitForFinished();
    for (auto &[slot, watcher] : requests_) {
        (void)slot;
        watcher->waitForFinished();
    }
}
void CalibrationController::setVisible(bool v) {
    if (visible_ == v)
        return;
    visible_ = v;
    if (v) {
        timer_.start();
        refresh();
    } else
        timer_.stop();
    emit changed();
}
void CalibrationController::setStage(int s) {
    if (s >= 0 && s < stages().size()) {
        stage_ = s;
        emit changed();
    }
}
bool CalibrationController::pending(const QString &s) const {
    auto it = requests_.find(s);
    return it != requests_.end() && it->second->isRunning();
}
bool CalibrationController::running(const QString &s) const {
    auto state = jobs_.value(s).display.value("state").toString();
    return state == "Queued" || state == "Running";
}
void CalibrationController::error(const QString &slot, const mantis::Error &e) {
    errors_[slot] = failure(e);
    emit changed();
}
void CalibrationController::request(const QString &slot, std::function<Reply(const CalibrationClient &)> work,
                                    std::function<void(const Reply &)> done) {
    if (pending(slot))
        return;
    auto &watcher = requests_[slot];
    if (!watcher)
        watcher = std::make_unique<QFutureWatcher<Reply>>();
    disconnect(watcher.get(), nullptr, this, nullptr);
    connect(watcher.get(), &QFutureWatcher<Reply>::finished, this, [this, slot, done, ptr = watcher.get()] {
        auto r = ptr->result();
        if (!r.error.isEmpty())
            errors_[slot] = r.error;
        else {
            errors_.remove(slot);
            done(r);
        }
        emit changed();
        refresh();
    });
    auto c = client_;
    watcher->setFuture(QtConcurrent::run([c, work] {
        Reply r;
        try {
            r = work(*c);
        } catch (const mantis::Failure &e) {
            r.error = failure(e.error);
        } catch (const std::exception &e) {
            r.error = failure({mantis::Status::io, e.what(), "daemon.connection"});
        }
        return r;
    }));
    errors_.remove(slot);
    emit changed();
}
void CalibrationController::refresh() {
    if (refresh_.isRunning()) {
        refreshAgain_ = true;
        return;
    }
    auto c = client_;
    auto known = infos_;
    auto d = device_;
    refresh_.setFuture(QtConcurrent::run([c, known, d] {
        Reply r;
        r.device = d;
        try {
            r.snapshot = c->snapshot();
            r.entries = c->list();
            for (const auto &e : r.entries) {
                auto id = q(e.artifact().id());
                if (e.artifact().state() != "FINALIZED")
                    continue;
                if (known.contains(id))
                    r.infos[id] = known[id];
                else {
                    try {
                        r.infos[id] = c->info(e.artifact().id());
                    } catch (const mantis::Failure &issue) {
                        if (issue.error.code != mantis::Status::corrupt &&
                            issue.error.code != mantis::Status::incompatible &&
                            issue.error.code != mantis::Status::not_found)
                            throw;
                        r.inspectionErrors[id] = failure(issue.error);
                    }
                }
            }
            if (!d.isEmpty())
                r.active = c->active(d.toStdString());
        } catch (const mantis::Failure &e) {
            r.error = failure(e.error);
        } catch (const std::exception &e) {
            r.error = failure({mantis::Status::io, e.what(), "daemon.connection"});
        }
        return r;
    }));
}
void CalibrationController::observeSnapshot(const w::Response &s) {
    snapshot_ = s;
    for (auto it = jobs_.begin(); it != jobs_.end(); ++it) {
        for (const auto &j : s.jobs())
            if (q(j.id()) == it->id) {
                it->display = {{"id", q(j.id())},
                               {"state", q(j.state())},
                               {"progress", j.progress()},
                               {"status", q(j.status())},
                               {"diagnostics", q(j.diagnostics())},
                               {"result", q(j.result_artifact())}};
            }
    }
    bindResults();
    emit changed();
}
void CalibrationController::bindResults() {
    for (auto it = jobs_.begin(); it != jobs_.end(); ++it) {
        if (it->bound || it->display.value("state") != "Completed")
            continue;
        auto id = it->display.value("result").toString();
        if (!infos_.contains(id))
            continue;
        it->bound = true;
        bool matches = it.key() == "dataset" ? selected_.value("target") == it->input
                                             : selected_.value("dataset") == it->input;
        if (it.key() == "rig")
            matches = matches && selected_.value("left") == it->left && selected_.value("right") == it->right;
        if (matches)
            select(it.key(), id);
    }
}
void CalibrationController::selectDevice(QString id) {
    device_ = id;
    capture_.clear();
    active_.clear();
    emit changed();
    refresh();
}
void CalibrationController::select(const QString &slot, const QString &id) {
    if (!infos_.contains(id))
        return;
    const auto &i = infos_[id];
    if (slot == "target" && i.has_target_info()) {
        if (selected_.value(slot) != id) {
            selected_.remove("dataset");
            selected_.remove("left");
            selected_.remove("right");
            selected_.remove("rig");
        }
        selected_[slot] = id;
    }
    if (slot == "dataset" && i.has_dataset_info()) {
        if (selected_.value(slot) != id) {
            selected_.remove("left");
            selected_.remove("right");
            selected_.remove("rig");
        }
        selected_[slot] = id;
        selected_["target"] = q(i.dataset_info().target().id());
    }
    if ((slot == "left" || slot == "right") && i.has_camera_info() &&
        q(i.camera_info().dataset().id()) == selected_.value("dataset") &&
        q(i.camera_info().camera().role()) == slot) {
        if (selected_.value(slot) != id)
            selected_.remove("rig");
        selected_[slot] = id;
    }
    if (slot == "rig" && i.has_rig_info()) {
        selected_["dataset"] = q(i.rig_info().dataset().id());
        selected_["target"] = q(i.rig_info().target().id());
        selected_["left"] = q(i.rig_info().left_camera().id());
        selected_["right"] = q(i.rig_info().right_camera().id());
        selected_[slot] = id;
    }
}
void CalibrationController::selectArtifact(QString slot, QString id) {
    if (!infos_.contains(id)) {
        error(slot, {mantis::Status::incompatible, "Select a finalized calibration artifact",
                     "studio.calibration"});
        return;
    }
    select(slot, id);
    if (selected_.value(slot) != id)
        error(slot, {mantis::Status::incompatible,
                     "Artifact does not match the selected Dataset and explicit camera role",
                     "studio.calibration"});
    else
        errors_.remove(slot);
    emit changed();
}
QVariantList CalibrationController::artifacts() const {
    QVariantList out;
    for (const auto &e : entries_) {
        auto m = entry(e);
        m["error"] = inspectionErrors_.value(q(e.artifact().id())).toMap();
        out.push_back(m);
    }
    return out;
}
QVariantList CalibrationController::choices(QString slot) const {
    QVariantList out;
    for (const auto &e : entries_) {
        auto id = q(e.artifact().id());
        if (!infos_.contains(id))
            continue;
        const auto &i = infos_[id];
        bool match = (slot == "target" && i.has_target_info()) ||
                     (slot == "dataset" && i.has_dataset_info()) || (slot == "rig" && i.has_rig_info());
        if (slot == "left" || slot == "right")
            match = i.has_camera_info() && q(i.camera_info().dataset().id()) == selected_.value("dataset") &&
                    q(i.camera_info().camera().role()) == slot;
        if (match)
            out.push_back(entry(e));
    }
    return out;
}
QVariantList CalibrationController::devices() const {
    QVariantList out;
    for (const auto &d : snapshot_.devices()) {
        if (!d.parent().empty() || !calibrationCapable(d, snapshot_))
            continue;
        QStringList caps;
        for (const auto &c : d.capabilities())
            caps.push_back(q(c));
        out.push_back(
            QVariantMap{{"id", q(d.id())},
                        {"name", q(d.name())},
                        {"capabilities", caps},
                        {"status", errors_.contains("connection") ? "Runtime unavailable" : "Discovered"},
                        {"x1", d.plugin_id() == "org.mantis.x1"}});
    }
    return out;
}
QVariantList CalibrationController::captures() const {
    QVariantList out;
    for (const auto &a : snapshot_.artifacts())
        if (a.type() == "org.mantis.RawCapture")
            out.push_back(QVariantMap{{"id", q(a.id())},
                                      {"state", q(a.state())},
                                      {"schema", a.schema_version()},
                                      {"ready", a.state() == "FINALIZED" && a.schema_version() == 2},
                                      {"selected", raw_.contains(q(a.id()))}});
    return out;
}
void CalibrationController::setCaptureSelected(QString id, bool yes) {
    if (!yes) {
        raw_.removeAll(id);
        errors_.remove("captures");
        emit changed();
        return;
    }
    if (raw_.contains(id))
        return;
    if (raw_.size() >= 1024) {
        error("captures", {mantis::Status::invalid_argument, "At most 1024 RawCaptures can be selected",
                           "studio.calibration"});
        return;
    }
    for (const auto &a : snapshot_.artifacts())
        if (q(a.id()) == id && a.type() == "org.mantis.RawCapture" && a.schema_version() == 2 &&
            a.state() == "FINALIZED") {
            raw_.push_back(id);
            errors_.remove("captures");
            emit changed();
            return;
        }
    error("captures", {mantis::Status::incompatible, "RawCapture must be finalized schema 2 before selection",
                       "studio.calibration"});
}
w::CalibrationTargetSpecification CalibrationController::targetSpecification(const QVariantMap &m) {
    w::CalibrationTargetSpecification t;
    for (auto key : {QString("squaresX"), QString("squaresY")}) {
        const auto n = number(m, key);
        if (n <= 0 || n > UINT32_MAX || std::floor(n) != n)
            mantis::fail(mantis::Status::invalid_argument, "Square counts must be positive integers",
                         "studio.calibration.target");
        if (key == "squaresX")
            t.set_squares_x(static_cast<uint32_t>(n));
        else
            t.set_squares_y(static_cast<uint32_t>(n));
    }
    t.set_nominal_square_size_mm(number(m, "squareMm"));
    if (m.value("pattern").toString() == "checkerboard")
        t.mutable_checkerboard();
    else if (m.value("pattern").toString() == "charuco") {
        auto *c = t.mutable_charuco();
        c->set_dictionary(m.value("dictionary").toString().toStdString());
        c->set_nominal_marker_size_mm(number(m, "markerMm"));
        c->set_pattern_layout(m.value("layout").toString().toStdString());
    } else
        mantis::fail(mantis::Status::invalid_argument, "Choose ChArUco or Checkerboard",
                     "studio.calibration.target");
    if (m.contains("measuredWidth"))
        t.mutable_measurement()->set_active_width_mm(number(m, "measuredWidth"));
    if (m.contains("measuredHeight"))
        t.mutable_measurement()->set_active_height_mm(number(m, "measuredHeight"));
    if (m.value("provenancePresent").toBool()) {
        auto *p = t.mutable_measurement()->mutable_provenance();
        if (m.contains("widthUncertainty"))
            p->set_width_uncertainty_mm(number(m, "widthUncertainty"));
        if (m.contains("heightUncertainty"))
            p->set_height_uncertainty_mm(number(m, "heightUncertainty"));
        if (m.contains("instrument"))
            p->set_instrument(m.value("instrument").toString().toStdString());
        if (m.contains("note"))
            p->set_note(m.value("note").toString().toStdString());
    }
    return t;
}
void CalibrationController::createTarget(QVariantMap form) {
    try {
        auto spec = targetSpecification(form);
        request(
            "target",
            [spec](const auto &c) {
                Reply r;
                auto e = c.create(spec);
                r.id = q(e.artifact().id());
                r.infos[r.id] = c.info(e.artifact().id());
                r.entries.push_back(e);
                return r;
            },
            [this](const Reply &r) {
                infos_.insert(r.id, r.infos[r.id]);
                entries_.push_back(r.entries.front());
                select("target", r.id);
            });
    } catch (const mantis::Failure &e) {
        error("target", e.error);
    }
}
void CalibrationController::startCapture() {
    auto d = device_;
    if (d.isEmpty()) {
        error("captures",
              {mantis::Status::invalid_argument, "Select a device to record", "studio.calibration"});
        return;
    }
    request(
        "captures",
        [d](const auto &c) {
            Reply r;
            r.id = q(c.start(d.toStdString()));
            return r;
        },
        [this](const Reply &r) { capture_ = r.id; });
}
void CalibrationController::stopCapture() {
    auto id = state().value("captureId").toString();
    if (id.isEmpty())
        return;
    request(
        "captures",
        [id](const auto &c) {
            c.stop(id.toStdString());
            return Reply{};
        },
        [](const Reply &) {});
}
void CalibrationController::buildDataset(int maxSelected) {
    if (running("dataset"))
        return;
    try {
        w::CalibrationDatasetBuild r;
        auto target = selected_.value("target");
        r.set_target_artifact_id(target.toStdString());
        r.set_max_selected_per_camera(positive(maxSelected));
        for (const auto &id : raw_)
            r.add_raw_capture_artifact_ids(id.toStdString());
        r.add_camera_roles("left");
        r.add_camera_roles("right");
        request(
            "dataset",
            [r](const auto &c) {
                Reply out;
                out.id = q(c.dataset(r));
                return out;
            },
            [this, target](const Reply &out) {
                jobs_["dataset"] = {
                    out.id, target, {}, {}, {{"id", out.id}, {"state", "Queued"}, {"progress", 0.0}}, false};
            });
    } catch (const mantis::Failure &e) {
        error("dataset", e.error);
    }
}
void CalibrationController::solveCamera(QString role, int heldout) {
    if ((role != "left" && role != "right") || running(role))
        return;
    try {
        w::CalibrationCameraSolve r;
        auto dataset = selected_.value("dataset");
        r.set_dataset_artifact_id(dataset.toStdString());
        r.set_camera_role(role.toStdString());
        r.set_heldout_per_camera(positive(heldout));
        request(
            role,
            [r](const auto &c) {
                Reply out;
                out.id = q(c.camera(r));
                return out;
            },
            [this, role, dataset](const Reply &out) {
                jobs_[role] = {
                    out.id, dataset, {}, {}, {{"id", out.id}, {"state", "Queued"}, {"progress", 0.0}}, false};
            });
    } catch (const mantis::Failure &e) {
        error(role, e.error);
    }
}
void CalibrationController::solveBoth(int heldout) {
    solveCamera("left", heldout);
    solveCamera("right", heldout);
}
void CalibrationController::solveRig(int heldout, QString frameId, QString frameName) {
    if (running("rig"))
        return;
    try {
        w::CalibrationRigSolve r;
        auto dataset = selected_.value("dataset"), left = selected_.value("left"),
             right = selected_.value("right");
        r.set_dataset_artifact_id(dataset.toStdString());
        r.set_left_camera_artifact_id(left.toStdString());
        r.set_right_camera_artifact_id(right.toStdString());
        r.set_heldout_pairs(positive(heldout));
        r.set_rig_frame_id(frameId.toStdString());
        r.set_rig_frame_name(frameName.toStdString());
        request(
            "rig",
            [r](const auto &c) {
                Reply out;
                out.id = q(c.rig(r));
                return out;
            },
            [this, dataset, left, right](const Reply &out) {
                jobs_["rig"] = {
                    out.id, dataset, left, right, {{"id", out.id}, {"state", "Queued"}, {"progress", 0.0}},
                    false};
            });
    } catch (const mantis::Failure &e) {
        error("rig", e.error);
    }
}
void CalibrationController::activate(QString d, QString r) {
    if (d.isEmpty() || d != device_ || r.isEmpty() || r != selected_.value("rig")) {
        error("activation",
              {mantis::Status::invalid_argument,
               "Device or Rig selection changed; review the exact revision again", "studio.calibration"});
        return;
    }
    request(
        "activation",
        [d, r](const auto &c) {
            c.activate(d.toStdString(), r.toStdString());
            return Reply{};
        },
        [](const Reply &) {});
}
void CalibrationController::clearActive(QString d) {
    if (d.isEmpty() || d != device_)
        return;
    request(
        "activation",
        [d](const auto &c) {
            c.clear(d.toStdString());
            return Reply{};
        },
        [](const Reply &) {});
}
void CalibrationController::cancelJob(QString slot) {
    auto id = jobs_.value(slot).id;
    if (id.isEmpty() || !running(slot))
        return;
    request(
        "cancel",
        [id](const auto &c) {
            c.cancel(id.toStdString());
            return Reply{};
        },
        [](const Reply &) {});
}
QVariantMap CalibrationController::evidence(const QString &slot) const {
    auto id = selected_.value(slot);
    if (!infos_.contains(id))
        return {};
    const auto &i = infos_[id];
    auto out = entry(i.entry());
    if (i.has_target_info()) {
        const auto &t = i.target_info().target();
        out["pattern"] = t.has_charuco() ? "charuco" : "checkerboard";
        out["squaresX"] = t.squares_x();
        out["squaresY"] = t.squares_y();
        out["squareMm"] = t.nominal_square_size_mm();
        if (t.has_charuco()) {
            out["dictionary"] = q(t.charuco().dictionary());
            out["layout"] = q(t.charuco().pattern_layout());
            out["markerMm"] = t.charuco().nominal_marker_size_mm();
        }
        if (t.measurement().has_active_width_mm())
            out["measuredWidth"] = t.measurement().active_width_mm();
        if (t.measurement().has_active_height_mm())
            out["measuredHeight"] = t.measurement().active_height_mm();
        out["provenancePresent"] = t.measurement().has_provenance();
        const auto &p = t.measurement().provenance();
        if (p.has_width_uncertainty_mm())
            out["widthUncertainty"] = p.width_uncertainty_mm();
        if (p.has_height_uncertainty_mm())
            out["heightUncertainty"] = p.height_uncertainty_mm();
        if (p.has_instrument())
            out["instrument"] = q(p.instrument());
        if (p.has_note())
            out["note"] = q(p.note());
    }
    if (i.has_dataset_info()) {
        const auto &d = i.dataset_info();
        out["target"] = q(d.target().id());
        out["records"] = QVariant::fromValue(d.total_records());
        out["rawCount"] = QVariant::fromValue(d.raw_capture_count());
        out["policy"] = d.selection_policy_version();
        QVariantList cs;
        for (const auto &c : d.cameras()) {
            auto m = camera(c.camera());
            m["analyzed"] = QVariant::fromValue(c.analyzed());
            m["detected"] = QVariant::fromValue(c.detected());
            m["noTarget"] = QVariant::fromValue(c.no_target());
            m["selected"] = QVariant::fromValue(c.selected());
            cs.push_back(m);
        }
        out["cameras"] = cs;
    }
    if (i.has_camera_info()) {
        const auto &c = i.camera_info();
        out["camera"] = camera(c.camera());
        out["dataset"] = q(c.dataset().id());
        out["target"] = q(c.target().id());
        out["training"] = mono(c.training());
        out["heldout"] = mono(c.heldout());
        out["final"] = mono(c.final());
        out["implementation"] = implementation(c.implementation());
    }
    if (i.has_rig_info()) {
        const auto &r = i.rig_info();
        out["dataset"] = q(r.dataset().id());
        out["target"] = q(r.target().id());
        out["left"] = q(r.left_camera().id());
        out["right"] = q(r.right_camera().id());
        out["baseline"] = r.baseline_mm();
        out["angleRad"] = r.relative_rotation_angle_rad();
        out["training"] = stereo(r.training());
        out["heldout"] = stereo(r.heldout());
        out["final"] = stereo(r.final());
        out["transforms"] = QString("T_right_from_left\n%1\nT_rig_from_left\n%2\nT_rig_from_right\n%3")
                                .arg(transform(r.t_right_from_left()), transform(r.t_rig_from_left()),
                                     transform(r.t_rig_from_right()));
        out["implementation"] = implementation(r.implementation());
    }
    return out;
}
QVariantMap CalibrationController::state() const {
    QVariantMap out{{"deviceId", device_},
                    {"active", active_},
                    {"errors", errors_},
                    {"rawIds", raw_},
                    {"captureId", capture_}};
    auto dev = QVariantMap{{"id", device_},
                           {"name", device_.isEmpty() ? "Offline calibration" : device_},
                           {"status", "Not currently discovered"},
                           {"capabilities", QStringList{}},
                           {"x1", false}};
    for (const auto &v : devices())
        if (v.toMap().value("id") == device_)
            dev = v.toMap();
    out["device"] = dev;
    out["rigFrameId"] = dev.value("x1").toBool() ? "org.mantis.x1.rig" : "";
    out["rigFrameName"] = dev.value("x1").toBool() ? "Mantis X1 rig" : "";
    QVariantMap selected, pendingMap, jobs;
    for (const auto &slot : {"target", "dataset", "left", "right", "rig"}) {
        selected[slot] = selected_.value(slot);
        out[slot] = evidence(slot);
    }
    for (const auto &slot : {"target", "captures", "dataset", "left", "right", "rig", "activation", "cancel"})
        pendingMap[slot] = pending(slot);
    for (auto it = jobs_.begin(); it != jobs_.end(); ++it)
        jobs[it.key()] = it->display;
    out["selected"] = selected;
    out["pending"] = pendingMap;
    out["jobs"] = jobs;
    QString captureState = "No recording";
    bool recording{};
    for (const auto &c : snapshot_.captures()) {
        // Resume a daemon-owned session after UI restart, only for the selected device.
        bool deviceMatches =
            std::find(c.devices().begin(), c.devices().end(), device_.toStdString()) != c.devices().end();
        if (q(c.id()) != capture_ && !(capture_.isEmpty() && c.active() && deviceMatches))
            continue;
        recording = c.active();
        captureState = recording ? "Recording RawCapture" : "Finalizing RawCapture";
        for (const auto &a : snapshot_.artifacts())
            if (a.id() == c.raw_artifact() && a.state() == "FINALIZED")
                captureState = "RawCapture finalized — select it below";
        if (!c.error().empty())
            captureState = q(c.error());
        out["captureId"] = q(c.id());
        out["captureRaw"] = q(c.raw_artifact());
        out["captureFrames"] = QVariant::fromValue(c.framesets_committed());
        out["finalizationJob"] = q(c.finalization_job_id());
    }
    out["recording"] = recording;
    out["captureState"] = captureState;
    auto target = evidence("target");
    out["checkerboard"] = target.value("pattern") == "checkerboard";
    out["canBuild"] =
        !selected_.value("target").isEmpty() && !raw_.isEmpty() && !pending("dataset") && !running("dataset");
    QVariantMap canSolveCamera;
    for (auto role : {"left", "right"})
        canSolveCamera[role] = !selected_.value("dataset").isEmpty() && !pending(role) && !running(role);
    out["canSolveCamera"] = canSolveCamera;
    out["canSolveBoth"] = canSolveCamera.value("left").toBool() && canSolveCamera.value("right").toBool();
    out["canSolveRig"] = !selected_.value("dataset").isEmpty() && !selected_.value("left").isEmpty() &&
                         !selected_.value("right").isEmpty() && !out.value("checkerboard").toBool() &&
                         !pending("rig") && !running("rig");
    return out;
}
