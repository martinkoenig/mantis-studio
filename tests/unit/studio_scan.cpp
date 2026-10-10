#include "bridge.hpp"
#include "calibration_controller.hpp"
#include "devices_model.hpp"
#include "home_model.hpp"
#include "projects_model.hpp"
#include "projects_scroll.hpp"
#include "scan_model.hpp"
#include "screenshot.hpp"
#include <QAccessible>
#include <QDir>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSet>
#include <QSignalSpy>
#include <QTest>
#include <QWheelEvent>
#include <functional>
#include <iostream>
#include <stdexcept>

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
    using StudioBridge::applyResult;
    int commands{};
    QSet<QObject *> views, previews;
    Q_INVOKABLE void attachView(QObject *v) {
        views.insert(v);
        StudioBridge::attachView(v);
    }
    Q_INVOKABLE void attachPreview(QObject *l, QObject *r) {
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
    p.setProperty("capturing", true);
    p.setProperty("lastKnownCapturing", true);
    p.setProperty("project", "A");
    emit p.changed();
    check(m.data()["captureActive"].toBool() && !m.data()["commandsAllowed"].toBool(),
          "Read-only capture authority failed");
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
                 : QStringList{"mock", "waiting", "live", "stale", "hybrid-live", "hybrid-demo"}) {
            w->setProperty("uiMode", state == "mock"              ? "mock"
                                     : state.startsWith("hybrid") ? "hybrid"
                                                                  : "live");
            r->setProperty("bridge", QVariant::fromValue<QObject *>(state == "waiting" ? nullptr : &b));
            b.applyResult(snapshot());
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
    for (int i = 0; i < 100; ++i) {
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
void native(QQuickWindow *w, Observed &b, const QString &out) {
    for (const auto &source : {"mock", "live", "hybrid"}) {
        w->setProperty("uiMode", source);
        b.applyResult(snapshot());
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
        if (task == "model")
            modelTests();
        else {
            Observed observed;
            std::unique_ptr<StudioBridge> publicBridge;
            if (task == "wire")
                publicBridge = std::make_unique<StudioBridge>();
            StudioBridge *bridge = publicBridge ? publicBridge.get() : &observed;
            CalibrationController calibration(std::make_shared<PublicCalibrationClient>(
                mantis::client::Client{mantis::client::Endpoint{}}));
            QQmlApplicationEngine engine;
            engine.rootContext()->setContextProperty("studio", bridge);
            engine.rootContext()->setContextProperty("calibration", &calibration);
            engine.setInitialProperties({{"uiMode", task == "wire" ? "live" : "mock"},
                                         {"workspace", task == "qml" ? "home" : "scan"}});
            engine.load(QUrl("qrc:/ui/shell/Main.qml"));
            check(!engine.rootObjects().empty(), "Production Scan shell failed to load");
            auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().front());
            check(window, "Window unavailable");
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
            else if (task == "stress")
                stress(window, observed, out);
            else if (task == "wire")
                wire(window, *publicBridge, out);
            else if (task == "native")
                native(window, observed, out);
            else {
                interactions(window, observed, out);
                jobPresentation(window, observed, out);
            }
            check(observed.commands == 0, "Scan invoked runtime mutation/data request");
        }
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
