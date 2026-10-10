#include "bridge.hpp"
#include "calibration_controller.hpp"
#include "devices_model.hpp"
#include "home_model.hpp"
#include "projects_model.hpp"
#include "projects_scroll.hpp"
#include "scan_model.hpp"
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
#include <QSignalSpy>
#include <QTest>
#include <algorithm>
#include <atomic>
#include <iostream>
#include <mutex>
#include <stdexcept>

namespace w = mantis::wire::v1;
namespace {
QStringList warnings;
void messages(QtMsgType type, const QMessageLogContext &, const QString &message) {
    if (type >= QtWarningMsg && !message.startsWith("QStandardPaths:"))
        warnings.push_back(message);
}
void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
class Observed : public StudioBridge {
  public:
    Observed() : StudioBridge(nullptr, false) {}
    using StudioBridge::applyResult;
};
w::Device *device(w::Response &s, const std::string &id, const std::string &cap = {},
                  const std::string &parent = {}) {
    auto *d = s.add_devices();
    d->set_id(id);
    d->set_name(id);
    d->set_parent(parent);
    d->set_plugin_id("org.example.device");
    if (!cap.empty())
        d->add_capabilities(cap);
    return d;
}
StudioResult snapshot(int count = 6) {
    StudioResult r;
    auto &s = r.snapshot.emplace();
    s.set_project_path("/fixture/Devices.mantis");
    auto *d = device(s, "demo-device", "org.mantis.camera.frameset-stream.v1");
    d->set_name("Third-party stereo system");
    d->add_children("left");
    d->add_children("right");
    d->add_children("emitter");
    (*d->mutable_metadata())["vendor.note"] = "<b>Literal metadata</b>";
    device(s, "left", "org.mantis.camera.image-stream.v1", "demo-device");
    device(s, "right", "org.mantis.camera.image-stream.v1", "demo-device");
    device(s, "emitter", "org.mantis.emitter.power-control.v1", "demo-device");
    device(s, "image-only", "org.mantis.camera.image-stream.v1");
    device(s, "unknown", "org.example.radiation.raw.v7")->set_name("Robot labelled unknown sensor");
    for (int i = 6; i < count; ++i)
        device(s, "additional-" + std::to_string(i));
    auto *p = s.add_plugins();
    p->set_id("org.example.device");
    p->set_state("Enabled");
    p->set_version("fixture-1");
    p->set_diagnostic("Plugin initialized; physical readiness not published");
    p->set_kind("device");
    p->set_execution("native");
    auto *e = s.add_events();
    e->set_sequence(18446744073709551615ULL);
    e->set_component("runtime");
    e->set_kind("warning");
    e->set_message("demo-device in arbitrary runtime event text, not device attribution");
    return r;
}
QVariantMap row(const DevicesModel &m, const QString &id) {
    for (const auto &v : m.nodes())
        if (v.toMap()["id"] == id)
            return v.toMap();
    throw std::runtime_error("Device not present in bounded graph");
}
void modelTests() {
    Observed bridge;
    DevicesModel m;
    m.setBridge(&bridge);
    check(!m.data()["hasSnapshot"].toBool() && !m.data()["confirmed"].toBool(), "Initial state invented");
    auto s = snapshot();
    bridge.applyResult(s);
    check(m.data()["confirmed"].toBool() && m.nodes().size() == 6, "Full graph / signal seam missing");
    check(row(m, "left")["depth"] == 1 && row(m, "left")["group"] == "Acquisition parents",
          "Child not associated with logical parent");
    check(row(m, "unknown")["group"] == "Other devices", "Name classified as motion device");
    m.selectDevice("demo-device");
    check(m.calibrationIdentity() == "demo-device", "Exact eligible identity unavailable");
    CalibrationController controller(
        std::make_shared<PublicCalibrationClient>(mantis::client::Client{mantis::client::Endpoint{}}));
    controller.observeSnapshot(*s.snapshot);
    check(controller.devices().size() == 1 && row(m, "demo-device")["eligible"].toBool(),
          "Shared eligibility diverged");
    for (const auto &id : {"left", "emitter", "image-only", "unknown"}) {
        m.selectDevice(id);
        check(m.calibrationIdentity().isEmpty(), "Component / unknown acquired calibration authority");
    }
    m.selectDevice("demo-device");
    QSignalSpy notifications(&m, &DevicesModel::changed), graph(&m, &DevicesModel::graphChanged);
    for (int i = 0; i < 100; ++i)
        bridge.applyResult(s);
    check(notifications.empty() && graph.empty(), "Identical graph churns presentation / delegates");
    auto plugin = s;
    plugin.snapshot->mutable_plugins(0)->set_state("Failed");
    plugin.snapshot->mutable_plugins(0)->set_diagnostic("Exact plugin failure");
    bridge.applyResult(plugin);
    check(row(m, "demo-device")["pluginInfo"].toMap()["state"] == "Failed",
          "Changed exact plugin state lost");
    auto noMatch = s;
    noMatch.snapshot->mutable_devices(0)->set_plugin_id("unknown-plugin");
    bridge.applyResult(noMatch);
    check(row(m, "demo-device")["pluginInfo"].toMap()["state"] == "Unknown", "Unknown plugin fabricated");
    StudioResult failure;
    failure.issues.push_back({"snapshot", "Confirmation refused",
                              mantis::Error{mantis::Status::io, "Confirmation refused", "wire.fixture"}});
    bridge.applyResult(failure);
    check(!m.data()["confirmed"].toBool() && m.nodes().size() == 6 && m.calibrationIdentity().isEmpty(),
          "Stale graph lost or actionable");
    const auto issue = m.data()["issues"].toList().front().toMap();
    check(issue["phase"] == "snapshot" && issue["component"] == "wire.fixture" &&
              issue["code"] == static_cast<int>(mantis::Status::io),
          "Structured issue changed");
    bridge.applyResult(s);
    check(m.calibrationIdentity() == "demo-device", "Confirmed identity failed to reconcile");
    auto removed = s;
    removed.snapshot->clear_devices();
    bridge.applyResult(removed);
    check(m.data()["selectedId"].toString().isEmpty() && m.nodes().empty() && m.data()["confirmed"].toBool(),
          "Confirmed empty retained selection");
    bridge.applyResult(s);
    check(m.data()["selectedId"].toString().isEmpty(), "Reappearance silently selected a device");
    m.selectDevice("demo-device");
    auto switched = s;
    switched.snapshot->set_project_path("/fixture/Other.mantis");
    bridge.applyResult(switched);
    check(m.data()["selectedId"].toString().isEmpty(), "Project context retained action selection");
    // Multiple legitimate parents, childless FrameSet and emitter-only composite.
    auto topology = s;
    auto *second = device(*topology.snapshot, "second-parent", "org.mantis.camera.frameset-stream.v1");
    second->add_children("second-image");
    device(*topology.snapshot, "second-image", "org.mantis.camera.image-stream.v1", "second-parent");
    device(*topology.snapshot, "childless", "org.mantis.camera.frameset-stream.v1");
    auto *emitterParent =
        device(*topology.snapshot, "emitter-parent", "org.mantis.camera.frameset-stream.v1");
    emitterParent->add_children("emitter-only");
    device(*topology.snapshot, "emitter-only", "org.mantis.emitter.power-control.v1", "emitter-parent");
    bridge.applyResult(topology);
    controller.observeSnapshot(*topology.snapshot);
    check(controller.devices().size() == 2 && m.nodes().size() == 11, "Multiple parent graph incomplete");
    m.selectDevice("second-parent");
    check(m.calibrationIdentity() == "second-parent", "Second parent's exact authority lost");
    for (const auto &id : {"childless", "emitter-parent", "second-image"}) {
        m.selectDevice(id);
        check(m.calibrationIdentity().isEmpty(), "Unsupported topology gained device-bound action");
    }
    auto duplicatePlugin = s;
    *duplicatePlugin.snapshot->add_plugins() = duplicatePlugin.snapshot->plugins(0);
    bridge.applyResult(duplicatePlugin);
    check(row(m, "demo-device")["pluginInfo"].toMap()["state"] == "Unknown", "Ambiguous plugin aliased");
    // Canonical IDs remain distinct when display text truncates to the same prefix.
    auto identities = s;
    const std::string prefix(200, 'a');
    device(*identities.snapshot, prefix + "A");
    device(*identities.snapshot, prefix + "B");
    bridge.applyResult(identities);
    m.selectDevice(QString::fromStdString(prefix + "B"));
    check(m.data()["selectedId"] == QString::fromStdString(prefix + "B"),
          "Canonical identity truncated / aliased");
    auto hugeRelations = s;
    auto *huge = hugeRelations.snapshot->mutable_devices(0);
    for (int i = 0; i < 1000; ++i) {
        huge->add_children("missing-" + std::to_string(i));
        huge->add_capabilities("org.example.extra." + std::to_string(i) + ".v1");
    }
    bridge.applyResult(hugeRelations);
    check(row(m, "demo-device")["children"].toStringList().size() <= 64 &&
              row(m, "demo-device")["capabilities"].toStringList().size() <= 32 &&
              m.calibrationIdentity().isEmpty(),
          "Oversized relationships / capabilities unbounded");
    auto adversarial = s;
    device(*adversarial.snapshot, "demo-device");
    device(*adversarial.snapshot, "");
    device(*adversarial.snapshot, "orphan", {}, "missing");
    auto *a = device(*adversarial.snapshot, "cycle-a", {}, "cycle-b");
    a->add_children("cycle-b");
    auto *b = device(*adversarial.snapshot, "cycle-b", {}, "cycle-a");
    b->add_children("cycle-a");
    auto *self = device(*adversarial.snapshot, "self", {}, "self");
    self->add_children("self");
    auto *wrong = device(*adversarial.snapshot, "contradictory");
    wrong->add_children("left");
    bridge.applyResult(adversarial);
    check(m.nodes().size() == adversarial.snapshot->devices_size() && !m.data()["graphTrusted"].toBool(),
          "Malformed graph lost descriptors / trusted");
    check(!row(m, "demo-device")["selectable"].toBool(), "Duplicate identity silently aliased");
    m.selectDevice("image-only");
    check(m.calibrationIdentity().isEmpty(), "Malformed graph granted authority");
    auto deep = s;
    for (int i = 0; i < 20; ++i) {
        auto *n =
            device(*deep.snapshot, "deep-" + std::to_string(i), {}, i ? "deep-" + std::to_string(i - 1) : "");
        if (i < 19)
            n->add_children("deep-" + std::to_string(i + 1));
    }
    bridge.applyResult(deep);
    check(!m.data()["anomalies"].toStringList().empty(), "Deep graph not reported");
    for (const auto &v : m.nodes())
        check(v.toMap()["depth"].toInt() <= 8, "Unbounded depth");
    auto hostile = s;
    auto *h = hostile.snapshot->mutable_devices(0);
    h->set_name(std::string(20000, 'x') + "\xe2\x80\xae");
    h->add_capabilities("org.example.unknown.v3");
    h->add_capabilities("org.example.unknown.v3");
    (*h->mutable_metadata())["<b>key</b>"] =
        std::string("literal\n\xe2\x80\xae\xff") + std::string(10000, 'y');
    for (int i = 0; i < 1000; ++i)
        (*h->mutable_metadata())["key-" + std::to_string(i)] = std::string(1000, 'a');
    device(*hostile.snapshot, std::string("invalid\xff"));
    bridge.applyResult(hostile);
    check(m.data()["limited"].toBool() && row(m, "demo-device")["name"].toString().size() <= 129,
          "Hostile strings unbounded");
    check(row(m, "demo-device")["metadata"].toList().size() <= 32, "Metadata unbounded");
    check(row(m, "demo-device")["capabilities"].toStringList().count("org.example.unknown.v3") == 1,
          "Duplicate capability lost normalization");
    auto many = snapshot(12000);
    QElapsedTimer elapsed;
    elapsed.start();
    bridge.applyResult(many);
    m.selectDevice("demo-device");
    check(m.nodes().size() == 512 && m.data()["count"] == 12000 && m.calibrationIdentity().isEmpty(),
          "Oversized graph not bounded/non-actionable");
    notifications.clear();
    graph.clear();
    for (int i = 0; i < 100; ++i)
        m.observeSnapshot(*many.snapshot);
    check(notifications.empty() && graph.empty(), "Identical large projections emitted changes");
    std::cout << "Bounded 12000-descriptor input: 100 normalized updates " << elapsed.elapsed()
              << " ms; no timer/thread/I/O in model\n";
    auto *temporary = new Observed;
    m.setBridge(temporary);
    temporary->applyResult(s);
    delete temporary;
    check(!m.bridge() && m.nodes().empty() && !m.data()["confirmed"].toBool(),
          "Bridge lifetime guard failed");
}
// Existing controller reads are injectable. Every mutation is counted and fails the test.
class FixtureCalibration final : public CalibrationClient {
  public:
    mutable std::atomic<int> mutations{}, selections{};
    mutable std::mutex mutex;
    w::Response current = *::snapshot().snapshot;
    std::vector<w::CalibrationEntry> list() const override {
        return {};
    }
    w::CalibrationInfo info(const std::string &) const override {
        return {};
    }
    w::Response snapshot() const override {
        std::lock_guard lock(mutex);
        return current;
    }
    w::CalibrationEntry create(const w::CalibrationTargetSpecification &) const override {
        ++mutations;
        return {};
    }
    std::string dataset(const w::CalibrationDatasetBuild &) const override {
        ++mutations;
        return {};
    }
    std::string camera(const w::CalibrationCameraSolve &) const override {
        ++mutations;
        return {};
    }
    std::string rig(const w::CalibrationRigSolve &) const override {
        ++mutations;
        return {};
    }
    std::optional<w::ActiveCalibrationBinding> active(const std::string &) const override {
        ++selections;
        return {};
    }
    void activate(const std::string &, const std::string &) const override {
        ++mutations;
    }
    void clear(const std::string &) const override {
        ++mutations;
    }
    std::string start(const std::string &) const override {
        ++mutations;
        return {};
    }
    void stop(const std::string &) const override {
        ++mutations;
    }
    void cancel(const std::string &) const override {
        ++mutations;
    }
};
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
void settle() {
    QTest::qWait(40);
}
void click(QQuickWindow *w, const QString &name, bool keyboard = false) {
    auto *b = item(w, name);
    check(b->isVisible() && b->isEnabled(), ("Unavailable control " + name).toUtf8().constData());
    b->forceActiveFocus(Qt::TabFocusReason);
    settle();
    if (keyboard)
        QTest::keyClick(w, Qt::Key_Space);
    else
        QTest::mouseClick(w, Qt::LeftButton, Qt::NoModifier,
                          b->mapToScene(QPointF(b->width() / 2, b->height() / 2)).toPoint());
    settle();
}
DevicesModel *model(QQuickWindow *w) {
    return qobject_cast<DevicesModel *>(
        item(w, "devicesWorkspace")->property("liveModel").value<QObject *>());
}
const QList<QSize> sizes{{1080, 720},  {1280, 720},  {1366, 768},  {1440, 900}, {1536, 1024},
                         {1920, 1080}, {2560, 1440}, {3440, 1440}, {3840, 2160}};
void capture(QQuickWindow *w, const QString &out, const QString &name) {
    const auto image = w->grabWindow();
    check(!image.isNull(), "Missing rendered window");
    check(image.size() ==
              QSize(qRound(w->width() * w->devicePixelRatio()), qRound(w->height() * w->devicePixelRatio())),
          "Incorrect actual Qt DPR geometry");
    check(image.save(out + "/" + name + ".png"), "Cannot save Qt screenshot");
}
void bounds(QQuickWindow *w) {
    // Layout polish is asynchronous. Synchronize with a real rendered frame,
    // rather than assuming a fixed qWait has made resized geometry authoritative.
    check(!w->grabWindow().isNull(), "Responsive transition has no rendered frame");
    auto *r = item(w, "devicesWorkspace");
    for (const auto &name :
         {"devicesPanes", "devicesInspector", "devicesNavigator", "devicesOverviewViewport", "devicesAdd",
          "devicesPresets", "devicesRefresh", "openCalibration"}) {
        auto *p = item(w, name);
        if (!p->isVisible())
            continue;
        const auto rect = p->mapRectToItem(r, p->boundingRect());
        check(rect.left() >= -1 && rect.right() <= r->width() + 1 && rect.top() >= -1 &&
                  rect.bottom() <= r->height() + 1,
              ("Clipped workspace control " + QString(name) + " rect=" + QString::number(rect.left()) + "," +
               QString::number(rect.top()) + "," + QString::number(rect.width()) + "," +
               QString::number(rect.height()) + " workspace=" + QString::number(r->width()) + "x" +
               QString::number(r->height()))
                  .toUtf8()
                  .constData());
    }
    if (r->property("multiPane").toBool()) {
        auto *inspector = item(w, "devicesInspector");
        auto *panes = item(w, "devicesPanes");
        const auto rect = inspector->mapRectToItem(panes, inspector->boundingRect());
        check(std::abs(rect.right() - panes->width()) <= 1, "Inspector detached from usable right edge");
    }
    for (const auto &name : {"devicesOverviewViewport", "devicesInspectorViewport"}) {
        auto *p = item(w, name);
        if (p->isVisible())
            check(p->property("contentWidth").toDouble() <= p->width() + 1, "Horizontal overflow");
    }
}
void controls(QQuickWindow *w, FixtureCalibration &f) {
    w->resize(1536, 1024);
    settle();
    click(w, "devicesTabSettings");
    const auto before = f.mutations.load();
    for (const auto &name :
         {"devicesAdd", "devicesPresets", "devicesIdentify", "devicesFirmware", "devicesCameraResolution",
          "devicesCameraExposure", "devicesLaserPower", "devicesSync", "devicesSavePreset"}) {
        auto *b = item(w, name);
        check(!b->isEnabled(), "Unsupported control enabled");
        QSignalSpy clicks(b, SIGNAL(clicked()));
        QTest::mouseClick(w, Qt::LeftButton, Qt::NoModifier,
                          b->mapToScene(QPointF(b->width() / 2, b->height() / 2)).toPoint());
        b->forceActiveFocus();
        QTest::keyClick(w, Qt::Key_Return);
        QTest::keyClick(w, Qt::Key_Space);
        auto *a = QAccessible::queryAccessibleInterface(b);
        check(a && !a->text(QAccessible::Description).isEmpty(),
              "Disabled action lacks accessible explanation");
        if (auto *action = a->actionInterface())
            action->doAction(QAccessibleActionInterface::pressAction());
        check(clicks.empty(), "Disabled action succeeded via input/accessibility");
        // Direct signal is adversarial too; unavailable actions have no handler issuing commands.
        QMetaObject::invokeMethod(b, "clicked");
    }
    check(f.mutations.load() == before, "Disabled control mutated calibration/runtime");
}
void interactions(QQuickWindow *w, Observed &b, FixtureCalibration &f, const QString &out) {
    w->setProperty("uiMode", "live");
    b.applyResult(snapshot());
    settle();
    model(w)->selectDevice("demo-device");
    w->resize(1536, 1024);
    settle();
    QPointer<QQuickItem> acquisition = item(w, "acquisitionWorkspace");
    QPointer<QQuickItem> calibrationView = item(w, "calibrationWorkspace");
    auto *r = item(w, "devicesWorkspace");
    auto *inspector = item(w, "devicesInspector");
    const auto pos = inspector->position();
    const auto size = inspector->size();
    QSignalSpy intents(r, SIGNAL(calibrate(QString)));
    for (const auto &tab : {"Settings", "Calibration", "Diagnostics", "Info"}) {
        click(w, "devicesTab" + QString(tab), true);
        check(inspector->position() == pos && inspector->size() == size &&
                  model(w)->data()["selectedId"] == "demo-device",
              "Tab changed geometry / selection");
        capture(w, out, "inspector-" + QString(tab));
    }
    click(w, "devicesTabCalibration");
    click(w, "devicesCalibrate", true);
    check(intents.size() == 1 && intents.front().front() == "demo-device" &&
              w->property("workspace") == "calibration",
          "Exact calibration route failed");
    for (int i = 0; i < 7; ++i)
        check(item(w, "calibrationStage" + QString::number(i)), "Existing seven-stage hook missing");
    check(f.mutations.load() == 0, "Opening Calibration initiated mutation");
    w->setProperty("workspace", "devices");
    settle();
    for (const auto &id : {"left", "emitter", "image-only", "unknown"}) {
        model(w)->selectDevice(id);
        settle();
        check(!item(w, "devicesCalibrate")->isEnabled(), "Ineligible selected action enabled");
        QMetaObject::invokeMethod(item(w, "devicesCalibrate"), "clicked");
    }
    check(intents.size() == 1, "Ineligible direct signal routed identity");
    b.applyResult({});
    settle();
    model(w)->selectDevice("demo-device");
    QMetaObject::invokeMethod(item(w, "devicesCalibrate"), "clicked");
    check(intents.size() == 1, "Stale selected route authorized");
    b.applyResult(snapshot());
    model(w)->selectDevice("demo-device");
    w->setProperty("uiMode", "hybrid");
    settle();
    b.applyResult(snapshot());
    model(w)->selectDevice("demo-device");
    click(w, "devicesDemoSource");
    check(r->property("selectedId") == "demo-device" && !item(w, "devicesCalibrate")->isEnabled(),
          "Colliding demo ID gained authority");
    QMetaObject::invokeMethod(item(w, "devicesCalibrate"), "clicked");
    check(intents.size() == 1, "Demo ID entered controller");
    click(w, "devicesLiveSource");
    check(model(w)->data()["selectedId"] == "demo-device", "Hybrid source changed live selection");
    controls(w, f);
    w->resize(1080, 720);
    settle();
    click(w, "devicesCompactNavigator");
    click(w, "deviceRow_1", true);
    check(model(w)->data()["selectedId"] == "left", "Keyboard navigator identity changed");
    QTest::keyClick(w, Qt::Key_Down);
    QTest::keyClick(w, Qt::Key_Space);
    settle();
    check(model(w)->data()["selectedId"] == "right", "Arrow / Space navigator failed");
    QTest::keyClick(w, Qt::Key_Escape);
    settle();
    check(r->property("compactPane") == "Overview", "Compact Escape failed");
    click(w, "openCalibration");
    check(w->property("workspace") == "calibration", "Offline route missing");
    w->setProperty("workspace", "devices");
    for (const auto &route : {"home", "projects", "scan", "calibration", "devices"}) {
        w->setProperty("workspace", route);
        settle();
        check(!w->grabWindow().isNull(), "Legacy route failed");
    }
    check(acquisition && calibrationView && acquisition == item(w, "acquisitionWorkspace") &&
              calibrationView == item(w, "calibrationWorkspace"),
          "Navigation rebuilt existing workflow instances");
    w->setProperty("uiMode", "mock");
    settle();
    check(!item(w, "devicesRefresh")->isEnabled(), "Mock refresh enabled");
    check(!item(w, "devicesScan")->isEnabled(), "Demo starts acquisition");
    controls(w, f);
}
void matrix(QQuickWindow *w, Observed &b, const QString &out, bool dpi) {
    for (const auto &size : sizes) {
        w->resize(size);
        for (const auto &state : dpi ? QStringList{"mock", "live", "hybrid"}
                                     : QStringList{"mock", "live", "hybrid", "empty", "waiting", "stale",
                                                   "third-party", "hybrid-stale"}) {
            w->setProperty("uiMode", state == "mock"              ? "mock"
                                     : state.startsWith("hybrid") ? "hybrid"
                                                                  : "live");
            auto *r = item(w, "devicesWorkspace");
            if (state == "waiting") {
                r->setProperty("bridge", QVariant::fromValue<QObject *>(nullptr));
            } else {
                r->setProperty("bridge", QVariant::fromValue<QObject *>(&b));
                auto s = snapshot();
                if (state == "empty")
                    s.snapshot->clear_devices();
                b.applyResult(s);
                if (state != "empty")
                    model(w)->selectDevice(state == "third-party" ? "unknown" : "demo-device");
                if (state == "stale" || state == "hybrid-stale")
                    b.applyResult({});
            }
            settle();
            bounds(w);
            capture(w, out,
                    state + "-" + QString::number(size.width()) + "x" + QString::number(size.height()));
            if (!r->property("multiPane").toBool() && !dpi) {
                click(w, "devicesCompactNavigator");
                bounds(w);
                capture(w, out, state + "-navigator-" + QString::number(size.width()));
                click(w, "devicesCompactInspector");
                bounds(w);
                capture(w, out, state + "-inspector-" + QString::number(size.width()));
                click(w, "devicesCompactOverview");
            }
            std::cout << state.toStdString() << " " << size.width() << "x" << size.height()
                      << " dpr=" << w->devicePixelRatio() << "\n";
        }
    }
    item(w, "devicesWorkspace")->setProperty("bridge", QVariant::fromValue<QObject *>(&b));
}
void stress(QQuickWindow *w, Observed &b, const QString &out) {
    w->setProperty("uiMode", "live");
    b.applyResult(snapshot(100));
    model(w)->selectDevice("demo-device");
    w->resize(1536, 1024);
    settle();
    QPointer<QQuickItem> inspector = item(w, "devicesInspector"),
                         overview = item(w, "devicesOverviewViewport");
    QPointer<QQuickItem> first = item(w, "deviceRow_0");
    for (int i = 0; i < 150; ++i) {
        w->resize(sizes[i % sizes.size()]);
        auto *r = item(w, "devicesWorkspace");
        r->setProperty("compactPane", i % 2 ? "Overview" : "Inspector");
        inspector->setProperty("tab", QStringList{"Settings", "Calibration", "Diagnostics", "Info"}[i % 4]);
        model(w)->selectDevice(i % 2 ? "left" : "demo-device");
        if (i % 5 == 0)
            b.applyResult(snapshot(100));
        settle();
        bounds(w);
        check(inspector && overview && first, "Resize / selection destroyed persistent delegates");
    }
    w->resize(1080, 720);
    settle();
    item(w, "devicesWorkspace")->setProperty("compactPane", "All Devices");
    settle();
    auto *list = item(w, "devicesNavigatorList");
    item(w, "deviceRow_0")->forceActiveFocus(Qt::TabFocusReason);
    for (int i = 0; i < 95; ++i) {
        QTest::keyClick(w, Qt::Key_Down);
        settle();
    }
    auto *focus = w->activeFocusItem();
    check(focus && focus->objectName() == "deviceRow_95", "Virtual navigator keyboard traversal lost focus");
    const auto rect = focus->mapRectToItem(list, focus->boundingRect());
    check(rect.top() >= -1 && rect.bottom() <= list->height() + 1,
          "Focused descriptor not revealed in navigator");
    capture(w, out, "navigator-keyboard-last");
    for (int i = 0; i < 30; ++i) {
        w->setProperty("uiMode", i % 3 == 0 ? "mock" : i % 3 == 1 ? "hybrid" : "live");
        b.applyResult(snapshot(i % 2 ? 6 : 100));
        w->resize(sizes[i % sizes.size()]);
        settle();
        bounds(w);
    }
    std::cout << "150 persistent-pane/selection/resize transitions + 95 keyboard rows + 30 source/graph "
                 "transitions\n";
}
void native(QQuickWindow *w, Observed &b, const QString &out) {
    for (const auto &source : QStringList{"mock", "live", "hybrid"}) {
        w->setProperty("uiMode", source);
        b.applyResult(snapshot());
        if (source != "mock")
            model(w)->selectDevice("demo-device");
        w->resize(1536, 1024);
        check(QTest::qWaitForWindowExposed(w, 5000), "Native window not exposed");
        settle();
        bounds(w);
        capture(w, out, "native-normal-" + source);
        const auto normal = w->size();
        auto *button = item(w, "openCalibration");
        button->forceActiveFocus(Qt::TabFocusReason);
        w->showMaximized();
        check(QTest::qWaitFor([&] { return w->isExposed() && w->visibility() == QWindow::Maximized; }, 5000),
              "Native maximize unavailable");
        settle();
        capture(w, out, "native-maximized-" + source);
        bounds(w);
        check(button->hasActiveFocus(), "Native maximize lost focus");
        w->showNormal();
        check(QTest::qWaitFor([&] { return w->isExposed() && w->size() == normal; }, 5000),
              "Native restore unavailable");
        settle();
        capture(w, out, "native-restored-" + source);
        bounds(w);
        check(button->hasActiveFocus(), "Native restore lost focus");
        std::cout << "Native " << source.toStdString() << " normal/maximized/restored; DPR "
                  << w->devicePixelRatio() << '\n';
    }
}
void wire(QQuickWindow *w, StudioBridge &b, FixtureCalibration &f, const QString &out) {
    check(QTest::qWaitFor([&] { return model(w)->data()["confirmed"].toBool(); }, 5000),
          "Public-wire first snapshot failed");
    model(w)->selectDevice("demo-device");
    check(model(w)->calibrationIdentity() == "demo-device", "Wire graph authority failed");
    w->resize(1536, 1024);
    settle();
    capture(w, out, "wire-confirmed");
    for (const auto &tab : {"Info", "Diagnostics", "Calibration", "Settings"})
        click(w, "devicesTab" + QString(tab));
    controls(w, f);
    mantis::client::Client audit;
    auto mode = [&](uint64_t n) {
        w::Request request;
        request.mutable_events()->set_after(n);
        (void)audit.call(request);
        b.refresh();
    };
    mode(1);
    check(QTest::qWaitFor([&] { return !b.connected() && !b.busy(); }, 5000), "Wire disconnect missing");
    check(model(w)->nodes().size() == 6 && model(w)->calibrationIdentity().isEmpty(),
          "Wire stale data/action failed");
    capture(w, out, "wire-stale");
    mode(2);
    check(QTest::qWaitFor([&] { return !b.connected() && b.error().contains("Invalid local access token"); },
                          5000),
          "Wire auth failure lost");
    capture(w, out, "wire-auth");
    mode(3);
    check(QTest::qWaitFor([&] { return b.connected() && model(w)->data()["count"] == 0; }, 5000),
          "Wire confirmed empty failed");
    check(model(w)->data()["selectedId"].toString().isEmpty(), "Wire churn retained selection");
    mode(4);
    check(QTest::qWaitFor([&] { return b.connected() && model(w)->data()["count"] == 6; }, 5000),
          "Wire recovery failed");
    check(model(w)->data()["selectedId"].toString().isEmpty(), "Wire recovery silently selected");
    capture(w, out, "wire-recovery");
    mode(5);
    check(QTest::qWaitFor(
              [&] { return row(*model(w), "demo-device")["pluginInfo"].toMap()["state"] == "Failed"; }, 5000),
          "Wire plugin failure not reflected");
    mode(6);
    check(QTest::qWaitFor([&] { return model(w)->data()["limited"].toBool(); }, 5000),
          "Wire hostile descriptor bounds failed");
    model(w)->selectDevice("unknown");
    check(model(w)->calibrationIdentity().isEmpty() && row(*model(w), "unknown")["metadata"].toList().empty(),
          "Wire hostile metadata gained authority / unbounded rows");
    click(w, "devicesTabDiagnostics");
    capture(w, out, "wire-hostile");
    std::cout << "Public Client/StudioBridge snapshots, structured auth/failure, descriptor churn and "
                 "recovery; read-only events fixture controls only\n";
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
        const QString out = argc > 1 ? argv[1] : QDir::tempPath() + "/mantis-devices";
        const QString task = argc > 2 ? argv[2] : "qml";
        check(QDir().mkpath(out), "Evidence directory unavailable");
        if (task == "model")
            modelTests();
        else {
            Observed observed;
            std::unique_ptr<StudioBridge> publicBridge;
            if (task == "wire")
                publicBridge = std::make_unique<StudioBridge>();
            auto f = std::make_shared<FixtureCalibration>();
            CalibrationController calibration(f);
            auto *bridge = publicBridge ? publicBridge.get() : &observed;
            QObject::connect(bridge, &StudioBridge::snapshotReady, &calibration,
                             &CalibrationController::observeSnapshot);
            QQmlApplicationEngine engine;
            engine.rootContext()->setContextProperty("studio", bridge);
            engine.rootContext()->setContextProperty("calibration", &calibration);
            engine.setInitialProperties(
                {{"uiMode", task == "wire" ? "live" : "mock"}, {"workspace", "devices"}});
            engine.load(QUrl("qrc:/ui/shell/Main.qml"));
            check(!engine.rootObjects().empty(), "Devices shell failed to load");
            auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().front());
            check(window, "Qt window unavailable");
            settle();
            if (task == "matrix" || task == "hidpi")
                matrix(window, observed, out, task == "hidpi");
            else if (task == "stress")
                stress(window, observed, out);
            else if (task == "native")
                native(window, observed, out);
            else if (task == "wire")
                wire(window, *publicBridge, *f, out);
            else
                interactions(window, observed, *f, out);
            check(f->mutations.load() == 0, "Devices initiated runtime mutations");
        }
        for (const auto &warning : warnings)
            std::cerr << warning.toStdString() << '\n';
        check(warnings.empty(), "Qt/QML warnings in Devices tests");
        std::cout << "PASS: Devices " << (argc > 2 ? argv[2] : "qml") << '\n';
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        for (const auto &warning : warnings)
            std::cerr << warning.toStdString() << '\n';
        return 1;
    }
    return 0;
}
