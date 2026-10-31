#include "bridge.hpp"
#include "calibration_controller.hpp"
#include "devices_model.hpp"
#include "home_model.hpp"
#include "live_views.hpp"
#include "projects_model.hpp"
#include "projects_scroll.hpp"
#include "scan_model.hpp"
#include "screenshot.hpp"
#include <QAccessible>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QKeyEvent>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSet>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>
#include <QWheelEvent>
#include <algorithm>
#include <cstring>
#include <functional>
#include <iostream>
#include <mantis/data_io.hpp>
#include <stdexcept>
#include <source_location>

namespace w = mantis::wire::v1;
class Probe : public QObject {
    Q_OBJECT
  signals:
    void changed();
};
// Observe production DTO projection, and trap any new QML command intent even offline.
class Observed : public StudioBridge {
    Q_OBJECT
  public:
    Observed() : StudioBridge(nullptr, false) {}
    using StudioBridge::applyPreview;
    using StudioBridge::applyResult;
    using StudioBridge::projectGeneration;
    int commands{}, viewAttachments{}, previewAttachments{};
    QSet<QObject *> views, previews;
    Q_INVOKABLE void attachView(QObject *v) {
        ++viewAttachments;
        views.insert(v);
        StudioBridge::attachView(v);
    }
    Q_INVOKABLE void attachPreview(QObject *l, QObject *r) {
        ++previewAttachments;
        previews.insert(l);
        previews.insert(r);
        StudioBridge::attachPreview(l, r);
    }
    Q_INVOKABLE void refresh() {
        ++commands;
    }
    Q_INVOKABLE void startCapture(QString) {
        ++commands;
    }
    Q_INVOKABLE void stopCapture() {
        ++commands;
    }
    Q_INVOKABLE void runPipeline(QString) {
        ++commands;
    }
    Q_INVOKABLE void replay(QString, bool) {
        ++commands;
    }
    Q_INVOKABLE void exportArtifact(QString) {
        ++commands;
    }
    Q_INVOKABLE void cancelJob(QString) {
        ++commands;
    }
    Q_INVOKABLE void selectArtifact(QString) {
        ++commands;
    }
    Q_INVOKABLE void enablePlugin(QString, bool) {
        ++commands;
    }
};
namespace {
QStringList warnings;
void messages(QtMsgType t, const QMessageLogContext &, const QString &s) {
    if (t >= QtWarningMsg && !s.startsWith("QStandardPaths:"))
        warnings.push_back(s);
}
void check(bool ok, const char *s) {
    if (!ok)
        throw std::runtime_error(s);
}
StudioResult snapshot(QString project = "/fixture/Scan-A.mantis", bool active = true) {
    StudioResult r;
    auto &s = r.snapshot.emplace();
    s.set_project_path(project.toStdString());
    auto *d = s.add_devices();
    d->set_id("stereo-parent");
    d->set_name("Public stereo descriptor");
    d->add_capabilities("org.mantis.camera.frameset-stream.v1");
    d->set_plugin_id("org.fixture.camera");
    auto *c = s.add_captures();
    c->set_id("existing-daemon-capture");
    c->set_active(active);
    auto *a = s.add_artifacts();
    a->set_id("raw-advertised");
    a->set_type("org.mantis.RawCapture");
    a->set_state("FINALIZED");
    auto *j = s.add_jobs();
    j->set_id("actual-job");
    j->set_name("Fixture reconstruction");
    j->set_state("Running");
    auto *e = s.add_events();
    e->set_kind("warning");
    e->set_component("fixture");
    e->set_message("No physical readiness published");
    return r;
}
// Exercise the production DTO projection, not a synthetic scan/job association.
void jobAuthority(ScanModel &m, Observed &b, const std::function<void()> &presentation = {}) {
    const auto verify = [&] {
        check(m.data()["processingStatus"] == "Not reported",
              "Runtime-wide inventory invented scan processing authority");
        for (const auto &v : m.data()["jobs"].toList()) {
            const auto row = v.toMap();
            const bool current = m.data()["confirmed"].toBool();
            check(row["stateConfirmed"].toBool() == current &&
                      row["state"].toString().startsWith("Last known: ") == !current,
                  "Job state freshness disagrees with strict snapshot confirmation");
        }
        if (presentation)
            presentation();
    };
    for (const auto &states :
         {QStringList{"Completed", "Running"}, QStringList{"Running", "Completed"}, QStringList{"Running"}}) {
        auto r = snapshot();
        r.snapshot->clear_jobs();
        for (int i = 0; i < states.size(); ++i) {
            auto *j = r.snapshot->add_jobs();
            j->set_id("unrelated-runtime-job-" + std::to_string(i));
            j->set_name("Background task");
            j->set_state(states[i].toStdString());
        }
        b.applyResult(r);
        verify();
        const auto rows = m.data()["jobs"].toList();
        check(rows.size() == states.size(), "Runtime-wide job inventory lost");
        for (int i = 0; i < rows.size(); ++i)
            check(rows[i].toMap()["id"] == "unrelated-runtime-job-" + QString::number(i) &&
                      rows[i].toMap()["state"] == states[i],
                  "Job inventory reordered or associated by name/state");
    }
    StudioResult offline;
    offline.issues.push_back(
        {"snapshot", "Offline", mantis::Error{mantis::Status::io, "Offline", "wire.fixture"}});
    b.applyResult(offline);
    verify();
    check(m.data()["jobs"].toList()[0].toMap()["state"] == "Last known: Running",
          "Disconnected running job lost its per-row stale qualifier");
    check(m.data()["artifacts"].toList()[0].toMap()["state"] == "FINALIZED",
          "Immutable artifact state incorrectly qualified as a current job");
    StudioResult unrelated;
    unrelated.issues.push_back(
        {"artifact", "Refused", mantis::Error{mantis::Status::io, "Refused", "wire.fixture"}});
    b.applyResult(unrelated);
    verify();
    check(!m.data()["confirmed"].toBool(), "Non-snapshot result confirmed old job state");
    b.applyResult(snapshot());
    verify();
    check(m.data()["jobs"].toList()[0].toMap()["state"] == "Running",
          "Confirmed reconnect retained stale qualifier");
    auto malformed = snapshot();
    malformed.snapshot->clear_jobs();
    malformed.snapshot->add_jobs()->set_state("Running"); // No authority ID.
    malformed.snapshot->add_jobs()->set_id("state-unavailable");
    b.applyResult(malformed);
    verify();
    check(m.data()["jobs"].toList().size() == 1 &&
              m.data()["jobs"].toList()[0].toMap()["id"] == "state-unavailable" &&
              m.data()["jobs"].toList()[0].toMap()["state"] == "Unknown",
          "Malformed production job invented an ID or execution state");
    auto empty = snapshot("/fixture/Scan-B.mantis");
    empty.snapshot->clear_jobs();
    b.applyResult(empty);
    verify();
    check(m.data()["jobs"].toList().empty(), "Project B inherited project A jobs");
    b.applyResult({});
    verify();
    b.applyResult(snapshot());
    verify();
}
mantis::data::Published fixtureCloud() {
    using namespace mantis;
    memory::BufferBuilder bytes(512 * 3 * sizeof(float));
    auto writable = bytes.writable();
    for (int i = 0; i < 512; ++i) {
        const float xyz[]{float(i % 32 - 16) * 2, float(i / 32 - 8) * 2, float((i % 7) * 3)};
        std::memcpy(writable.data() + i * sizeof(xyz), xyz, sizeof(xyz));
    }
    data::Packet packet;
    packet.type = schema::points;
    packet.attributes.push_back({{"org.mantis.position", schema::ScalarType::f32, {512, 3}, {12, 4}, "mm"},
                                 std::move(bytes).publish()});
    return data::publish(std::move(packet));
}
mantis::data::Published fixtureFrames() {
    using namespace mantis;
    data::Packet set;
    set.type = schema::frameset;
    for (const auto &role : {std::string("left"), std::string("right")}) {
        const uint64_t width = role == "left" ? 96 : 48, height = role == "left" ? 48 : 96;
        memory::BufferBuilder bytes(width * height);
        auto writable = bytes.writable();
        for (uint64_t y = 0; y < height; ++y)
            for (uint64_t x = 0; x < width; ++x)
                writable[y * width + x] = std::byte((x / 8 + y / 8) % 2 ? 220 : role == "left" ? 40 : 90);
        data::Packet frame;
        frame.type = schema::image;
        frame.header.metadata = {{"role", role}, {"fourcc", "GREY"}};
        frame.attributes.push_back(
            {{"org.mantis.pixels", schema::ScalarType::u8, {height, width}, {width, 1}, "intensity"},
             std::move(bytes).publish()});
        set.frames.push_back(data::publish(std::move(frame)));
    }
    return data::publish(std::move(set));
}
PreviewResult fixturePreview(quint64 generation) {
    QImage left(96, 48, QImage::Format_Grayscale8), right(48, 96, QImage::Format_Grayscale8);
    left.fill(40);
    right.fill(220);
    return {std::move(left), std::move(right), {}, generation, {}};
}
void captureModelTests() {
    ScanModel m;
    Observed b;
    m.setBridge(&b);
    b.applyResult(snapshot("A", false));
    check(!m.data()["canStartCapture"].toBool() && !m.data()["canStopCapture"].toBool(),
          "Idle model guessed a device");
    m.setSelectedDevice("stereo-parent");
    check(m.data()["canStartCapture"].toBool(), "Unique capable device did not enable Start");
    m.setSelectedDevice("unknown");
    check(m.selectedDevice().isEmpty() && !m.data()["canStartCapture"].toBool(),
          "Unknown selected identity admitted");
    for (int variant = 0; variant < 6; ++variant) {
        auto r = snapshot("A", false);
        auto *d = r.snapshot->mutable_devices(0);
        if (variant == 0)
            *r.snapshot->add_devices() = *d;
        if (variant == 1)
            d->clear_capabilities();
        if (variant == 2)
            d->set_id(std::string(4097, 'x'));
        if (variant == 3)
            d->set_parent("other-parent");
        if (variant == 4)
            for (int i = 0; i < 65; ++i)
                d->add_capabilities("unknown-capability");
        if (variant == 5)
            r.snapshot->clear_devices();
        b.applyResult(r);
        m.setSelectedDevice(variant == 2 ? QString(4097, 'x') : "stereo-parent");
        check(m.selectedDevice().isEmpty() && !m.data()["canStartCapture"].toBool(),
              "Invalid device acquired capture authority");
    }
    b.applyResult(snapshot("A", false));
    m.setSelectedDevice("stereo-parent");
    b.applyResult(snapshot("B", false));
    check(m.selectedDevice().isEmpty(), "Project switch retained device selection");
    m.setSelectedDevice("stereo-parent");
    b.applyResult(snapshot("B"));
    check(!m.data()["canStartCapture"].toBool() && m.data()["canStopCapture"].toBool(),
          "Active capture gating failed");
    auto ambiguous = snapshot("B");
    *ambiguous.snapshot->add_captures() = ambiguous.snapshot->captures(0);
    b.applyResult(ambiguous);
    check(!b.captureStateKnown() && m.data()["captureStatus"] == "Unknown" &&
              !m.data()["canStartCapture"].toBool() && !m.data()["canStopCapture"].toBool(),
          "Ambiguous capture acquired Stop authority");
    auto emptyId = snapshot("B");
    emptyId.snapshot->mutable_captures(0)->clear_id();
    b.applyResult(emptyId);
    check(!m.data()["canStopCapture"].toBool(), "Empty active capture ID admitted");
    b.applyResult(snapshot("B", false));
    m.setSelectedDevice("stereo-parent");
    b.applyResult({});
    check(m.selectedDevice().isEmpty() && !m.data()["canStartCapture"].toBool() &&
              !m.data()["canStopCapture"].toBool(),
          "Disconnected selection/commands remained current");
    m.setBridge(nullptr);
    check(m.selectedDevice().isEmpty(), "Detached selection retained");
    Probe malformed;
    malformed.setProperty("connected", true);
    malformed.setProperty("hasSnapshot", true);
    malformed.setProperty("capturing", false);
    malformed.setProperty("captureStateKnown", true);
    malformed.setProperty("devices",
                          QVariantList{QVariantMap{{"id", "bad"},
                                                   {"capabilities", "org.mantis.camera.frameset-stream.v1"},
                                                   {"captureSupported", true}}});
    m.setBridge(&malformed);
    m.setSelectedDevice("bad");
    check(m.selectedDevice().isEmpty(), "Malformed capability/string or loose bool authorized Start");
    check(b.commands == 0, "Local selection invoked runtime command");
}
void modelTests() {
    ScanModel m;
    Probe p;
    m.setBridge(&p);
    check(!m.data()["hasSnapshot"].toBool() && !m.data()["confirmed"].toBool(),
          "Missing fields invented confirmation");
    p.setProperty("connected", "true");
    p.setProperty("hasSnapshot", true);
    emit p.changed();
    check(!m.data()["confirmed"].toBool(), "String truth acquired confirmation");
    p.setProperty("connected", true);
    emit p.changed();
    check(m.data()["captureStatus"] == "Unknown", "Missing capture field fabricated idle");
    p.setProperty("busy", false);
    p.setProperty("captureStateKnown", true);
    p.setProperty("capturing", true);
    p.setProperty("lastKnownCapturing", true);
    p.setProperty("project", "A");
    emit p.changed();
    check(m.data()["captureActive"].toBool() && m.data()["canStopCapture"].toBool() &&
              !m.data()["canStartCapture"].toBool(),
          "Confirmed capture authority failed");
    const auto epochA = m.data()["projectEpoch"].toULongLong();
    p.setProperty("project", "B");
    emit p.changed();
    p.setProperty("project", "A");
    emit p.changed();
    check(m.data()["projectEpoch"].toULongLong() > epochA + 1, "A/B/A project boundary lost");
    p.setProperty("connected", false);
    emit p.changed();
    check(!m.data()["captureActive"].toBool() && m.data()["lastKnownCaptureActive"].toBool() &&
              m.data()["captureStatus"] == "Unknown",
          "Stale capture misclassified");
    p.setProperty("connected", true);
    p.setProperty("capturing", false);
    emit p.changed();
    check(m.data()["captureStatus"] == "Idle", "Confirmed idle failed");
    QVariantList devices, artifacts;
    const QString prefix(220, 'a');
    for (const auto &id : {prefix + "A", prefix + "B", QString(4097, 'z'), QString{}})
        devices.push_back(QVariantMap{{"id", id},
                                      {"name", "<b>literal</b>\u202e\n" + QString(700, 'x')},
                                      {"capabilities", QStringList{"org.mantis.camera.frameset-stream.v1"}}});
    devices.push_back(QVariantMap{{"id", "false-capability"},
                                  {"captureSupported", true},
                                  {"capabilities", "org.mantis.camera.frameset-stream.v1"}});
    devices.push_back(42);
    p.setProperty("devices", devices);
    emit p.changed();
    const auto rows = m.data()["devices"].toList();
    check(rows.size() == 2 && rows[0].toMap()["id"] == prefix + "A" && rows[1].toMap()["id"] == prefix + "B",
          "Exact identities aliased or invalid capability trusted");
    check(rows[0].toMap()["name"].toString().size() <= 193 &&
              !rows[0].toMap()["name"].toString().contains(QChar(0x202e)),
          "Untrusted display unbounded/unsanitized");
    artifacts = {QVariantMap{{"id", "cloud"}, {"type", "org.mantis.PointCloud"}, {"state", "FINALIZED"}}};
    p.setProperty("artifacts", artifacts);
    p.setProperty("selectedArtifact", "cloud");
    emit p.changed();
    check(m.data()["selectedArtifact"] == "cloud" && m.data()["latestPointCloud"] == "cloud",
          "Advertised cloud status lost");
    p.setProperty("selectedArtifact", "foreign");
    emit p.changed();
    check(m.data()["selectedArtifact"] == "None advertised", "Unadvertised artifact presented as current");
    p.setProperty("errorDetails", QVariantList{QVariantMap{{"phase", "artifact"},
                                                           {"code", 5},
                                                           {"component", "wire.test"},
                                                           {"message", "Payload failed"}},
                                               QVariantMap{{"code", "5"}}});
    emit p.changed();
    check(m.data()["issues"].toList()[0].toMap()["phase"] == "artifact" &&
              !m.data()["issues"].toList()[1].toMap()["code"].isValid(),
          "Structured issue type/phase lost");
    for (int i = 0; i < 12000; ++i)
        devices.push_back(QVariantMap{{"id", QString::number(i)},
                                      {"capabilities", QStringList{"org.mantis.camera.image-stream.v1"}}});
    p.setProperty("devices", devices);
    p.setProperty("artifacts", devices);
    p.setProperty("jobs", devices);
    p.setProperty("diagnostics", devices);
    emit p.changed();
    check(m.data()["devices"].toList().size() <= 12 && m.data()["jobs"].toList().size() <= 12 &&
              m.data()["diagnostics"].toList().size() <= 12 &&
              m.data()["latestPointCloud"] == "None / unavailable",
          "Large inventories unbounded or incomplete latest fabricated");
    QSignalSpy changed(&m, &ScanModel::changed);
    QElapsedTimer time;
    time.start();
    for (int i = 0; i < 1000; ++i)
        emit p.changed();
    check(changed.empty(), "Identical normalized snapshots churn delegates");
    std::cout << "1000 bounded identical projections: " << time.elapsed() << " ms; zero changes\n";
    p.setProperty("project", QString(65537, 'p'));
    emit p.changed();
    check(!m.data()["identityValid"].toBool() && m.data()["devices"].toList().empty(),
          "Oversized identity truncated into authority");
    m.setBridge(nullptr);
    check(!m.bridge() && !m.data()["hasSnapshot"].toBool(), "Detach retained data");
    p.setProperty("project", "A");
    emit p.changed();
    check(!m.data()["hasSnapshot"].toBool(), "Detached source still observed");
    auto *temporary = new Probe;
    temporary->setProperty("connected", true);
    temporary->setProperty("hasSnapshot", true);
    m.setBridge(temporary);
    delete temporary;
    check(!m.bridge() && !m.data()["confirmed"].toBool(), "Destroyed bridge retained authority");
    check(m.children().empty(), "Read-only model owns timer/worker");
    Observed actual;
    m.setBridge(&actual);
    check(m.data()["jobs"].toList().empty() && m.data()["processingStatus"] == "Not reported",
          "Production bridge without snapshot invented processing");
    actual.applyResult(snapshot());
    check(m.data()["confirmed"].toBool() && m.data()["captureActive"].toBool(),
          "Production bridge DTO signal seam failed");
    StudioResult failure;
    failure.issues.push_back(
        {"snapshot", "Offline", mantis::Error{mantis::Status::io, "Offline", "wire.fixture"}});
    actual.applyResult(failure);
    check(m.data()["lastKnownCaptureActive"].toBool() && m.data()["artifacts"].toList().size() == 1 &&
              m.data()["issues"].toList()[0].toMap()["phase"] == "snapshot",
          "Production stale evidence lost");
    actual.applyResult(snapshot());
    check(m.data()["confirmed"].toBool() && actual.commands == 0, "Recovery initiated command");
    StudioResult artifactFailure;
    artifactFailure.issues.push_back(
        {"artifact", "Payload refused",
         mantis::Error{mantis::Status::io, "Payload refused", "artifact.fixture"}});
    actual.applyResult(artifactFailure);
    actual.applyResult(snapshot());
    check(m.data()["confirmed"].toBool() && m.data()["runtimeError"].toString().contains("Payload refused") &&
              m.data()["issues"].toList()[0].toMap()["phase"] == "artifact",
          "Unrelated snapshot erased artifact-phase error");
    actual.applyResult(snapshot("/fixture/Scan-A.mantis", false));
    check(m.data()["captureStatus"] == "Idle", "Production confirmed idle misclassified");
    jobAuthority(m, actual);
    m.setBridge(&p);
    p.setProperty("hasSnapshot", false);
    p.setProperty("jobs", QVariantList{QVariantMap{{"id", "old-job"}, {"state", "Running"}}});
    emit p.changed();
    check(m.data()["jobs"].toList().empty() && m.data()["processingStatus"] == "Not reported",
          "No snapshot exposed job state");
    p.setProperty("hasSnapshot", true);
    p.setProperty("project", "A");
    for (const auto &invalid :
         {QVariant("not a list"), QVariant(QVariantList{}),
          QVariant(QVariantList{42, QVariantMap{{"state", "Running"}}, QVariantMap{{"id", 42}}})}) {
        p.setProperty("jobs", invalid);
        emit p.changed();
        check(m.data()["jobs"].toList().empty() && m.data()["processingStatus"] == "Not reported",
              "Malformed/empty job inventory invented IDs or processing");
    }
    p.setProperty("jobs", QVariantList{QVariantMap{{"id", "unknown-state"}, {"state", 42}}});
    emit p.changed();
    check(m.data()["jobs"].toList()[0].toMap()["state"] == "Unknown",
          "Malformed job state fabricated execution");
}
QQuickItem *find(QQuickItem *p, const QString &name) {
    if (p->objectName() == name)
        return p;
    for (auto *c : p->childItems())
        if (auto *r = find(c, name))
            return r;
    return nullptr;
}
QQuickItem *item(QQuickWindow *w, const QString &name) {
    auto *r = find(w->contentItem(), name);
    check(r, ("Missing item " + name).toUtf8().constData());
    return r;
}
ScanModel *model(QQuickWindow *w) {
    return qobject_cast<ScanModel *>(item(w, "scanWorkspace")->property("liveModel").value<QObject *>());
}
void settle() {
    QTest::qWait(30);
}
void click(QQuickWindow *w, const QString &name, bool keyboard = false) {
    auto *b = item(w, name);
    check(b->isEnabled() && b->isVisible(), ("Unavailable " + name).toUtf8().constData());
    b->forceActiveFocus(Qt::TabFocusReason);
    settle();
    if (keyboard)
        QTest::keyClick(w, Qt::Key_Space);
    else
        QTest::mouseClick(w, Qt::LeftButton, Qt::NoModifier,
                          b->mapToScene(QPointF(b->width() / 2, b->height() / 2)).toPoint());
    settle();
}
void capture(QQuickWindow *w, const QString &out, const QString &name) {
    bool done = false, success = false;
    QString error;
    ScreenshotRequest request(w, out + "/" + name + ".png", 0, 5000, [&](bool ok, const QString &s) {
        done = true;
        success = ok;
        error = s;
    });
    check(QTest::qWaitFor([&] { return done; }, 5500) && success, error.toUtf8().constData());
    QImage image(out + "/" + name + ".png");
    const QSize expected(qRound(w->width() * w->devicePixelRatio()),
                         qRound(w->height() * w->devicePixelRatio()));
    // Fractional native Wayland surfaces round through physical compositor
    // allocation. The offscreen DPR matrices retain exact equality.
    const int tolerance = QGuiApplication::platformName().startsWith("wayland") ? 1 : 0;
    check(std::abs(image.width() - expected.width()) <= tolerance &&
              std::abs(image.height() - expected.height()) <= tolerance,
          "Incorrect Qt DPR readback");
}
void bounds(QQuickWindow *w) {
    check(!w->grabWindow().isNull(), "No rendered frame after resize");
    auto *r = item(w, "scanWorkspace");
    for (const auto &name : {"scanToolbar", "scanPanes", "scanCameras", "scanSetup", "scanViewport",
                             "scanStatusStrip", "scanDock", "scanOpenClassic"}) {
        auto *p = item(w, name);
        if (!p->isVisible())
            continue;
        const auto rect = p->mapRectToItem(r, p->boundingRect());
        check(rect.left() >= -1 && rect.top() >= -1 && rect.right() <= r->width() + 1 &&
                  rect.bottom() <= r->height() + 1 && rect.width() > 0 && rect.height() > 0,
              ("Clipped panel " + QString(name)).toUtf8().constData());
    }
    if (r->property("multiPane").toBool()) {
        const auto right = item(w, "scanSetup")
                               ->mapRectToItem(item(w, "scanPanes"), item(w, "scanSetup")->boundingRect())
                               .right();
        check(std::abs(right - item(w, "scanPanes")->width()) <= 1, "Setup rail detached from right edge");
    }
    check(item(w, "scanViewport")->width() > 280, "Elastic viewport unusably narrow");
}
const QList<QSize> sizes{{1080, 720},  {1280, 720},  {1366, 768},  {1440, 900}, {1536, 1024},
                         {1920, 1080}, {2560, 1440}, {3440, 1440}, {3840, 2160}};
void matrix(QQuickWindow *w, Observed &b, const QString &out, bool dpi) {
    auto *r = item(w, "scanWorkspace");
    for (const auto &size : sizes) {
        w->resize(size);
        for (const auto &state :
             dpi ? QStringList{"mock", "live", "hybrid-demo"}
                 : QStringList{"mock", "waiting", "live", "idle", "stale", "hybrid-live", "hybrid-demo"}) {
            w->setProperty("uiMode", state == "mock"              ? "mock"
                                     : state.startsWith("hybrid") ? "hybrid"
                                                                  : "live");
            r->setProperty("bridge", QVariant::fromValue<QObject *>(state == "waiting" ? nullptr : &b));
            auto loaded = snapshot("/fixture/Scan-A.mantis", state != "idle");
            loaded.cloud = fixtureCloud();
            loaded.cloud_id = "fixture-cloud";
            b.applyResult(loaded);
            b.applyPreview(fixturePreview(b.projectGeneration()));
            if (state == "stale")
                b.applyResult({});
            r->setProperty("activeSource", state == "mock" || state == "hybrid-demo" ? "mock" : "live");
            r->setProperty("compactPane", "Viewport");
            settle();
            bounds(w);
            const auto tag =
                state + "-" + QString::number(size.width()) + "x" + QString::number(size.height());
            capture(w, out, tag);
            check((model(w)->bridge() == nullptr) ==
                      (state == "mock" || state == "hybrid-demo" || state == "waiting"),
                  "Source attached incorrectly");
            const bool demo = state == "mock" || state == "hybrid-demo";
            check(item(w, "scanSourceBadge")->property("source") == (demo ? "mock" : "live") &&
                      item(w, "scanDemoStudy")->isVisible() == demo,
                  "Source badge/imagery isolation failed");
            if (!dpi && !r->property("multiPane").toBool()) {
                for (const auto &pane : {"Cameras", "Setup", "Sequence"}) {
                    click(w, "scanPane" + QString(pane), true);
                    bounds(w);
                    capture(w, out, tag + "-" + pane);
                }
                QTest::keyClick(w, Qt::Key_Escape);
                settle();
                check(r->property("compactPane") == "Viewport", "Escape did not return viewport");
            }
            std::cout << tag.toStdString() << " dpr=" << w->devicePixelRatio() << '\n';
        }
    }
    w->resize(1920, 1080);
    settle();
    const auto narrow = item(w, "scanViewport")->width();
    w->resize(3440, 1440);
    settle();
    bounds(w);
    check(item(w, "scanViewport")->width() > narrow + 1400, "Wide viewport failed to grow");
}
void interactions(QQuickWindow *w, Observed &b, const QString &out) {
    auto *r = item(w, "scanWorkspace");
    w->resize(1536, 1024);
    settle();
    check(r->isVisible() && !item(w, "acquisitionWorkspace")->isVisible() && !model(w)->bridge(),
          "Mock Scan routing or isolation failed");
    QSignalSpy navigation(r, SIGNAL(navigate(QString)));
    QMetaObject::invokeMethod(r, "openClassic");
    QMetaObject::invokeMethod(item(w, "scanOpenClassic"), "clicked");
    check(navigation.empty(), "Mock entered Classic hardware route");
    auto *notesSave = item(w, "scanSaveNotes");
    QTest::mouseMove(
        w, notesSave->mapToScene(QPointF(notesSave->width() / 2, notesSave->height() / 2)).toPoint());
    check(
        QTest::qWaitFor(
            [&] {
                return notesSave->findChild<QObject *>("scanUnavailableHover")->property("hovered").toBool();
            },
            1000),
        "Disabled control lost explanatory hover");
    for (const auto &name : {"scanStart", "scanSaveNotes", "scanMarkerMap", "scanProfile", "scanPipeline"}) {
        auto *p = item(w, name);
        check(!p->isEnabled(), "Deferred control enabled");
        auto *a = QAccessible::queryAccessibleInterface(p);
        check(a && !a->text(QAccessible::Description).isEmpty(), "Deferred control lacks accessible reason");
        QSignalSpy activated(p, SIGNAL(clicked()));
        p->forceActiveFocus();
        QTest::keyClick(w, Qt::Key_Space);
        if (auto *action = a->actionInterface())
            action->doAction(QAccessibleActionInterface::pressAction());
        check(activated.empty(), "Disabled control activated");
        QMetaObject::invokeMethod(p, "clicked");
    }
    click(w, "scanReplayTab", true);
    check(r->property("presentationTab") == "Replay Capture", "Replay presentation tab failed");
    click(w, "scanDisplayConfidence");
    check(r->property("displayStyle") == "Confidence", "Display chrome failed");
    for (const auto &tab : {"Tracking", "MarkerMap", "Plugins", "Parameters"})
        click(w, "scanSetup" + QString(tab), true);
    auto *settings = item(w, "scanSetupScroll")->property("contentItem").value<QQuickItem *>();
    auto *cameras = item(w, "scanCameraScroll")->property("contentItem").value<QQuickItem *>();
    check(settings && cameras && settings->property("contentHeight").toDouble() > settings->height(),
          "Setup has no independent scroll surface");
    settings->setProperty("contentY", 0.0);
    const auto cameraY = cameras->property("contentY");
    const auto stagePosition = item(w, "scanViewport")->position();
    const auto point = settings->mapToScene(QPointF(settings->width() / 2, settings->height() / 2));
    QWheelEvent wheel(point, w->mapToGlobal(point.toPoint()), {}, QPoint(0, -120), Qt::NoButton,
                      Qt::NoModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(w, &wheel);
    settle();
    check(settings->property("contentY").toDouble() > 0 && cameras->property("contentY") == cameraY &&
              item(w, "scanViewport")->position() == stagePosition,
          "Settings wheel leaked to camera/viewport");
    check(item(w, "scanNotes")->isVisible(), "Notes lost behind settings scroll");
    capture(w, out, "setup-scroll-notes");
    w->resize(1080, 720);
    settle();
    click(w, "scanPaneSetup", true);
    QTest::keyClick(w, Qt::Key_Escape);
    settle();
    check(r->property("compactPane") == "Viewport" && item(w, "scanPaneViewport")->hasActiveFocus(),
          "Compact Escape lost keyboard focus");
    w->resize(1536, 1024);
    settle();
    w->setProperty("uiMode", "live");
    b.applyResult(snapshot());
    settle();
    check(model(w)->bridge() == &b && model(w)->data()["captureActive"].toBool(), "Live evidence missing");
    click(w, "scanArtifactRow_0", true);
    check(r->property("selectedRow") == "raw-advertised", "Passive selection failed");
    auto removed = snapshot();
    removed.snapshot->clear_artifacts();
    b.applyResult(removed);
    settle();
    check(r->property("selectedRow").toString().isEmpty(), "Removed row retained selection");
    b.applyResult(snapshot());
    r->setProperty("selectedRow", "raw-advertised");
    b.applyResult(snapshot("B"));
    b.applyResult(snapshot());
    settle();
    check(r->property("selectedRow").toString().isEmpty(), "Project A/B/A retained selection");
    w->setProperty("uiMode", "hybrid");
    click(w, "scanDemoSource", true);
    check(!model(w)->bridge() && r->property("illustrative").toBool() &&
              model(w)->data()["artifacts"].toList().empty(),
          "Hybrid demo merged live evidence");
    click(w, "scanLiveSource");
    check(model(w)->bridge() == &b && !r->property("illustrative").toBool(),
          "Hybrid live restoration failed");
    const auto original = QList<QQuickItem *>{item(w, "acquisitionWorkspace"), item(w, "pointCloudView"),
                                              item(w, "leftPreview"), item(w, "rightPreview")};
    click(w, "scanOpenClassic", true);
    check(w->property("workspace") == "acquisition", "Keyboard Classic route failed");
    w->setProperty("workspace", "scan");
    click(w, "scanOpenClassic");
    check(w->property("workspace") == "acquisition", "Mouse Classic route failed");
    std::vector<qint64> routeMicros;
    for (int i = 0; i < 100; ++i) {
        QElapsedTimer routeTimer;
        routeTimer.start();
        w->setProperty("workspace", i % 2 ? "scan" : "acquisition");
        QCoreApplication::processEvents();
        check(model(w)->data()["captureActive"].toBool(), "Navigation changed capture evidence");
    }
    w->setProperty("workspace", "scan");
    QMetaObject::invokeMethod(r, "openClassic");
    check(w->property("workspace") == "acquisition", "Programmatic Classic route failed");
    w->setProperty("workspace", "scan");
    settle();
    check(original == QList<QQuickItem *>{item(w, "acquisitionWorkspace"), item(w, "pointCloudView"),
                                          item(w, "leftPreview"), item(w, "rightPreview")} &&
              b.views.size() == 1 && b.previews.size() == 2,
          "Classic view identity/attachments changed");
    check(b.commands == 0 && b.capturing(), "Scan initiated runtime commands or stopped active capture");
    capture(w, out, "live-classic-roundtrip");
}
void jobPresentation(QQuickWindow *w, Observed &b, const QString &out) {
    auto *r = item(w, "scanWorkspace");
    w->resize(1920, 1080);
    w->setProperty("uiMode", "live");
    settle();
    const auto verify = [&] {
        settle();
        check(item(w, "scanStatusProcessing")->property("text") == "Not reported",
              "Production status strip claimed an unscoped processing state");
        const auto timeline = item(w, "scanTimelineProcessing")->property("text").toString();
        check(timeline.contains("Scan processing: Not reported") &&
                  timeline.contains("runtime jobs are not linked to this timeline") &&
                  !timeline.contains("Running") && !timeline.contains("Completed"),
              "Production timeline implied scan/job execution association");
    };
    jobAuthority(*model(w), b, verify);
    StudioResult offline;
    offline.issues.push_back(
        {"snapshot", "Offline", mantis::Error{mantis::Status::io, "Offline", "wire.fixture"}});
    b.applyResult(offline);
    verify();
    capture(w, out, "job-authority-stale-1920x1080");
    for (const auto &mode : {"mock", "hybrid"}) {
        w->setProperty("uiMode", mode);
        if (QString(mode) == "hybrid")
            click(w, "scanDemoSource", true);
        verify();
        check(!model(w)->bridge() && model(w)->data()["jobs"].toList().empty(),
              "Mock/demo retained live job inventory");
    }
    click(w, "scanLiveSource", true);
    verify();
    check(model(w)->data()["jobs"].toList()[0].toMap()["state"] == "Last known: Running",
          "Returning to live promoted an old job to current");
    b.applyResult(snapshot());
    verify();
    check(model(w)->data()["jobs"].toList()[0].toMap()["state"] == "Running",
          "Production shell reconnect did not clear stale job qualifier");
    auto empty = snapshot("/fixture/New-project.mantis");
    empty.snapshot->clear_jobs();
    b.applyResult(empty);
    verify();
    check(model(w)->data()["jobs"].toList().empty(), "Shell project switch inherited old jobs");
    r->setProperty("bridge", QVariant::fromValue<QObject *>(nullptr));
    verify();
    check(model(w)->data()["jobs"].toList().empty(), "Waiting shell retained old jobs");
    r->setProperty("bridge", QVariant::fromValue<QObject *>(&b));
    check(b.commands == 0, "Job presentation issued a runtime command");
}
void orbitZoomReset(QQuickWindow *w, mantis::render::PointCloudView *cloud, const QString &reset) {
    settle(); // Complete layout/focus changes before mapping the active visual host.
    check(cloud->isVisible() && cloud->pointCount() == 512 &&
              item(w, "geometryInteraction")->property("enabled").toBool(),
          "Loaded local geometry interaction unavailable");
    cloud->setYaw(17);
    cloud->setPitch(23);
    cloud->setZoom(1.4);
    const auto center = cloud->mapToScene(QPointF(cloud->width() / 2, cloud->height() / 2));
    QTest::mousePress(w, Qt::LeftButton, Qt::NoModifier, center.toPoint());
    QTest::mouseMove(w, (center + QPointF(30, 20)).toPoint(), 10);
    QTest::mouseRelease(w, Qt::LeftButton, Qt::NoModifier, (center + QPointF(30, 20)).toPoint());
    check(cloud->yaw() != 17 && cloud->pitch() != 23,
          QString("Real mouse orbit did not update renderer in %1 / %2 at %3,%4 (%5x%6)")
              .arg(w->property("workspace").toString(), w->property("uiMode").toString())
              .arg(center.x())
              .arg(center.y())
              .arg(cloud->width())
              .arg(cloud->height())
              .toUtf8()
              .constData());
    QWheelEvent wheel(center, w->mapToGlobal(center.toPoint()), {}, QPoint(0, 120), Qt::NoButton,
                      Qt::NoModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(w, &wheel);
    check(cloud->zoom() > 1.4, "Real wheel zoom did not update renderer");
    click(w, reset, true);
    check(cloud->yaw() == -35 && cloud->pitch() == 35 && cloud->zoom() == 1,
          "Reset did not update real renderer");
}
void surfaces(QQuickWindow *w, Observed &b, const QString &out) {
    auto *owner = qobject_cast<LiveViews *>(item(w, "liveSurfaces"));
    check(owner, "Canonical owner unavailable");
    QPointer<QQuickItem> cloud = owner->cloud(), left = owner->left(), right = owner->right();
    const auto verify = [&] {
        check(cloud && left && right && owner->cloud() == cloud && owner->left() == left &&
                  owner->right() == right,
              "Canonical visual identities changed");
        check(cloud->QObject::parent() == owner && left->QObject::parent() == owner &&
                  right->QObject::parent() == owner,
              "Visual host acquired lifetime ownership");
        check(w->findChildren<mantis::render::PointCloudView *>().size() == 1 &&
                  owner->findChildren<MeasurementView *>().size() == 2 &&
                  w->findChildren<MeasurementView *>().size() == 2,
              "Duplicate canonical renderer or previews");
        for (auto *view : {cloud.data(), left.data(), right.data()})
            check(QQmlEngine::objectOwnership(view) == QQmlEngine::CppOwnership && view->window() == w &&
                      view->width() > 0 && view->height() > 0,
                  "Canonical ownership/window/finite geometry contract failed");
        check(b.viewAttachments == 1 && b.previewAttachments == 1,
              "Route, layout or snapshot caused redundant attachment");
    };
    auto *r = item(w, "scanWorkspace");
    w->setProperty("uiMode", "live");
    w->resize(1920, 1080);
    b.applyResult(snapshot());
    settle();
    verify();
    auto loaded = snapshot();
    loaded.cloud = fixtureCloud();
    loaded.cloud_id = "fixture-cloud";
    b.applyResult(loaded);
    b.applyPreview(fixturePreview(b.projectGeneration()));
    auto *cloudView = qobject_cast<mantis::render::PointCloudView *>(cloud.data());
    auto *leftView = qobject_cast<MeasurementView *>(left.data());
    auto *rightView = qobject_cast<MeasurementView *>(right.data());
    const auto leftStorage = leftView->image().constBits(), rightStorage = rightView->image().constBits();
    cloudView->setYaw(17);
    cloudView->setPitch(23);
    cloudView->setZoom(1.4);
    const auto pixels = [&](bool geometry = true) {
        settle();
        const auto frame = w->grabWindow();
        check(!frame.isNull(), "Real canonical frame unavailable");
        for (auto *preview : {leftView, rightView}) {
            check(preview->hasImage(), "Canonical grayscale pixels absent");
            const auto p = preview->mapToScene(QPointF(preview->width() / 2, preview->height() / 2));
            const auto color = frame.pixelColor(qRound(p.x() * w->devicePixelRatio()),
                                                qRound(p.y() * w->devicePixelRatio()));
            const int expected = preview == leftView ? 40 : 220;
            check(std::abs(color.red() - expected) < 3 && color.red() == color.green() &&
                      color.green() == color.blue(),
                  "Expected grayscale pixels missing from actual Qt host region");
            const bool wide = preview->width() / preview->height() >
                              preview->imageSize().width() / double(preview->imageSize().height());
            const auto edge = preview->mapToScene(wide ? QPointF(2, preview->height() / 2)
                                                       : QPointF(preview->width() / 2, 2));
            const auto background = frame.pixelColor(qRound(edge.x() * w->devicePixelRatio()),
                                                     qRound(edge.y() * w->devicePixelRatio()));
            check(background == QColor("#10151c"), "Non-square grayscale aspect preservation failed");
        }
        if (!geometry)
            return;
        const auto region = cloudView->mapRectToScene(cloudView->boundingRect());
        int geometryPixels = 0;
        for (int y = qRound(region.top() * w->devicePixelRatio()) + 40;
             y < qRound(region.bottom() * w->devicePixelRatio()) - 20; ++y)
            for (int x = qRound(region.left() * w->devicePixelRatio()) + 60;
                 x < qRound(region.right() * w->devicePixelRatio()) - 60; ++x) {
                const auto color = frame.pixelColor(x, y);
                if (color.green() > 120 && color.green() > color.red() * 1.3 && color.blue() > 80)
                    ++geometryPixels;
            }
        check(geometryPixels > 600, "Published geometry absent from actual Qt frame");
    };
    const auto cloudReferences = loaded.cloud.use_count();
    check(cloudReferences == 2, "Published cloud must be shared only by fixture and canonical renderer");
    {
        QQuickWindow otherWindow;
        QQuickItem foreignHost(otherWindow.contentItem());
        const auto original = owner->cloudHost();
        owner->setCloudHost(&foreignHost);
        check(owner->cloudHost() == original && cloud->window() == w, "Cross-window hosting admitted");
        qmlEngine(owner)->collectGarbage();
        verify();
    }
    pixels();
    capture(w, out, "scan-real-pixels");
    w->setProperty("workspace", "acquisition");
    settle();
    pixels();
    capture(w, out, "classic-real-pixels");
    // Calibration's capture stage uses the same live pair and never steals bridge attachment.
    auto *calibration = item(w, "calibrationWorkspace")->property("controller").value<QObject *>();
    check(calibration, "Calibration controller missing");
    calibration->setProperty("stage", 2);
    w->setProperty("workspace", "calibration");
    settle();
    verify();
    pixels(false);
    check(left->parentItem() == item(w, "calibrationLeftHost") &&
              right->parentItem() == item(w, "calibrationRightHost"),
          "Calibration live pair not canonically hosted");
    capture(w, out, "calibration-real-pixels");
    for (int stage = 0; stage < 7; ++stage) {
        calibration->setProperty("stage", stage);
        QCoreApplication::processEvents();
        verify();
        check(left->parentItem() == (stage == 2 ? item(w, "calibrationLeftHost") : owner),
              "Calibration stage left wrong live host");
    }
    w->setProperty("workspace", "scan");
    std::vector<qint64> routeMicros;
    for (int i = 0; i < 100; ++i) {
        QElapsedTimer routeTimer;
        routeTimer.start();
        w->setProperty("workspace", i % 2 ? "scan" : "acquisition");
        QCoreApplication::processEvents();
        routeMicros.push_back(routeTimer.nsecsElapsed() / 1000);
        check(loaded.cloud.use_count() == cloudReferences, "Navigation duplicated Published cloud ownership");
        check(cloud->parentItem() == item(w, i % 2 ? "scanCloudHost" : "classicCloudHost"),
              "Renderer remained in wrong route host");
        check(left->parentItem() == item(w, i % 2 ? "scanLeftHost" : "classicLeftHost") &&
                  right->parentItem() == item(w, i % 2 ? "scanRightHost" : "classicRightHost"),
              "Preview remained in wrong route host");
        verify();
        check(cloudView->pointCount() == 512 && cloudView->yaw() == 17 && cloudView->pitch() == 23 &&
                  cloudView->zoom() == 1.4 && leftView->image().constBits() == leftStorage &&
                  rightView->image().constBits() == rightStorage,
              "Route transition lost view state or duplicated pixel buffers");
    }
    w->setProperty("workspace", "scan");
    for (int i = 0; i < 120; ++i) {
        w->resize(i % 3 ? QSize(1080, 720) : QSize(1920, 1080));
        r->setProperty("compactPane", QStringList{"Viewport", "Cameras", "Setup", "Sequence"}[i % 4]);
        QCoreApplication::processEvents();
        check(cloud->parentItem() ==
                  (r->property("viewportActive").toBool() ? item(w, "scanCloudHost") : owner),
              "Compact viewport incorrectly hosted");
        check(left->parentItem() == (r->property("camerasActive").toBool() ? item(w, "scanLeftHost") : owner),
              "Compact camera incorrectly hosted");
        verify();
    }
    w->resize(1920, 1080);
    w->setProperty("uiMode", "hybrid");
    for (int i = 0; i < 40; ++i) {
        r->setProperty("activeSource", "mock");
        QCoreApplication::processEvents();
        check(!cloud->isVisible() && !left->isVisible() && !right->isVisible() &&
                  cloud->parentItem() == owner && left->parentItem() == owner,
              "Hybrid demo leaked canonical live surfaces");
        verify();
        r->setProperty("activeSource", "live");
        QCoreApplication::processEvents();
        verify();
    }
    for (int i = 0; i < 500; ++i) {
        b.applyResult(snapshot());
        QCoreApplication::processEvents();
        verify();
    }
    check(loaded.cloud.use_count() == cloudReferences && leftView->image().constBits() == leftStorage &&
              rightView->image().constBits() == rightStorage,
          "Steady snapshots duplicated canonical buffers");
    std::sort(routeMicros.begin(), routeMicros.end());
    std::cout << "Canonical buffers: cloud=" << loaded.cloud->attributes.front().buffer.size()
              << " bytes, Published refs=" << cloudReferences
              << "; grayscale=" << leftView->image().sizeInBytes() + rightView->image().sizeInBytes()
              << " bytes; route GUI latency us p50=" << routeMicros[50] << " p95=" << routeMicros[95]
              << " max=" << routeMicros.back() << '\n';
    orbitZoomReset(w, cloudView, "scanResetView");
    w->setProperty("workspace", "acquisition");
    settle();
    orbitZoomReset(w, cloudView, "classicResetView");
    auto partial = fixturePreview(b.projectGeneration());
    partial.right = {};
    b.applyPreview(std::move(partial));
    check(!b.previewCurrent() && leftView->image().constBits() == leftStorage && !b.previewError().isEmpty(),
          "Missing frame became current or destroyed last-known buffer");
    StudioResult failure;
    failure.issues.push_back(
        {"snapshot", "Offline", mantis::Error{mantis::Status::io, "Offline", "wire.fixture"}});
    b.applyResult(failure);
    settle();
    check(!b.previewCurrent() && cloudView->pointCount() == 512 && leftView->hasImage(),
          "Disconnect lost last-known display evidence");
    const auto epoch = model(w)->data()["projectEpoch"];
    for (const auto &mode : {"live", "hybrid"}) {
        w->setProperty("uiMode", mode);
        for (const auto &route : {"acquisition", "scan"}) {
            w->setProperty("workspace", route);
            settle();
            orbitZoomReset(w, cloudView, QString(route) == "scan" ? "scanResetView" : "classicResetView");
            check(!b.connected() && !model(w)->data()["confirmed"].toBool() &&
                      model(w)->data()["projectEpoch"] == epoch,
                  "Local inspection refreshed stale state or changed project generation");
            auto *reset = item(w, QString(route) == "scan" ? "scanResetView" : "classicResetView");
            auto *accessible = QAccessible::queryAccessibleInterface(reset);
            check(accessible && accessible->text(QAccessible::Description).contains("local") &&
                      accessible->text(QAccessible::Description).contains("offline"),
                  "Offline reset accessibility description is misleading");
            check(item(w, "acquisitionFreshness")->property("text").toString().contains("last-known") &&
                      model(w)->data()["freshness"].toString().contains("Last-known"),
                  "Local inspection lost last-known qualifiers");
            verify();
            capture(w, out, QString("offline-%1-%2").arg(mode, route));
        }
    }
    const auto noInteraction = [&] {
        settle();
        check(!owner->property("interactive").toBool() && !cloud->isVisible() &&
                  cloud->parentItem() == owner &&
                  !item(w, "geometryInteraction")->property("enabled").toBool() &&
                  !item(w, "scanResetView")->isEnabled() && !item(w, "classicResetView")->isEnabled(),
              QString("Inactive geometry guard in %1 / %2 / %3: interactive=%4 visible=%5 parked=%6 mouse=%7 "
                      "scanReset=%8 classicReset=%9")
                  .arg(w->property("workspace").toString(), w->property("uiMode").toString(),
                       r->property("activeSource").toString())
                  .arg(owner->property("interactive").toBool())
                  .arg(cloud->isVisible())
                  .arg(cloud->parentItem() == owner)
                  .arg(item(w, "geometryInteraction")->property("enabled").toBool())
                  .arg(item(w, "scanResetView")->isEnabled())
                  .arg(item(w, "classicResetView")->isEnabled())
                  .toUtf8()
                  .constData());
        const auto yaw = cloudView->yaw(), pitch = cloudView->pitch(), zoom = cloudView->zoom();
        QMetaObject::invokeMethod(owner, "resetView");
        const QPoint center(950, 420);
        QTest::mousePress(w, Qt::LeftButton, Qt::NoModifier, center);
        QTest::mouseMove(w, center + QPoint(30, 20), 10);
        QTest::mouseRelease(w, Qt::LeftButton, Qt::NoModifier, center + QPoint(30, 20));
        QWheelEvent wheel(center, w->mapToGlobal(center), {}, QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
                          Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(w, &wheel);
        check(cloudView->yaw() == yaw && cloudView->pitch() == pitch && cloudView->zoom() == zoom,
              "Hidden/illustrative input changed canonical camera properties");
        verify();
    };
    cloudView->setYaw(41);
    cloudView->setPitch(29);
    cloudView->setZoom(1.6);
    r->setProperty("activeSource", "mock");
    noInteraction(); // Hybrid-Demo.
    r->setProperty("activeSource", "live");
    w->setProperty("uiMode", "mock");
    noInteraction();
    w->setProperty("workspace", "acquisition");
    noInteraction(); // Pure Mock cannot activate Classic's live surface.
    w->setProperty("uiMode", "live");
    for (const auto &route : {"home", "devices"}) {
        w->setProperty("workspace", route);
        noInteraction();
    }
    w->setProperty("workspace", "scan");
    w->resize(1080, 720);
    r->setProperty("compactPane", "Cameras");
    noInteraction();
    r->setProperty("compactPane", "Viewport");
    settle();
    check(cloudView->yaw() == 41 && cloudView->pitch() == 29 && cloudView->zoom() == 1.6 &&
              owner->cloud() == cloud && owner->property("interactive").toBool() &&
              !item(w, "classicResetView")->isEnabled(),
          "Returning to Scan lost canonical camera state or enabled inactive Classic reset");
    orbitZoomReset(w, cloudView, "scanResetView");
    capture(w, out, "stale-real-pixels");
    w->resize(1920, 1080);
    b.applyResult(snapshot());
    settle();
    check(cloudView->pointCount() == 512 && owner->cloud() == cloud && model(w)->data()["confirmed"].toBool(),
          "Reconnect discarded retained local geometry");
    orbitZoomReset(w, cloudView, "scanResetView");
    const auto oldGeneration = b.projectGeneration();
    b.applyResult(snapshot("B"));
    b.applyResult(snapshot());
    b.applyPreview(fixturePreview(oldGeneration));
    auto late = snapshot();
    late.requestGeneration = oldGeneration;
    late.cloud = fixtureCloud();
    b.applyResult(late);
    check(cloudView->pointCount() == 0 && !leftView->hasImage() && !rightView->hasImage() &&
              !b.previewCurrent(),
          "Old A preview/cloud crossed A/B/A project generation");
    b.applyPreview(fixturePreview(b.projectGeneration()));
    auto stopped = snapshot("/fixture/Scan-A.mantis", false);
    b.applyResult(stopped);
    auto oldSource = fixturePreview(b.projectGeneration());
    oldSource.source = "existing-daemon-capture";
    b.applyPreview(std::move(oldSource));
    check(!b.previewCurrent(), "Late stopped-capture frame became current");
    b.applyResult(snapshot());
    b.applyPreview(fixturePreview(b.projectGeneration()));
    check(b.previewCurrent() && leftView->hasImage(),
          "Preview did not recover after rehosting/project reset");
    check(b.commands == 0, "Visual hosting issued runtime command");
    capture(w, out, "canonical-live");
}
void stress(QQuickWindow *w, Observed &b, const QString &out) {
    w->setProperty("uiMode", "live");
    b.applyResult(snapshot());
    w->resize(1536, 1024);
    settle();
    QPointer<QQuickItem> first = item(w, "scanArtifactRow_0"), acquisition = item(w, "acquisitionWorkspace");
    QSignalSpy changed(model(w), &ScanModel::changed);
    QElapsedTimer timer;
    timer.start();
    for (int i = 0; i < 500; ++i)
        b.applyResult(snapshot());
    settle();
    check(changed.empty() && first && first == item(w, "scanArtifactRow_0"),
          "Identical DTOs churned delegates");
    std::cout << "500 identical bridge DTOs: " << timer.elapsed() << " ms, zero Scan changes\n";
    for (int i = 0; i < 120; ++i) {
        w->resize(sizes[i % sizes.size()]);
        w->setProperty("workspace", i % 3 == 0 ? "acquisition" : "scan");
        item(w, "scanWorkspace")->setProperty("compactPane", i % 2 ? "Setup" : "Viewport");
        settle();
        if (w->property("workspace") == "scan")
            bounds(w);
        check(acquisition && first, "Resize/routing destroyed persistent views/delegates");
    }
    w->setProperty("workspace", "scan");
    for (int i = 0; i < 30; ++i) {
        w->setProperty("uiMode", QStringList{"mock", "hybrid", "live"}[i % 3]);
        b.applyResult(snapshot());
        settle();
    }
    check(b.commands == 0 && b.views.size() == 1 && b.previews.size() == 2,
          "Stress introduced commands or views");
    auto inventory = snapshot();
    for (int i = 1; i < 12; ++i) {
        auto *a = inventory.snapshot->add_artifacts();
        a->set_id("raw-" + std::to_string(i));
        a->set_type("org.mantis.RawCapture");
        a->set_state("FINALIZED");
    }
    b.applyResult(inventory);
    w->resize(1080, 720);
    settle();
    click(w, "scanPaneSequence", true);
    click(w, "scanArtifactRow_0", true);
    for (int i = 0; i < 11; ++i) {
        QTest::keyClick(w, Qt::Key_Down);
        settle();
    }
    QTest::keyClick(w, Qt::Key_Space);
    settle();
    check(w->activeFocusItem() && w->activeFocusItem()->objectName() == "scanArtifactRow_11" &&
              item(w, "scanWorkspace")->property("selectedRow") == "raw-11",
          "Virtual sequence keyboard traversal lost selection/focus");
    const auto rect = w->activeFocusItem()->mapRectToItem(item(w, "scanSequenceList"),
                                                          w->activeFocusItem()->boundingRect());
    check(rect.top() >= -1 && rect.bottom() <= item(w, "scanSequenceList")->height() + 1,
          "Focused sequence row not revealed");
    check(b.commands == 0, "Sequence selection initiated runtime request");
    capture(w, out, "stress-final");
}
void wire(QQuickWindow *w, StudioBridge &b, const QString &out) {
    mantis::client::Client control;
    auto mode = [&](int n) {
        w::Request r;
        r.mutable_events()->set_after(n);
        (void)control.call(r);
    };
    check(QTest::qWaitFor([&] { return b.connected() && b.capturing(); }, 5000),
          "Public wire initial capture missing");
    check(model(w)->data()["captureActive"].toBool(), "Public wire model signal failed");
    QPointer<QQuickItem> stableRow = item(w, "scanArtifactRow_0");
    QSignalSpy snapshots(&b, &StudioBridge::snapshotReady);
    check(QTest::qWaitFor([&] { return snapshots.size() >= 2; }, 5000),
          "Normal bridge polling did not complete");
    check(stableRow && stableRow == item(w, "scanArtifactRow_0"),
          "Unchanged polling / busy transitions recreated artifact delegates");
    for (int i = 0; i < 80; ++i) {
        w->setProperty("workspace", i % 2 ? "scan" : "acquisition");
        QCoreApplication::processEvents();
    }
    w->setProperty("workspace", "scan");
    capture(w, out, "wire-active");
    mode(1);
    check(QTest::qWaitFor([&] { return !b.connected() && !b.busy(); }, 5000), "Wire disconnect missing");
    check(model(w)->data()["lastKnownCaptureActive"].toBool() && !model(w)->data()["captureActive"].toBool(),
          "Wire last-known semantics failed");
    capture(w, out, "wire-stale");
    mode(2);
    check(QTest::qWaitFor([&] { return b.connected() && b.project() == "B" && !b.capturing(); }, 5000),
          "Wire project/idle missing");
    mode(3);
    check(QTest::qWaitFor([&] { return b.connected() && b.project() == "A" && b.capturing(); }, 5000),
          "Wire A/B/A recovery missing");
    capture(w, out, "wire-recovery");
}
void captureWire(QQuickWindow *w, StudioBridge &b, const QString &out) {
    mantis::client::Client client;
    auto *r = item(w, "scanWorkspace");
    auto *owner = qobject_cast<LiveViews *>(item(w, "liveSurfaces"));
    auto *cloud = qobject_cast<mantis::render::PointCloudView *>(owner->cloud());
    auto *left = qobject_cast<MeasurementView *>(owner->left());
    auto *right = qobject_cast<MeasurementView *>(owner->right());
    int fixtureMode = 10;
    QString intent;
    const auto await = [&](auto predicate, int deadline = 6000,
                           std::source_location where = std::source_location::current()) {
        if (!QTest::qWaitFor(predicate, deadline))
            throw std::runtime_error(
                QString("Capture wire deadline: line=%1 mode=%2 intent=%3 connected=%4 busy=%5 "
                        "captureKnown=%6 capturing=%7 previewCurrent=%8 error=%9")
                    .arg(where.line())
                    .arg(fixtureMode)
                    .arg(intent)
                    .arg(b.connected())
                    .arg(b.busy())
                    .arg(b.captureStateKnown())
                    .arg(b.capturing())
                    .arg(b.previewCurrent())
                    .arg(b.error())
                    .toStdString());
    };
    const auto captureClick = [&](const QString &name, bool keyboard = false) {
        intent = name;
        // Begin input just after an actual periodic snapshot completes. Otherwise its
        // busy lease can correctly disable/cancel a button between key press/release.
        // Send one intent only; preserve the authoritative post-click assertions.
        QSignalSpy confirmations(&b, &StudioBridge::snapshotReady);
        await([&] {
            return !confirmations.empty() && !b.busy() && item(w, name)->isEnabled() &&
                   item(w, name)->isVisible();
        });
        auto *button = item(w, name);
        button->forceActiveFocus(Qt::TabFocusReason);
        check(w->activeFocusItem() == button, "Capture control did not acquire input focus");
        QSignalSpy clicks(button, SIGNAL(clicked()));
        // Deterministically put a normal periodic refresh at the next event-loop
        // turn. A test gesture must not yield between its press and release.
        QTimer::singleShot(0, &b, &StudioBridge::refresh);
        // QTest's key helper calls processEvents after each edge. Deliver this
        // one ready gesture synchronously through Qt's normal window input path;
        // the queued refresh must run after admission, not cancel a held button.
        if (keyboard) {
            QKeyEvent press(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier, " ");
            QKeyEvent release(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier, " ");
            QCoreApplication::sendEvent(w, &press);
            QCoreApplication::sendEvent(w, &release);
        } else {
            const auto position = button->mapToScene(QPointF(button->width() / 2, button->height() / 2));
            // Unlike keyClick, mouseClick delivers both QPA edges before it
            // processes events. Retain its platform-device/timestamp handling.
            QTest::mouseClick(w, Qt::LeftButton, Qt::NoModifier, position.toPoint());
        }
        if (clicks.size() != 1)
            throw std::runtime_error(QString("Capture gesture cancelled: mode=%1 intent=%2 busy=%3")
                                         .arg(fixtureMode)
                                         .arg(name)
                                         .arg(b.busy())
                                         .toStdString());
    };
    const auto refresh = [&] {
        await([&] { return !b.busy(); });
        b.refresh();
        await([&] { return !b.busy(); });
    };
    const auto control = [&](int mode) {
        fixtureMode = mode;
        intent.clear();
        w::Request req;
        req.mutable_events()->set_after(mode);
        (void)client.call(req);
        refresh();
        settle();
    };
    const auto count = [&](const std::string &kind) {
        w::Request req;
        req.mutable_events()->set_after(90);
        const auto result = client.call(req);
        for (const auto &e : result.events())
            if (e.kind() == kind)
                return e.sequence();
        return uint64_t{};
    };
    const auto choose = [&] {
        check(QMetaObject::invokeMethod(r, "chooseCaptureDevice", Q_ARG(QVariant, QVariant("stereo-parent"))),
              "Device selection method unavailable");
        settle();
    };
    const auto intents = [&] {
        QMetaObject::invokeMethod(r, "startCapture");
        QMetaObject::invokeMethod(r, "stopCapture");
        QMetaObject::invokeMethod(item(w, "scanStart"), "clicked");
        QMetaObject::invokeMethod(item(w, "scanStop"), "clicked");
    };
    w->resize(1920, 1080);
    await([&] { return b.connected() && !b.busy() && cloud->pointCount() == 512; });
    check(!b.capturing() && b.captureStateKnown() && !item(w, "scanStart")->isEnabled(),
          "Idle bridge guessed a selection");
    for (const auto &bad : {QString{}, QString("unknown"), QString(4097, 'x')})
        b.startCapture(bad);
    b.stopCapture();
    for (const auto &mode : {"mock", "hybrid"}) {
        w->setProperty("uiMode", mode);
        if (QString(mode) == "hybrid")
            r->setProperty("activeSource", "mock");
        choose();
        intents();
        check(!item(w, "scanStart")->isEnabled() && !item(w, "scanStop")->isEnabled() &&
                  !owner->cloud()->isVisible(),
              "Mock/demo authority or visual isolation failed");
    }
    r->setProperty("activeSource", "live");
    control(20);
    choose();
    check(item(w, "scanStart")->isEnabled() && !item(w, "scanStop")->isEnabled(),
          "Permitted Start unavailable");
    captureClick("scanStart", true);
    check(b.busy() && !b.capturing(), "Start was optimistic or completion lease lost");
    for (int i = 0; i < 8; ++i) {
        intents();
        b.startCapture("stereo-parent");
        b.stopCapture();
    }
    r->setProperty("activeSource", "mock");
    w->setProperty("workspace", "acquisition");
    QCoreApplication::processEvents();
    check(b.busy() && !b.capturing(), "Start completion escaped the fixture release gate");
    w::Request releaseStart;
    releaseStart.mutable_events()->set_after(91);
    (void)client.call(releaseStart);
    for (int i = 0; i < 100; ++i) {
        w->setProperty("workspace", i % 2 ? "scan" : "acquisition");
        QCoreApplication::processEvents();
    }
    w->setProperty("workspace", "scan");
    await([&] { return !b.busy() && b.capturing() && b.previewCurrent(); });
    check(b.lastKnownCapturing() && !owner->cloud()->isVisible(),
          "Capture did not survive pending source/route changes");
    r->setProperty("activeSource", "live");
    settle();
    check(left->imageSize() == QSize(96, 48) && right->imageSize() == QSize(48, 96) &&
              left->image().pixelColor(0, 0).red() == 40 && right->image().pixelColor(0, 0).red() == 90 &&
              left->image().pixelColor(8, 0).red() == 220,
          "Existing worker did not convert valid mapped grayscale pair");
    capture(w, out, "capture-active-real-wire");
    const auto beforeData = count("artifact_data"), before = count("preview");
    QTest::qWait(1000);
    const auto baseline = count("preview") - before;
    for (int i = 0; i < 100; ++i) {
        w->setProperty("workspace", i % 2 ? "scan" : "acquisition");
        if (i % 5 == 0)
            r->setProperty("activeSource", i % 10 ? "live" : "mock");
        QCoreApplication::processEvents();
    }
    w->setProperty("workspace", "scan");
    r->setProperty("activeSource", "live");
    const auto after = count("preview");
    QTest::qWait(1000);
    const auto routed = count("preview") - after;
    check(baseline >= 8 && baseline <= 20 && routed >= 8 && routed <= 20 &&
              std::abs(int(baseline) - int(routed)) <= 5,
          "Preview request budget changed after visual routing");
    check(count("artifact_data") == beforeData && cloud->pointCount() == 512,
          "Navigation loaded extra cloud or rewound geometry");
    std::cout << "preview requests / 1000 ms: baseline=" << baseline << " routed=" << routed
              << "; navigation artifact_data=0; one canonical renderer/two live previews\n";
    captureClick("scanStop", true);
    await([&] { return !b.busy() && !b.capturing() && b.captureStateKnown(); });
    check(!item(w, "scanStop")->isEnabled(), "Stop did not follow confirmed daemon snapshot");
    for (int mode : {12, 13, 14, 15, 16, 26, 27, 29}) {
        control(mode);
        choose();
        intents();
        b.startCapture("stereo-parent");
        b.stopCapture();
        check(!model(w)->data()["canStartCapture"].toBool() && !model(w)->data()["canStopCapture"].toBool(),
              "Invalid descriptor enabled capture controls");
    }
    for (int mode : {11, 21, 22, 28}) {
        control(mode);
        intents();
        b.startCapture("stereo-parent");
        b.stopCapture();
        check(!b.captureStateKnown() && model(w)->data()["captureStatus"] == "Unknown",
              "Ambiguous capture was guessed");
    }
    control(17);
    choose();
    captureClick("scanStart");
    await([&] { return !b.busy(); });
    check(b.connected() && !b.capturing() && b.error().contains("wire.capture_start"),
          "Rejected Start became active or lost typed error");
    control(31);
    captureClick("scanStop");
    await([&] { return !b.busy(); });
    check(b.capturing() && b.error().contains("wire.capture_stop"),
          "Rejected Stop became idle or lost typed error");
    await([&] { return b.previewCurrent(); });
    control(25);
    check(!b.connected() && b.lastKnownCapturing() && left->hasImage() && cloud->pointCount() == 512,
          "Recording disconnect lost last-known buffers");
    const auto offlineStarts = count("capture_start"), offlineStops = count("capture_stop"),
               offlineData = count("artifact_data"), offlinePreviews = count("preview");
    const auto offlineEpoch = model(w)->data()["projectEpoch"];
    const auto offlineLeft = left->image().constBits(), offlineRight = right->image().constBits();
    for (const auto &mode : {"live", "hybrid"}) {
        w->setProperty("uiMode", mode);
        for (const auto &route : {"acquisition", "scan"}) {
            w->setProperty("workspace", route);
            orbitZoomReset(w, cloud, QString(route) == "scan" ? "scanResetView" : "classicResetView");
            choose();
            intents();
            b.startCapture("stereo-parent");
            b.stopCapture();
            // Disabled Classic controls also remain harmless if their signals are invoked directly.
            QMetaObject::invokeMethod(item(w, "startCapture_stereo-parent"), "clicked");
            QMetaObject::invokeMethod(item(w, "classicStopCapture"), "clicked");
            check(!b.connected() && !b.canStartCapture("stereo-parent") && !b.canStopCapture() &&
                      !model(w)->data()["confirmed"].toBool() && model(w)->selectedDevice().isEmpty() &&
                      model(w)->data()["projectEpoch"] == offlineEpoch &&
                      !item(w, "scanStart")->isEnabled() && !item(w, "scanStop")->isEnabled() &&
                      !item(w, "scanCaptureDevice")->isEnabled() &&
                      !item(w, "startCapture_stereo-parent")->isEnabled() &&
                      !item(w, "classicStopCapture")->isEnabled() && cloud->pointCount() == 512 &&
                      left->image().constBits() == offlineLeft && right->image().constBits() == offlineRight,
                  "Offline inspection changed buffers, freshness, selection or hardware authority");
            capture(w, out, QString("offline-%1-%2-real-wire").arg(mode, route));
        }
    }
    check(count("capture_start") == offlineStarts && count("capture_stop") == offlineStops &&
              count("artifact_data") == offlineData && count("preview") == offlinePreviews,
          "Offline local gestures or prohibited hardware intents issued wire commands/data/preview");
    std::cout << "Offline local inspection: Live/Hybrid Scan/Classic orbit, wheel and Reset; "
                 "extra capture_start=0 capture_stop=0 artifact_data=0 preview=0 "
                 "(existing snapshot polling continues)\n";
    capture(w, out, "recording-disconnected-real-wire");
    control(31);
    await([&] { return b.capturing() && b.previewCurrent(); });
    check(b.canStopCapture() && !b.canStartCapture("stereo-parent") && item(w, "scanStop")->isEnabled() &&
              cloud->pointCount() == 512 && count("artifact_data") == offlineData,
          "Reconnect changed capture guards or reloaded cloud for local inspection");
    orbitZoomReset(w, cloud, "scanResetView");
    control(18);
    choose();
    captureClick("scanStart");
    await([&] { return !b.busy(); });
    check(!b.connected() && !item(w, "scanStart")->isEnabled() && b.error().contains("operation"),
          "Socket closure advertised command success");
    intents();
    b.startCapture("stereo-parent");
    b.stopCapture();
    int heartbeats = 0;
    QTimer heartbeat;
    heartbeat.setInterval(10);
    QObject::connect(&heartbeat, &QTimer::timeout, [&] { ++heartbeats; });
    heartbeat.start();
    control(19);
    choose();
    captureClick("scanStart");
    check(b.busy() && !b.capturing(), "Timed-out Start was optimistic");
    for (int i = 0; i < 8; ++i) {
        intents();
        b.startCapture("stereo-parent");
        b.stopCapture();
    }
    r->setProperty("activeSource", "mock");
    intents();
    await([&] { return !b.busy(); }, 10000);
    check(b.connected() && !b.capturing() && b.error().contains("operation") && !owner->cloud()->isVisible(),
          "Timeout/source change lost conservative state");
    heartbeat.stop();
    check(heartbeats > 50, "Capture timeout blocked the GUI event loop");
    std::cout << "GUI heartbeats during socket timeout: " << heartbeats << '\n';
    r->setProperty("activeSource", "live");
    control(23);
    check(model(w)->selectedDevice().isEmpty() && cloud->pointCount() == 0 && !left->hasImage() &&
              !right->hasImage(),
          "Wire project replacement retained old source buffers");
    control(10);
    await([&] { return cloud->pointCount() == 512; });
    check(count("capture_start") == 4 && count("capture_stop") == 2 && count("artifact_data") == 2,
          "Mutation retried, unauthorized intent transmitted, or cloud loaded on route");
    capture(w, out, "capture-recovered-idle-wire");
}
void native(QQuickWindow *w, Observed &b, const QString &out) {
    for (const auto &source : {"mock", "live", "hybrid"}) {
        w->setProperty("uiMode", source);
        auto loaded = snapshot();
        loaded.cloud = fixtureCloud();
        loaded.cloud_id = "fixture-cloud";
        b.applyResult(loaded);
        b.applyPreview(fixturePreview(b.projectGeneration()));
        w->resize(1536, 1024);
        check(QTest::qWaitForWindowExposed(w, 5000), "Native window not exposed");
        settle();
        bounds(w);
        capture(w, out, "native-normal-" + QString(source));
        w->showMaximized();
        check(QTest::qWaitFor([&] { return w->visibility() == QWindow::Maximized; }, 5000),
              "Native maximize failed");
        settle();
        bounds(w);
        capture(w, out, "native-maximized-" + QString(source));
        w->showNormal();
        settle();
        bounds(w);
        capture(w, out, "native-restored-" + QString(source));
    }
}
} // namespace
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    QQuickStyle::setStyle("Basic");
    qInstallMessageHandler(messages);
    qmlRegisterType<ScanModel>("Mantis.Studio", 1, 0, "ScanModel");
    qmlRegisterType<LiveViews>("Mantis.Studio", 1, 0, "LiveViews");
    qmlRegisterType<DevicesModel>("Mantis.Studio", 1, 0, "DevicesModel");
    qmlRegisterType<HomeModel>("Mantis.Studio", 1, 0, "HomeModel");
    qmlRegisterType<ProjectsModel>("Mantis.Studio", 1, 0, "ProjectsModel");
    qmlRegisterType<ProjectsScrollInput>("Mantis.Studio", 1, 0, "ProjectsScrollInput");
    qmlRegisterType<mantis::render::PointCloudView>("Mantis.Render", 1, 0, "PointCloudView");
    qmlRegisterType<MeasurementView>("Mantis.Render", 1, 0, "MeasurementView");
    try {
        const QString out = argc > 1 ? argv[1] : QDir::tempPath() + "/mantis-scan",
                      task = argc > 2 ? argv[2] : "qml";
        check(QDir().mkpath(out), "Evidence directory unavailable");
        QPointer<QObject> finalOwner, finalCloud, finalLeft, finalRight;
        if (task == "fixtures") {
            mantis::data::write_packet((out + "/cloud.packet").toStdString(), *fixtureCloud());
            mantis::data::write_packet((out + "/frames.packet").toStdString(), *fixtureFrames());
        } else if (task == "model")
            modelTests(), captureModelTests();
        else {
            Observed observed;
            std::unique_ptr<StudioBridge> publicBridge;
            if (task == "wire" || task == "capture-wire")
                publicBridge = std::make_unique<StudioBridge>();
            StudioBridge *bridge = publicBridge ? publicBridge.get() : &observed;
            CalibrationController calibration(std::make_shared<PublicCalibrationClient>(
                mantis::client::Client{mantis::client::Endpoint{}}));
            QQmlApplicationEngine engine;
            engine.rootContext()->setContextProperty("studio", bridge);
            engine.rootContext()->setContextProperty("calibration", &calibration);
            engine.setInitialProperties(
                {{"uiMode", (task == "wire" || task == "capture-wire") ? "live" : "mock"},
                 {"workspace", task == "qml" ? "home" : "scan"}});
            engine.load(QUrl("qrc:/ui/shell/Main.qml"));
            check(!engine.rootObjects().empty(), "Production Scan shell failed to load");
            auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().front());
            check(window, "Window unavailable");
            auto *canonical = qobject_cast<LiveViews *>(item(window, "liveSurfaces"));
            finalOwner = canonical;
            finalCloud = canonical->cloud();
            finalLeft = canonical->left();
            finalRight = canonical->right();
            settle();
            if (task == "qml") {
                // Other workspaces start with Scan eagerly instantiated but still
                // zero-sized. Exercise that lifetime before exposing its layout.
                for (int i = 0; i < 9; ++i) {
                    window->setProperty("uiMode", QStringList{"live", "hybrid", "mock"}[i % 3]);
                    observed.applyResult(snapshot());
                    window->resize(sizes[i]);
                    settle();
                }
                window->setProperty("uiMode", "mock");
                window->setProperty("workspace", "scan");
                settle();
            }
            if (task == "matrix" || task == "hidpi")
                matrix(window, observed, out, task == "hidpi");
            else if (task == "surfaces")
                surfaces(window, observed, out);
            else if (task == "stress")
                stress(window, observed, out);
            else if (task == "wire")
                wire(window, *publicBridge, out);
            else if (task == "capture-wire")
                captureWire(window, *publicBridge, out);
            else if (task == "native")
                native(window, observed, out);
            else {
                interactions(window, observed, out);
                jobPresentation(window, observed, out);
            }
            check(observed.commands == 0, "Scan invoked runtime mutation/data request");
        }
        check(finalOwner.isNull() && finalCloud.isNull() && finalLeft.isNull() && finalRight.isNull(),
              "Canonical objects survived final window/engine destruction");
        check(warnings.empty(), "Qt/QML warning in Scan tests");
        std::cout << "PASS: Scan " << (argc > 2 ? argv[2] : "qml") << '\n';
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        for (const auto &s : warnings)
            std::cerr << s.toStdString() << '\n';
        return 1;
    }
    return 0;
}
#include "studio_scan.moc"
