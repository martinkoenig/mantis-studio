#include "bridge.hpp"
#include "calibration_controller.hpp"
#include "home_model.hpp"
#include <QAccessible>
#include <QDir>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QImage>
#include <QJSValue>
#include <QList>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlProperty>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTest>
#include <QThread>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
QStringList warnings;
void messages(QtMsgType type, const QMessageLogContext &, const QString &message) {
    if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg)
        if (!message.startsWith("QStandardPaths:"))
            warnings.push_back(message);
}
void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
QVariantMap row(const QVariantMap &data, const char *key, qsizetype i = 0) {
    const auto list = data.value(key).toList();
    require(i >= 0 && i < list.size(), "Missing expected presentation row");
    return list[i].toMap();
}
QQuickItem *find(QQuickItem *parent, const QString &name) {
    if (parent->objectName() == name)
        return parent;
    for (auto *child : parent->childItems())
        if (auto *item = find(child, name))
            return item;
    return nullptr;
}
QQuickItem *item(QQuickWindow *window, const QString &name) {
    auto *result = find(window->contentItem(), name);
    require(result, ("Missing item " + name).toLocal8Bit().constData());
    return result;
}
void settle() {
    QTest::qWait(35);
}
void click(QQuickWindow *window, const QString &name, bool keyboard = false) {
    auto *button = item(window, name);
    require(button->isVisible() && button->isEnabled(), "CTA unavailable");
    if (keyboard) {
        button->forceActiveFocus(Qt::TabFocusReason);
        // Exercise actual tab traversal even when a persistent control retained mouse focus.
        QTest::keyClick(window, Qt::Key_Tab);
        QTest::keyClick(window, Qt::Key_Backtab, Qt::ShiftModifier);
        require(button->property("visualFocus").toBool(),
                ("CTA keyboard focus invisible: " + name +
                 ", active=" + QString::number(button->hasActiveFocus()) +
                 ", reason=" + button->property("focusReason").toString())
                    .toLocal8Bit()
                    .constData());
        QTest::keyClick(window, Qt::Key_Space);
    } else {
        auto point = button->mapToScene(QPointF(button->width() / 2, button->height() / 2));
        require(window->contentItem()->contains(point), "CTA clipped outside viewport");
        QTest::mouseMove(window, point.toPoint());
        require(button->property("hovered").toBool(), "CTA hover absent");
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point.toPoint());
    }
    settle();
}
class ObservedBridge : public StudioBridge {
  public:
    ObservedBridge() : StudioBridge(nullptr, false) {}
    using StudioBridge::applyResult;
    using StudioBridge::collectResult;
};
} // namespace
class HomeStub : public QObject {
    Q_OBJECT
  public:
    void publish() {
        emit changed();
    }
  signals:
    void changed();
};
namespace {
void presentation() {
    HomeStub stub;
    HomeModel model;
    model.setBridge(&stub);
    require(!model.data()["confirmed"].toBool(), "Missing connection assumed confirmed");
    require(model.data()["project"] == "Project path unavailable", "Missing path invented");
    require(!model.data()["devicesCount"].isValid() && !model.data()["devicesAvailable"].toBool(),
            "Absent device list fabricated zero");
    for (const char *key : {"devices", "jobs", "artifacts", "events"})
        require(model.data()[key].toList().empty(), "Absent list invented");
    stub.setProperty("busy", true);
    stub.publish();
    require(model.data()["freshness"] == "Awaiting runtime confirmation", "Initial loading state missing");
    stub.setProperty("busy", false);
    stub.setProperty("connected", "true");
    stub.publish();
    require(!model.data()["confirmed"].toBool(), "Malformed connection became authoritative");
    stub.setProperty("connected", true);
    stub.setProperty("project", "/work/<b>literal</b>/Fixture.mantis");
    stub.setProperty("devices", QVariantList{QVariantMap{
                                    {"id", "camera"},
                                    {"name", "Fixture camera"},
                                    {"capabilities", QStringList{"org.mantis.camera.image-stream.v1"}}}});
    QVariantList jobs;
    for (auto state : {"Running", "Queued", "Failed", "Cancelled"})
        jobs.push_back(QVariantMap{{"id", QString::number(jobs.size())},
                                   {"name", QString(state) + " job"},
                                   {"state", state},
                                   {"progress", 0.64},
                                   {"diagnostics", "fixture diagnostic"}});
    stub.setProperty("jobs", jobs);
    stub.setProperty("artifacts", QVariantList{QVariantMap{
                                      {"id", "raw"},
                                      {"type", "org.mantis.RawCapture"},
                                      {"state", "RECOVERABLE"},
                                      {"chunks", QVariant::fromValue(quint64{18446744073709551615ULL})}}});
    stub.setProperty("diagnostics", QVariantList{QVariantMap{{"sequence", qulonglong{4}},
                                                             {"component", "capture"},
                                                             {"kind", "info"},
                                                             {"message", "Earlier"}},
                                                 QVariantMap{{"sequence", qulonglong{5}},
                                                             {"component", "job"},
                                                             {"kind", "error"},
                                                             {"message", "Later"}}});
    stub.publish();
    require(model.data()["confirmed"].toBool() && model.data()["hasSnapshot"].toBool(),
            "Confirmed snapshot missing");
    require(model.data()["projectName"] == "Fixture.mantis", "Project basename wrong");
    require(row(model.data(), "devices")["state"] == "Discovered · readiness unknown",
            "Discovery became readiness");
    require(row(model.data(), "devices")["detail"] == "org.mantis.camera.image-stream.v1",
            "Capabilities lost");
    require(row(model.data(), "jobs")["progressText"] == "64%", "Valid progress wrong");
    require(row(model.data(), "jobs", 3)["state"] == "Cancelled", "Cancelled conflated");
    require(row(model.data(), "artifacts")["detail"].toString().startsWith("18446744073709551615"),
            "uint64 chunks rounded");
    require(row(model.data(), "events")["sequence"] == "5", "Events not ordered by sequence");
    QSignalSpy updates(&model, &HomeModel::changed);
    for (int i = 0; i < 100; ++i)
        stub.publish();
    require(updates.empty(), "Unchanged snapshots rebuild Home");
    stub.setProperty("errorDetails", QVariantList{QVariantMap{{"phase", "operation"},
                                                              {"kind", "failure"},
                                                              {"code", 6},
                                                              {"component", "pipeline"},
                                                              {"message", "Rejected"}}});
    stub.publish();
    require(model.data()["confirmed"].toBool() && row(model.data(), "issues")["code"] == 6,
            "Operation failure lost cause or connection");
    stub.setProperty("connected", false);
    stub.publish();
    require(model.data()["freshness"] == "Last known · current state unconfirmed" &&
                model.data()["projectName"] == "Fixture.mantis",
            "Cached snapshot not labelled stale");
    stub.setProperty("connected", true);
    stub.setProperty("devices", QVariantList{});
    stub.publish();
    require(model.data()["confirmed"].toBool() && model.data()["devices"].toList().empty(),
            "Reconnect did not clear removed device");
    for (auto progress : {QVariant{}, QVariant("0.5"), QVariant(true), QVariant(-1.0), QVariant(1.01),
                          QVariant(std::numeric_limits<double>::quiet_NaN()),
                          QVariant(std::numeric_limits<double>::infinity()), QVariant(0.0)}) {
        stub.setProperty(
            "jobs", QVariantList{QVariantMap{{"id", "job"}, {"state", "Running"}, {"progress", progress}}});
        stub.publish();
        require(!row(model.data(), "jobs")["progressKnown"].toBool(),
                "Unknown or invalid progress fabricated");
    }
    stub.setProperty("jobs",
                     QVariantList{QVariantMap{{"id", "job"}, {"state", "Completed"}, {"progress", 1.0}}});
    stub.publish();
    require(row(model.data(), "jobs")["state"] == "Completed" &&
                row(model.data(), "jobs")["progressText"] == "100%",
            "Completion evidence lost");
    for (auto invalid : {QVariant{}, QVariant(42), QVariant("rows"), QVariant(QVariantMap{}),
                         QVariant(QVariantList{42, QVariantMap{}, QVariantMap{{"id", 7}}})}) {
        stub.setProperty("devices", invalid);
        stub.publish();
        require(model.data()["devices"].toList().empty(), "Malformed device acquired identity");
    }
    QVariantList large;
    const auto oversizedName = QString(10000, 'x') + "<a href='evil'>";
    const QString oversizedCapability(10000, 'c');
    for (int i = 0; i < 10000; ++i)
        large.push_back(QVariantMap{{"id", QString::number(i)},
                                    {"name", oversizedName},
                                    {"capabilities", QStringList{oversizedCapability}}});
    stub.setProperty("devices", large);
    stub.setProperty("project", QString(10000, 'p') + QChar(0x202e));
    stub.publish();
    require(model.data()["devicesCount"] == 10000 && model.data()["devicesSampled"].toBool() &&
                model.data()["devices"].toList().size() == 3,
            "Large input not visibly bounded");
    require(row(model.data(), "devices")["name"].toString().size() <= 193 &&
                model.data()["project"].toString().size() <= 4097,
            "Text unbounded");
    stub.setProperty("artifacts", QVariantList{QVariantMap{{"id", "zero"}, {"chunks", 0}}});
    stub.publish();
    require(row(model.data(), "artifacts")["detail"].toString().startsWith("Chunks unavailable"),
            "Absent proto3 count inferred as zero");
    model.setBridge(nullptr);
    require(!model.data()["hasSnapshot"].toBool() && model.data()["devices"].toList().empty(),
            "Bridge replacement retained another runtime state");
    auto *transient = new HomeStub;
    transient->setProperty("connected", true);
    model.setBridge(transient);
    delete transient;
    require(!model.data()["confirmed"].toBool(), "Destroyed bridge remains confirmed");
}
StudioResult snapshot(int count = 1) {
    StudioResult result;
    result.snapshot.emplace();
    result.snapshot->set_project_path(
        "/fixture/<b>plain "
        "text</b>/engineering/very-long-path-with-explicit-source-information/Fixture.mantis");
    for (int i = 0; i < count; ++i) {
        auto *device = result.snapshot->add_devices();
        device->set_id("device-" + std::to_string(i));
        device->set_name("Fixture composite measurement source with long descriptive name " +
                         std::to_string(i));
        device->add_capabilities("org.mantis.camera.frameset-stream.v1");
    }
    for (auto state : {"Running", "Queued", "Failed", "Cancelled"}) {
        auto *job = result.snapshot->add_jobs();
        job->set_id("job-" + std::to_string(result.snapshot->jobs_size()));
        job->set_name(std::string(state) + " fixture processing operation");
        job->set_state(state);
        job->set_progress(state == std::string("Running") ? 0.64 : 0);
        if (state == std::string("Failed"))
            job->set_diagnostics("Storage destination unavailable · retained root cause");
    }
    auto *artifact = result.snapshot->add_artifacts();
    artifact->set_id("fixture-raw");
    artifact->set_type("org.mantis.RawCapture");
    artifact->set_state("FINALIZED");
    artifact->set_chunks(12);
    auto *event = result.snapshot->add_events();
    event->set_sequence(42);
    event->set_kind("info");
    event->set_component("fixture.capture");
    event->set_message("Read-only fixture snapshot; no capture started by Home");
    return result;
}
void capture(QQuickWindow *window, const QString &output, const QString &name) {
    settle();
    const auto image = window->grabWindow();
    require(!image.isNull() && image.size() == window->size(), "Rendered frame unavailable");
    require(image.save(output + "/" + name + "-" + QString::number(window->width()) + ".png"),
            "Screenshot failed");
}
void bounds(QQuickWindow *window) {
    auto *home = item(window, "homeWorkspace");
    require(home->property("contentWidth").toDouble() <= home->width() + 1, "Home horizontally overflows");
    require(item(window, "homeGoScan")->width() >= 80 && item(window, "homeProjects")->width() >= 100,
            "Hero actions collapsed");
    auto *path = item(window, "homeProjectPath");
    require(path->property("textFormat").toInt() == 0, "Runtime path uses rich text");
    require(item(window, "homeMetrics")->property("text") == "Metrics not available from this runtime",
            "Unsupported telemetry invented");
}
void projectIllustrations(QQuickWindow *window) {
    const auto frame = window->grabWindow();
    QList<QImage> thumbnails;
    for (int i = 0; i < 4; ++i) {
        auto *art = item(window, "homeProjectArt" + QString::number(i));
        const auto topLeft = art->mapToScene(QPointF{});
        // Compare the artwork's fitted viewport, excluding unused wide-card margins and text.
        const int fittedWidth = qRound(std::min(art->width(), art->height() * 300 / 140));
        const QRect crop(qRound(topLeft.x() + (art->width() - fittedWidth) / 2), qRound(topLeft.y()),
                         fittedWidth, qRound(art->height()));
        require(art->isVisible() && frame.rect().contains(crop), "Project illustration clipped");
        thumbnails.push_back(frame.copy(crop));
    }
    for (qsizetype i = 0; i < thumbnails.size(); ++i)
        for (qsizetype j = i + 1; j < thumbnails.size(); ++j) {
            require(thumbnails[i].size() == thumbnails[j].size(), "Thumbnail viewports differ");
            int different = 0;
            for (int y = 0; y < thumbnails[i].height(); ++y)
                for (int x = 0; x < thumbnails[i].width(); ++x) {
                    const auto a = thumbnails[i].pixelColor(x, y), b = thumbnails[j].pixelColor(x, y);
                    if (std::abs(a.red() - b.red()) + std::abs(a.green() - b.green()) +
                            std::abs(a.blue() - b.blue()) >
                        30)
                        ++different;
                }
            // Broad differentiation check, not a reference pixel-diff or semantic shape claim.
            require(different > thumbnails[i].width() * thumbnails[i].height() / 10,
                    "Project illustrations are visually indistinguishable");
        }
}
void detailActions(QQuickWindow *window, ObservedBridge &bridge) {
    window->resize(1536, 1024);
    window->setProperty("workspace", "home");
    for (const auto &mode : QStringList{"live", "mock", "hybrid", "mock", "live"}) {
        window->setProperty("uiMode", mode);
        settle();
        for (const auto &[buttonName, reasonName] :
             {std::pair{"homeJobs", "homeJobsUnavailable"}, {"homeArtifacts", "homeArtifactsUnavailable"}}) {
            auto *button = item(window, buttonName);
            auto *reason = item(window, reasonName);
            const bool demo = mode == "mock";
            require(button->isEnabled() != demo && reason->isVisible() == demo,
                    "Detail action source/availability contract wrong");
            auto *accessible = QAccessible::queryAccessibleInterface(button);
            // Qt 6.4 QAccessibleQuickItem::state() does not expose Item.enabled as disabled.
            // Prove actual input suppression, role/description and accessible press safety below.
            require(accessible && accessible->role() == QAccessible::Button,
                    "Detail action accessible role missing");
            if (demo) {
                require(reason->property("text").toString().contains("illustrative") &&
                            accessible->text(QAccessible::Description).contains("unavailable"),
                        "Mock detail action missing visible or accessible explanation");
                QSignalSpy clicks(button, SIGNAL(clicked()));
                const auto point = button->mapToScene(QPointF(button->width() / 2, button->height() / 2));
                require(window->contentItem()->contains(point), "Disabled detail control clipped");
                QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point.toPoint());
                button->forceActiveFocus(Qt::TabFocusReason);
                QTest::keyClick(window, Qt::Key_Space);
                QTest::keyClick(window, Qt::Key_Return);
                settle();
                require(clicks.empty() && window->property("workspace") == "home" && !bridge.busy(),
                        "Disabled mock details dispatched navigation or operation");
                require(accessible->actionInterface(), "Detail action accessible interface missing");
                accessible->actionInterface()->doAction(QAccessibleActionInterface::pressAction());
                settle();
                require(window->property("workspace") == "home" && !bridge.busy(),
                        "Accessible press bypassed disabled mock detail guard");
            } else {
                for (bool keyboard : {false, true}) {
                    click(window, buttonName, keyboard);
                    require(window->property("workspace") == "acquisition" && !bridge.busy(),
                            "Live/hybrid detail navigation failed or issued an operation");
                    window->setProperty("workspace", "home");
                    settle();
                }
            }
        }
    }
}
void components(QQuickWindow *window, ObservedBridge &bridge, const QString &output) {
    // Local route intent only. Even with a confirmed capture-capable snapshot,
    // the runtime-disabled bridge cannot hide an accidental command behind busy state.
    for (const QSize size : {QSize(1080, 720), QSize(1536, 1024), QSize(1920, 1080)}) {
        window->resize(size);
        capture(window, output, "live-awaiting-runtime");
    }
    bridge.applyResult(snapshot());
    const auto initialCapture = bridge.capturing();
    for (bool keyboard : {false, true})
        for (const auto &[name, route] : {std::pair{"homeGoScan", "acquisition"},
                                          {"homeAcquisition", "acquisition"},
                                          {"homeDevices", "devices"},
                                          {"homeProjects", "projects"},
                                          {"homeCalibration", "calibration"},
                                          {"homeCurrentProject", "acquisition"},
                                          {"homeDeviceDetails", "devices"},
                                          {"homeArtifacts", "acquisition"},
                                          {"homeJobs", "acquisition"}}) {
            window->setProperty("workspace", "home");
            window->resize(1536, 1024);
            settle();
            auto *button = item(window, name);
            auto *accessible = QAccessible::queryAccessibleInterface(button);
            require(accessible && !accessible->text(QAccessible::Name).isEmpty() &&
                        accessible->role() == QAccessible::Button,
                    "CTA accessibility missing");
            click(window, name, keyboard);
            require(window->property("workspace") == route, "Home CTA routing wrong");
            require(!bridge.busy() && bridge.capturing() == initialCapture, "Navigation altered capture");
        }
    window->setProperty("workspace", "home");
    settle();
    auto *planned = item(window, "homePlanned");
    require(!planned->isEnabled(), "Unsupported project control enabled");
    require(QAccessible::queryAccessibleInterface(planned)
                ->text(QAccessible::Description)
                .contains("unavailable"),
            "Disabled reason absent");
    QSignalSpy clicks(planned, SIGNAL(clicked()));
    auto point = planned->mapToScene(QPointF(planned->width() / 2, planned->height() / 2));
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point.toPoint());
    require(clicks.empty(), "Disabled CTA clicked");
    detailActions(window, bridge);
    for (const QSize size : {QSize(1080, 720), QSize(1536, 1024), QSize(1920, 1080)}) {
        window->resize(size);
        for (const auto &mode : QStringList{"mock", "live", "hybrid"}) {
            window->setProperty("uiMode", mode);
            bridge.applyResult(snapshot(3));
            settle();
            bounds(window);
            auto *model = qobject_cast<HomeModel *>(
                item(window, "homeWorkspace")->property("liveModel").value<QObject *>());
            require(model, "Home model absent");
            require((mode == "mock") == (model->bridge() == nullptr),
                    "Mock retains runtime presentation dependency");
            auto *fixture = item(window, "homeWorkspace")->findChild<QObject *>("homeDemoFixture");
            require(fixture && fixture->metaObject()->indexOfMethod("startCapture(QString)") < 0,
                    "Demo exposes command API");
            const auto demoData = fixture->property("data").value<QJSValue>().toVariant().toMap();
            require(demoData.value("source") == "mock", "Demo source label lost");
            for (const char *section : {"devices", "jobs", "artifacts", "events"})
                for (const auto &value : demoData.value(section).toList())
                    require(value.toMap().value("id").toString().isEmpty() &&
                                value.toMap().value("source") == "mock",
                            "Demo identity became actionable or live");
            require(model->data()["devicesCount"].toInt() == (mode == "mock" ? 0 : 3),
                    "Demo merged into live count");
            capture(window, output, mode + "-populated");
            if (mode == "mock")
                projectIllustrations(window);
            if (mode == "hybrid") {
                require(item(window, "homeHybridSeparation")->isVisible(), "Hybrid separation missing");
                auto *scroll = item(window, "homeWorkspace")->property("contentItem").value<QObject *>();
                require(scroll &&
                            scroll->setProperty("contentY", scroll->property("contentHeight").toDouble() -
                                                                scroll->property("height").toDouble()),
                        "Hybrid scroll unavailable");
                capture(window, output, "hybrid-demo-separation");
                scroll->setProperty("contentY", 0);
            }
        }
        window->setProperty("uiMode", "live");
        bridge.applyResult(snapshot(0));
        capture(window, output, "live-zero-devices");
        StudioResult empty;
        empty.snapshot.emplace();
        bridge.applyResult(empty);
        capture(window, output, "live-empty");
        StudioResult lost;
        lost.issues.push_back({"snapshot", "Fixture peer closed",
                               mantis::Error{mantis::Status::io, "Fixture peer closed", "platform"}});
        bridge.applyResult(lost);
        capture(window, output, "live-unconfirmed-empty");
        bridge.applyResult(snapshot(3));
        bridge.applyResult(lost);
        capture(window, output, "live-stale");
    }
    const auto originalFont = window->property("font");
    window->setProperty("font", QFont("DejaVu Serif", 12));
    window->resize(1080, 720);
    settle();
    bounds(window);
    capture(window, output, "alternate-font-live");
    window->setProperty("font", originalFont);
    bridge.applyResult(snapshot(1));
    StudioResult lost;
    bridge.applyResult(lost);
    window->setProperty("uiMode", "mock");
    window->setProperty("uiMode", "hybrid");
    settle();
    require(item(window, "homeFreshness")->property("text") == "Last known · current state unconfirmed",
            "Mode switch lost cached snapshot freshness");
    QElapsedTimer timing;
    timing.start();
    for (int i = 0; i < 90; ++i) {
        // No event processing between delegate removal, geometry mutation and route swap.
        bridge.applyResult(snapshot(i % 4 == 0 ? 0 : i % 4 == 1 ? 1 : 32));
        window->setProperty("uiMode", QStringList{"mock", "live", "hybrid"}[i % 3]);
        window->resize(i % 3 == 0 ? QSize(1080, 720) : i % 3 == 1 ? QSize(1536, 1024) : QSize(1920, 1080));
        window->setProperty("workspace", i % 2 ? "projects" : "home");
        QCoreApplication::processEvents();
    }
    window->setProperty("workspace", "home");
    window->setProperty("uiMode", "live");
    settle();
    bounds(window);
    std::cout << "Home stress: 90 immediate model/mode/route/resize transitions, observed "
              << timing.elapsed() << " ms (informational, not a latency budget)\n";
}
void wire(QQuickWindow *window, ObservedBridge &observed, const QString &output) {
    StudioBridge bridge(nullptr, true); // Production asynchronous polling and command gating.
    window->setProperty("studioBridge", QVariant::fromValue(&bridge));
    auto restoreBridge =
        qScopeGuard([&] { window->setProperty("studioBridge", QVariant::fromValue(&observed)); });
    auto wait = [&](const char *phase, auto condition) {
        QElapsedTimer deadline;
        deadline.start();
        while (!condition() && deadline.elapsed() < 5000)
            QTest::qWait(10);
        if (!condition())
            throw std::runtime_error(std::string("Timed out at ") + phase + ": " +
                                     bridge.error().toStdString());
    };
    wait("initial confirmation", [&] { return bridge.connected() && !bridge.busy(); });
    auto *model =
        qobject_cast<HomeModel *>(item(window, "homeWorkspace")->property("liveModel").value<QObject *>());
    require(model && model->data()["confirmed"].toBool(), "Public wire snapshot not confirmed");
    require(model->data()["projectName"] == "Wire.mantis" &&
                row(model->data(), "devices")["name"] == "Wire fixture camera",
            "Public wire project/device not presented");
    require(row(model->data(), "jobs")["name"] == "Wire processing" &&
                row(model->data(), "jobs")["progressText"] == "64%",
            "Public wire job not presented");
    require(row(model->data(), "artifacts")["name"] == "org.mantis.RawCapture" &&
                row(model->data(), "events")["sequence"] == "74",
            "Public wire artifacts/events not presented");
    for (const auto &[name, expected] : {std::pair{"home_devices_0_live", "Wire fixture camera"},
                                         {"home_artifacts_0_live", "RawCapture"},
                                         {"home_events_0_live", "wire.fixture"}}) {
        auto *rendered = find(item(window, name), "homeRowName");
        require(rendered && rendered->property("text") == expected,
                "Public wire data missing from rendered Home section");
    }
    require(item(window, "homeProjectPath")->property("text").toString().endsWith("Wire.mantis"),
            "Wire data not reaching actual Home text");
    auto *jobName = find(item(window, "home_jobs_0_live"), "homeRowName");
    require(jobName && jobName->property("text") == "Wire processing",
            "Wire jobs not reaching actual Home row");
    for (const auto &[name, route] : {std::pair{"homeGoScan", "acquisition"},
                                      {"homeDevices", "devices"},
                                      {"homeProjects", "projects"},
                                      {"homeArtifacts", "acquisition"},
                                      {"homeJobs", "acquisition"}}) {
        window->setProperty("workspace", "home");
        settle();
        click(window, name, true);
        require(window->property("workspace") == route, "Wire CTA wrong");
        require(!bridge.capturing(), "Home started a capture");
    }
    window->setProperty("workspace", "home");
    wait("idle watcher", [&] { return !bridge.busy(); });
    bridge.runPipeline("reject-home");
    // Deliberately hold GUI event delivery after the bounded fixture worker replies.
    // A finished worker still owns its undelivered result; another command must not replace it.
    QThread::msleep(200);
    require(bridge.busy(), "Finished worker released busy before GUI result delivery");
    bridge.refresh();
    bridge.runPipeline("forbidden-home");
    wait("rejected operation",
         [&] { return !bridge.busy() && bridge.error().contains("Rejected fixture operation"); });
    require(bridge.connected() && row(model->data(), "issues")["code"] == 6 &&
                model->data()["freshness"] == "Confirmed runtime snapshot",
            "Rejected operation disconnected Home");
    capture(window, output, "wire-rejected-operation");
    wait("idle watcher", [&] { return !bridge.busy(); });
    bridge.selectArtifact("missing-home-data");
    wait("artifact confirmation", [&] { return !bridge.busy() && bridge.errorDetails().size() == 2; });
    require(bridge.connected(), "Data access failure corrupted confirmed state");
    capture(window, output, "wire-data-failure");
    wait("idle watcher", [&] { return !bridge.busy(); });
    bridge.runPipeline("transport-home");
    wait("transport loss", [&] { return !bridge.busy() && !bridge.connected(); });
    require(model->data()["freshness"] == "Last known · current state unconfirmed" &&
                model->data()["projectName"] == "Wire.mantis",
            "Transport failure lost stale data semantics");
    capture(window, output, "wire-transport-loss");
    // Explicit test-fixture restoration through the existing wire command; no new production API.
    mantis::wire::v1::Request restore;
    restore.mutable_plugin_enable()->set_id("home-fixture-reconnect");
    const mantis::client::Client client;
    (void)client.call(restore);
    bridge.refresh();
    wait("reconnection",
         [&] { return bridge.connected() && model->data()["projectName"] == "Reconnected.mantis"; });
    for (const QSize size : {QSize(1080, 720), QSize(1536, 1024), QSize(1920, 1080)}) {
        window->resize(size);
        capture(window, output, "wire-connected-live");
        bounds(window);
    }
    require(!bridge.capturing(), "Fixture Home changed scanner state");
    window->setProperty("studioBridge", QVariant::fromValue(&observed));
    std::cout << "PASS: asynchronous public client/StudioBridge/Home UI, rejection, data access failure, "
                 "transport loss, reconnection; navigation sent no mutable command\n";
}

} // namespace
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    QAccessible::setActive(true); // Exercise control state updates as with an active accessibility client.
    QQuickStyle::setStyle("Basic");
    qInstallMessageHandler(messages);
    qmlRegisterType<HomeModel>("Mantis.Studio", 1, 0, "HomeModel");
    qmlRegisterType<mantis::render::PointCloudView>("Mantis.Render", 1, 0, "PointCloudView");
    qmlRegisterType<MeasurementView>("Mantis.Render", 1, 0, "MeasurementView");
    try {
        const bool integration = argc > 2 && QString::fromLocal8Bit(argv[2]) == "wire";
        const QString output = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QDir::tempPath() + "/mantis-home";
        require(QDir().mkpath(output), "Cannot create output directory");
        if (!integration)
            presentation();
        ObservedBridge bridge;
        CalibrationController calibration(
            std::make_shared<PublicCalibrationClient>(mantis::client::Client{mantis::client::Endpoint{}}));
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("studio", &bridge);
        engine.rootContext()->setContextProperty("calibration", &calibration);
        engine.setInitialProperties({{"uiMode", "live"}, {"workspace", "home"}});
        engine.load(QUrl("qrc:/ui/shell/Main.qml"));
        require(!engine.rootObjects().empty(), "Home shell failed to load");
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().front());
        require(window, "Home window absent");
        settle();
        if (integration)
            wire(window, bridge, output);
        else
            components(window, bridge, output);
        require(warnings.empty(), "Qt/QML warnings in Home paths");
        std::cout << "PASS: Home presentation, authority, accessibility and visual evidence\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        for (const auto &warning : warnings)
            std::cerr << warning.toStdString() << '\n';
        return 1;
    }
    return 0;
}
#include "studio_home.moc"
