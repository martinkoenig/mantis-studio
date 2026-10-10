#include "bridge.hpp"
#include "calibration_controller.hpp"
#include "devices_model.hpp"
#include "home_model.hpp"
#include "projects_model.hpp"
#include "projects_scroll.hpp"
#include <QAccessible>
#include <QDir>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QImage>
#include <QJSValue>
#include <QList>
#include <QPointer>
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
void scrollTo(QQuickWindow *window, QQuickItem *target) {
    auto *home = item(window, "homeWorkspace");
    auto *flick = qobject_cast<QQuickItem *>(home->property("contentItem").value<QObject *>());
    require(flick, "Home scrolling unavailable");
    const auto y = target->mapToItem(flick, QPointF(0, target->height() / 2)).y();
    if (y < 0 || y > flick->height()) {
        const auto maximum = std::max(0.0, flick->property("contentHeight").toDouble() - flick->height());
        flick->setProperty(
            "contentY",
            std::clamp(flick->property("contentY").toDouble() + y - flick->height() / 2, 0.0, maximum));
        settle();
    }
}
void resetScroll(QQuickWindow *window) {
    item(window, "homeWorkspace")->property("contentItem").value<QObject *>()->setProperty("contentY", 0);
    settle();
}
void click(QQuickWindow *window, const QString &name, bool keyboard = false) {
    auto *button = item(window, name);
    require(button->isVisible() && button->isEnabled(), "CTA unavailable");
    scrollTo(window, button);
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
    const QSize pixels(qRound(window->width() * window->devicePixelRatio()),
                       qRound(window->height() * window->devicePixelRatio()));
    if (image.isNull() || image.size() != pixels)
        std::cerr << "Capture logical=" << window->width() << "x" << window->height()
                  << " DPR=" << window->devicePixelRatio() << " expected=" << pixels.width() << "x"
                  << pixels.height() << " received=" << image.width() << "x" << image.height() << '\n';
    require(!image.isNull() && image.size() == pixels, "Rendered frame unavailable");
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
    require(item(window, "homeMetrics")->property("text") ==
                (window->property("uiMode") == "mock" ? "Illustrative gauges · Demo / Mock"
                                                      : "Metrics not available from this runtime"),
            "Unsupported telemetry invented");
}
void projectIllustrations(QQuickWindow *window) {
    QList<QImage> thumbnails;
    for (int i = 0; i < 4; ++i) {
        auto *art = item(window, "homeProjectArt" + QString::number(i));
        scrollTo(window, art);
        const auto frame = window->grabWindow();
        const auto topLeft = art->mapToScene(QPointF{});
        // Compare the artwork's fitted viewport, excluding unused wide-card margins and text.
        const int fittedWidth = qRound(std::min(art->width(), art->height() * 300 / 140));
        const QRect crop(qRound(topLeft.x() + (art->width() - fittedWidth) / 2), qRound(topLeft.y()),
                         fittedWidth, qRound(art->height()));
        require(art->isVisible() && frame.rect().contains(crop), "Project illustration clipped");
        thumbnails.push_back(frame.copy(crop));
    }
    resetScroll(window);
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
        for (const auto &[buttonName, reasonName] : {std::pair{"homeJobs", "homeJobsUnavailable"}}) {
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
                require(reason->property("text").toString().contains("Illustrative") &&
                            accessible->text(QAccessible::Description).contains("unavailable"),
                        "Mock detail action missing visible or accessible explanation");
                QSignalSpy clicks(button, SIGNAL(clicked()));
                scrollTo(window, button);
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
void fidelity(QQuickWindow *window) {
    auto *home = item(window, "homeWorkspace");
    require(!find(home, "homeArtifacts") && !find(home, "homeArtifactsUnavailable"),
            "Obsolete Home artifact CTA retained");
    QList<QQuickItem *> pending{home};
    while (!pending.empty()) {
        auto *node = pending.takeLast();
        const auto text = node->property("text").toString();
        require(text != "Example artifacts" && text != "Current project artifacts",
                "Standalone artifact card retained");
        for (auto *child : node->childItems())
            pending.push_back(child);
    }
    auto *hero = item(window, "homeHero");
    auto *primary = find(home, "homePrimary");
    const auto heroCeiling = primary && primary->property("wide").toBool() ? 330 : 310;
    require(hero->height() >= 270 && hero->height() <= heroCeiling, "Hero lost substantive height");
    const auto mock = window->property("uiMode") == "mock";
    require(item(window, "homeCurrentProjectCard")->isVisible() != mock,
            "Live current project confused with history");
    require(item(window, "homeProjectGallery")->isVisible() == mock,
            "Sample gallery leaked into live history");
    for (int i = 0; i < 4; ++i) {
        auto *card = item(window, "homeActionCard" + QString::number(i));
        require(card->height() >= 140, "Quick Action collapsed");
        for (auto *button : card->childItems()) {
            // Actual controls are inside the body; check descendants recursively below.
            QList<QQuickItem *> children{button};
            while (!children.empty()) {
                auto *child = children.takeLast();
                if (child->property("text").isValid() && child->property("background").isValid()) {
                    const auto rect = child->mapRectToItem(card, child->boundingRect());
                    require(card->boundingRect().adjusted(-1, -1, 1, 1).contains(rect),
                            "Quick Action control clipped within card");
                }
                for (auto *descendant : child->childItems())
                    children.push_back(descendant);
            }
        }
    }
    if (window->width() == 1536 && mock) {
        require(hero->width() >= 900, "Primary hero narrowed");
        for (int i = 0; i < 4; ++i) {
            auto *card = item(window, "homeProjectCard" + QString::number(i));
            require(card->height() >= 185 && card->width() >= 200, "Project card too small");
        }
        auto *actions = item(window, "homeQuickActions");
        auto *activity = item(window, "homeActivityHeading");
        auto *firstRow = item(window, "home_activity_0_mock");
        const auto actionBottom = actions->mapToScene(QPointF(0, actions->height())).y();
        const auto activityTop = activity->mapToScene(QPointF{}).y();
        require(activityTop > actionBottom && activityTop - actionBottom < 40,
                "Activity does not directly follow actions");
        const auto rowBottom = firstRow->mapToScene(QPointF(0, firstRow->height())).y();
        const auto homeBottom = home->mapToScene(QPointF(0, home->height())).y();
        require(rowBottom < homeBottom, "No actual Activity row in first primary viewport");
        std::cout << "Primary layout: hero " << hero->width() << "x" << hero->height() << ", actions bottom "
                  << actionBottom << ", activity " << activityTop << ", first row bottom " << rowBottom
                  << ", viewport bottom " << homeBottom << '\n';
    }
}
void activityContract(QQuickWindow *window) {
    const bool mock = window->property("uiMode") == "mock";
    if (mock) {
        auto *row = item(window, "home_activity_0_mock");
        require(find(row, "homeActivityType")->property("text") == "Scan" &&
                    find(row, "homeActivityDate")->property("text").toString().startsWith("2026-") &&
                    find(row, "homeActivitySize")->property("text") == "2.1 GB",
                "Illustrative Activity fields missing");
        return;
    }
    for (const auto &[name, type] :
         {std::pair{"home_events_0_live", "Runtime event"}, {"home_artifacts_0_live", "Project artifact"}}) {
        auto *row = find(window->contentItem(), name);
        if (!row)
            continue; // Empty states are exercised separately.
        require(find(row, "homeActivityType")->property("text") == type &&
                    find(row, "homeActivityDate")->property("text") == "—" &&
                    find(row, "homeActivitySize")->property("text") == "—",
                "Live Activity invented provenance/date/size");
        require(find(row, "homeRowName")->property("textFormat").toInt() == 0 &&
                    find(row, "homeActivityDetail")->property("textFormat").toInt() == 0,
                "Activity interprets runtime markup");
    }
}
void activityEdgeCases(QQuickWindow *window, ObservedBridge &bridge, const QString &output) {
    window->setProperty("uiMode", "live");
    window->setProperty("workspace", "home");
    auto result = snapshot(0);
    result.snapshot->clear_events();
    result.snapshot->clear_artifacts();
    for (const auto seq : {2, 99, 12, 0}) {
        auto *event = result.snapshot->add_events();
        event->set_sequence(seq);
        event->set_component("<b>literal</b>" + std::string(10000, 'c'));
        event->set_message(std::string(10000, 'm'));
    }
    for (const auto id : {"artifact-b", "artifact-a"}) {
        auto *artifact = result.snapshot->add_artifacts();
        artifact->set_id(id);
        artifact->set_type("<b>literal</b>" + std::string(10000, 't'));
    }
    bridge.applyResult(result);
    resetScroll(window);
    activityContract(window);
    auto *event = item(window, "home_events_0_live");
    auto *artifact = item(window, "home_artifacts_0_live");
    require(find(event, "homeRowName")->property("text").toString().size() <= 193 &&
                find(event, "homeActivityDetail")->property("text").toString().startsWith("#99 · ") &&
                find(event, "homeActivityDetail")->property("text").toString().size() <= 520,
            "Activity lost bounded event text or sequence ordering");
    require(find(artifact, "homeActivityDetail")->property("text") == "Chunks unavailable · artifact-a" &&
                find(artifact, "homeActivityStatus")->property("text") == "Unknown",
            "Activity lost artifact identity / ordering / unknown state");
    require(event->mapToScene(QPointF{}).y() < artifact->mapToScene(QPointF{}).y(),
            "Event/artifact groups were chronologically interleaved");
    window->resize(1080, 720);
    settle();
    scrollTo(window, artifact);
    capture(window, output, "live-long-activity");
    StudioResult lost;
    lost.issues.push_back({"snapshot", "Fixture peer closed",
                           mantis::Error{mantis::Status::io, "Fixture peer closed", "platform"}});
    bridge.applyResult(lost);
    settle();
    require(item(window, "homeActivity")->property("freshness") == "Last known · current state unconfirmed",
            "Activity stale provenance missing");
    StudioResult empty;
    empty.snapshot.emplace();
    bridge.applyResult(empty);
    settle();
    require(!find(window->contentItem(), "home_events_0_live") &&
                !find(window->contentItem(), "home_artifacts_0_live"),
            "Activity retained removed runtime rows");
    require(bridge.artifacts().empty(), "Activity corrupted bridge empty-artifact semantics");
    bridge.applyResult(snapshot());
    require(!bridge.artifacts().empty(), "Real artifact data removed with Home card");
    window->resize(1536, 1024);
    resetScroll(window);
}
void localActions(QQuickWindow *window, ObservedBridge &bridge, const QString &output) {
    const QList<QPointer<QQuickItem>> controls{item(window, "homeDevices"), item(window, "homeAcquisition"),
                                               item(window, "homeLearn"), item(window, "homeExamples")};
    for (const auto &mode : QStringList{"live", "mock", "hybrid"}) {
        std::cout << "STAGE: local Home actions in " << mode.toStdString() << std::endl;
        window->setProperty("uiMode", mode);
        window->setProperty("workspace", "home");
        resetScroll(window);
        for (const auto &control : controls)
            require(control && item(window, control->objectName()) == control,
                    "Mode change recreated a persistent Home action");
        for (bool keyboard : {false, true}) {
            click(window, "homeLearn", keyboard);
            auto *guide = window->findChild<QObject *>("homeGuide");
            require(guide && guide->property("opened").toBool(), "Local learning view did not open");
            require(window->property("workspace") == "home" && !bridge.busy(),
                    "Learning dispatched runtime work");
            if (mode == "mock" && !keyboard)
                capture(window, output, "mock-workflow-guide");
            click(window, "homeGuideClose", keyboard);
            require(!guide->property("opened").toBool(), "Guide did not close");
            click(window, "homeLearn", keyboard);
            click(window, "homeGuideDevices", keyboard);
            require(window->property("workspace") == "devices" && !bridge.busy(),
                    "Guide device routing dispatched an operation");
            window->setProperty("workspace", "home");
            resetScroll(window);
        }
        if (mode != "live") {
            click(window, "homeExamples", true);
            require(window->activeFocusItem() &&
                        window->activeFocusItem()->objectName() == "homeProjectGallery",
                    "Example action failed to focus showcase");
            require(window->property("workspace") == "home" && !bridge.busy(), "Examples dispatched import");
            capture(window, output, mode + "-example-focus");
        }
        resetScroll(window);
        for (const auto &name : QStringList{"homePlanned", "homeExamples"}) {
            auto *button = item(window, name);
            if (button->isEnabled())
                continue;
            auto *accessible = QAccessible::queryAccessibleInterface(button);
            require(accessible && accessible->text(QAccessible::Description).contains("unavailable"),
                    "Disabled action explanation missing");
            scrollTo(window, button);
            QSignalSpy clicks(button, SIGNAL(clicked()));
            const auto point = button->mapToScene(QPointF(button->width() / 2, button->height() / 2));
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point.toPoint());
            button->forceActiveFocus(Qt::TabFocusReason);
            QTest::keyClick(window, Qt::Key_Space);
            QTest::keyClick(window, Qt::Key_Return);
            accessible->actionInterface()->doAction(QAccessibleActionInterface::pressAction());
            settle();
            require(clicks.empty() && window->property("workspace") == "home" && !bridge.busy(),
                    "Disabled action dispatched");
        }
    }
    window->setProperty("uiMode", "live");
    resetScroll(window);
}
void previewGeometry(QQuickWindow *window) {
    QList<QQuickItem *> pending{item(window, "homeWorkspace")};
    const auto frame = window->grabWindow();
    const auto dpr = window->devicePixelRatio();
    int inspected = 0;
    while (!pending.empty()) {
        auto *node = pending.takeLast();
        if (node->isVisible() && node->objectName().startsWith("homeProjectArt")) {
            auto *image = qobject_cast<QQuickItem *>(node->property("image").value<QObject *>());
            const auto padding = node->property("safePadding").toDouble();
            require(image && padding >= 12 && padding <= 22, "Preview has no padded transparent layer");
            require(image->property("fillMode").toInt() == 1, "Preview geometry cropped or stretched");
            const auto url = QUrl("qrc:/ui/home/").resolved(image->property("source").toUrl());
            require(url.path().endsWith(".png"), "Opaque preview source retained");
            const QImage asset(":" + url.path());
            require(!asset.isNull() && asset.hasAlphaChannel(), "Preview alpha decode failed");
            const auto painted = QSizeF(image->property("paintedWidth").toDouble(),
                                        image->property("paintedHeight").toDouble());
            const QRectF fitted((node->width() - painted.width()) / 2,
                                (node->height() - painted.height()) / 2, painted.width(), painted.height());
            require(painted.width() > 0 && painted.height() > 0 &&
                        node->boundingRect()
                            .adjusted(padding - 1, padding - 1, 1 - padding, 1 - padding)
                            .contains(fitted),
                    "Preview subject escaped its safe area");
            auto *card = node->parentItem()->parentItem();
            require(std::abs(node->width() - card->width()) <= 1 && node->height() >= 120 &&
                        node->height() <= 180 && std::abs(card->height() - node->height() - 78) <= 1,
                    "Preview/body geometry changed");
            // The full upper band is card-owned, including outside the fitted image.
            // Sample actual Qt pixels, not a golden image or inferred QML color.
            const auto point = node->mapToScene(QPointF(10, 8));
            if (point.y() >= 0 && point.y() < window->height()) {
                const auto color = frame.pixelColor(qRound(point.x() * dpr), qRound(point.y() * dpr));
                for (double x : {node->width() / 2, node->width() - 10}) {
                    const auto other = node->mapToScene(QPointF(x, 8));
                    const auto pixel = frame.pixelColor(qRound(other.x() * dpr), qRound(other.y() * dpr));
                    require(std::abs(color.red() - pixel.red()) + std::abs(color.green() - pixel.green()) +
                                    std::abs(color.blue() - pixel.blue()) <=
                                3,
                            "Preview background has image-sized color bars");
                }
            }
            ++inspected;
        }
        for (auto *child : node->childItems())
            pending.push_back(child);
    }
    require(inspected == (window->property("uiMode") == "live" ? 0 : 4), "Visible preview layers missing");
}
void assetTransparency() {
    for (const auto &name : QStringList{"housing", "rotor", "bracket", "cover", "scanner"}) {
        const QImage image(":/ui/home/assets/" + name + ".png");
        require(!image.isNull() && image.hasAlphaChannel(), "Foreground is not decoded RGBA");
        int transparent = 0, opaque = 0, antialiased = 0;
        QRect subject;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x) {
                const auto alpha = image.pixelColor(x, y).alpha();
                transparent += alpha == 0;
                opaque += alpha == 255;
                antialiased += alpha > 0 && alpha < 255;
                if (alpha > 0)
                    subject = subject.united(QRect(x, y, 1, 1));
            }
        require(transparent > image.width() * image.height() / 10 &&
                    opaque > image.width() * image.height() / 10 && antialiased > 100,
                "Foreground has trivial alpha or no antialiased edges");
        require(subject.left() >= 8 && subject.top() >= 8 && subject.right() < image.width() - 8 &&
                    subject.bottom() < image.height() - 8,
                "Foreground geometry cropped at export boundary");
        std::cout << "RGBA " << name.toStdString() << " " << image.width() << "x" << image.height()
                  << " transparent=" << transparent << " opaque=" << opaque << " edge=" << antialiased
                  << " inset=" << subject.left() << "/" << subject.top() << '\n';
    }
}
void responsiveBounds(QQuickWindow *window) {
    auto *home = item(window, "homeWorkspace");
    auto *content = item(window, "homeContent");
    auto *dashboard = item(window, "homeDashboard");
    auto *hero = item(window, "homeHero");
    auto *rail = item(window, "homeRightRail");
    const auto available = home->property("availableWidth").toDouble();
    require(std::abs(content->width() - available) <= 1, "Whole Home dashboard remains a capped island");
    require(std::abs(dashboard->width() - content->width()) <= 1, "Dashboard escaped workspace");
    const auto railRect = rail->mapRectToItem(home, rail->boundingRect());
    const auto right = home->property("leftPadding").toDouble() + available - railRect.right();
    auto *flick = qobject_cast<QQuickItem *>(home->property("contentItem").value<QObject *>());
    require(flick && flick->property("contentWidth").toDouble() <= flick->width() + 1 &&
                std::abs(flick->property("contentX").toDouble()) <= 1,
            "Home permits horizontal scrolling");
    auto *primary = item(window, "homePrimary");
    const auto primaryRect = primary->mapRectToItem(home, primary->boundingRect());
    if (dashboard->property("stacked").toBool()) {
        require(std::abs(primary->width() - content->width()) <= 1 &&
                    std::abs(rail->width() - content->width()) <= 1 &&
                    railRect.top() >= primaryRect.bottom() + 15,
                "Narrow Home failed to stack its rail");
    } else {
        require(std::abs(right) <= 1 && rail->width() >= 320 && rail->width() <= 349 &&
                    std::abs(railRect.left() - primaryRect.right() - 16) <= 1,
                "Right rail detached from workspace edge or gutter grew");
        require(std::abs(primary->width() - (available - rail->width() - 16)) <= 1,
                "Primary region stopped using available width");
    }
    const auto heroRect = hero->mapRectToItem(primary, hero->boundingRect());
    auto *activity = item(window, "homeActivity");
    const auto activityRect = activity->mapRectToItem(primary, activity->boundingRect());
    auto *actions = item(window, "homeQuickActions");
    const auto actionsRect = actions->mapRectToItem(primary, actions->boundingRect());
    if (primary->property("wide").toBool()) {
        require(hero->width() >= primary->width() * 0.45 && hero->width() <= primary->width() * 0.55 &&
                    std::abs(actionsRect.left() - heroRect.right() - 24) <= 1 &&
                    std::abs(activity->width() - hero->width()) <= 1 &&
                    activityRect.top() > heroRect.bottom(),
                "Wide Home lanes unbalanced, empty or overlapping");
    } else
        require(std::abs(hero->width() - primary->width()) <= 1, "Normal Hero lost primary width");
    for (const auto &prefix : QStringList{"homeProjectCard", "homeActionCard"}) {
        if (prefix == "homeProjectCard" && window->property("uiMode") != "mock")
            continue;
        QList<QRectF> cards;
        for (int i = 0; i < 4; ++i) {
            auto *card = item(window, prefix + QString::number(i));
            require(card->isVisible() && card->width() >= 150 && card->width() <= 430 &&
                        card->height() >= 140,
                    "Responsive card collapsed or stretched beyond twice reference width");
            const auto rect = card->mapRectToItem(content, card->boundingRect());
            for (const auto &previous : cards)
                require(!rect.intersects(previous), "Responsive cards overlap");
            cards.push_back(rect);
        }
    }
    QList<QQuickItem *> pending{home};
    while (!pending.empty()) {
        auto *node = pending.takeLast();
        if (node->isVisible() && node->property("background").isValid() && node->property("text").isValid()) {
            const auto control = node->mapRectToItem(content, node->boundingRect());
            require(control.left() >= -1 && control.right() <= content->width() + 1 && node->width() > 0,
                    "Home control horizontally clipped");
        }
        for (auto *child : node->childItems())
            pending.push_back(child);
    }
    bounds(window);
    fidelity(window);
    activityContract(window);
    previewGeometry(window);
    std::cout << "Fluid Home: " << window->width() << "x" << window->height() << " "
              << window->property("uiMode").toString().toStdString() << " DPR=" << window->devicePixelRatio()
              << " content=" << content->width() << " primary=" << primary->width() << " rightGap=" << right
              << " hero=" << hero->width() << " rail=" << rail->width()
              << " card=" << item(window, "homeActionCard0")->width() << '\n';
}
void responsive(QQuickWindow *window, ObservedBridge &bridge, const QString &output, bool hidpi = false) {
    require(window->minimumWidth() == 1080 && window->minimumHeight() == 720,
            "Home correction changed global minimum size");
    if (hidpi)
        require(window->devicePixelRatio() == qEnvironmentVariable("QT_SCALE_FACTOR").toDouble(),
                "HiDPI fixture did not establish requested DPR");
    window->setProperty("workspace", "home");
    bridge.applyResult(snapshot(3));
    QList<QPointer<QQuickItem>> persistent;
    QList<QQuickItem *> pending{item(window, "homeWorkspace")};
    while (!pending.empty()) {
        auto *node = pending.takeLast();
        if (node->objectName().startsWith("homeProjectCard") ||
            node->objectName().startsWith("homeActionCard"))
            persistent.push_back(node);
        for (auto *child : node->childItems())
            pending.push_back(child);
    }
    require(persistent.size() == 12, "Persistent project / action cards absent");
    auto *learn = item(window, "homeLearn");
    auto checkPersistence = [&] {
        for (const auto &card : persistent)
            require(card, "Resize or mode change destroyed a project / action delegate");
        require(learn->hasActiveFocus(), "Resize or mode change lost Home keyboard focus");
    };
    const QList<QSize> sizes =
        hidpi ? QList<QSize>{{1536, 1024}, {2560, 1440}}
              : QList<QSize>{{1080, 720},  {1280, 720},  {1366, 768},  {1440, 900}, {1536, 1024},
                             {1920, 1080}, {2560, 1440}, {3440, 1440}, {3840, 2160}};
    for (const auto &mode : QStringList{"mock", "live", "hybrid"}) {
        window->setProperty("uiMode", mode);
        settle();
        learn->forceActiveFocus(Qt::TabFocusReason);
        qreal previousPrimary = 0;
        for (const auto &size : sizes) {
            window->resize(size);
            resetScroll(window);
            responsiveBounds(window);
            const auto mainWidth = item(window, "homePrimary")->width();
            if (size.width() >= 1536 && previousPrimary > 0)
                require(mainWidth > previousPrimary, "Main region failed to grow at larger viewports");
            previousPrimary = mainWidth;
            checkPersistence();
            capture(window, output, "responsive-" + mode);
            for (bool keyboard : {false, true}) {
                click(window, "homeLearn", keyboard);
                require(window->findChild<QObject *>("homeGuide")->property("opened").toBool(),
                        "Responsive guide action inaccessible");
                click(window, "homeGuideClose", keyboard);
                require(!bridge.busy() && window->property("workspace") == "home",
                        "Responsive guide dispatched runtime work");
            }
            learn->forceActiveFocus(Qt::TabFocusReason);
        }
    }
    // Cross the rail breakpoint, the gallery/action two-to-four-column breakpoint,
    // and the content-width ceiling in both directions, without recreating controls.
    for (int i = 0; i < 3; ++i)
        for (const int width : {1248, 1252, 1298, 1302, 1322, 1326, 2282, 2286, 3840, 2286, 2282, 1326, 1322,
                                1302, 1298, 1252, 1248}) {
            window->setProperty("uiMode", QStringList{"mock", "live", "hybrid"}[i]);
            window->resize(width, 900);
            settle();
            responsiveBounds(window);
            checkPersistence();
        }
    window->resize(3440, 1440);
    window->setProperty("uiMode", "hybrid");
    resetScroll(window);
    click(window, "homeExamples", true);
    require(window->activeFocusItem() && window->activeFocusItem()->objectName() == "homeProjectGallery",
            "Centered hybrid showcase focus broken");
    auto *gallery = window->activeFocusItem();
    scrollTo(window, gallery);
    const auto visible = gallery->mapRectToItem(item(window, "homeWorkspace"), gallery->boundingRect());
    require(visible.top() >= -1 && visible.bottom() <= item(window, "homeWorkspace")->height() + 1,
            "Centered hybrid showcase unreachable by vertical scroll");
    capture(window, output, "responsive-hybrid-showcase");
    window->setProperty("uiMode", "live");
    resetScroll(window);
}
void resizeContinuously(QQuickWindow *window, ObservedBridge &bridge, const QString &output) {
    window->setProperty("workspace", "home");
    bridge.applyResult(snapshot(3));
    QList<QPointer<QQuickItem>> controls;
    for (const auto &name : QStringList{"homeDevices", "homeAcquisition", "homeLearn", "homeExamples"})
        controls.push_back(item(window, name));
    QList<QPointer<QQuickItem>> cards;
    for (int i = 0; i < 4; ++i) {
        cards.push_back(item(window, "homeProjectCard" + QString::number(i)));
        cards.push_back(item(window, "homeActionCard" + QString::number(i)));
    }
    auto check = [&] {
        responsiveBounds(window);
        for (const auto &control : controls)
            require(control && item(window, control->objectName()) == control, "Resize recreated action");
        for (const auto &card : cards)
            require(card, "Resize destroyed project/action delegate");
        require(controls[2]->hasActiveFocus(), "Continuous resize lost keyboard focus");
    };
    for (const auto &mode : QStringList{"mock", "live", "hybrid"}) {
        window->setProperty("uiMode", mode);
        controls[2]->forceActiveFocus(Qt::TabFocusReason);
        for (int direction : {1, -1})
            for (int i = 0; i <= 44; ++i) {
                window->resize(direction == 1 ? 1080 + i * 62 : 3808 - i * 62, 900);
                settle();
                check();
            }
        window->resize(1536, 1024);
        resetScroll(window);
        const auto restored = window->geometry();
        window->showMaximized();
        settle();
        require(window->visibility() == QWindow::Maximized, "Maximize state transition failed");
        check();
        capture(window, output, "maximized-" + mode);
        window->showNormal();
        settle();
        require(window->visibility() == QWindow::Windowed && window->geometry() == restored,
                "Restore failed to recover normal window geometry");
        check();
        click(window, "homeLearn", true);
        click(window, "homeGuideClose", true);
        resetScroll(window);
        // Reach the final Activity row and the stacked rail at minimum height.
        window->resize(1080, 720);
        settle();
        auto *row = item(window, mode == "mock" ? "home_activity_4_mock" : "home_artifacts_0_live");
        scrollTo(window, row);
        const auto rect = row->mapRectToItem(item(window, "homeWorkspace"), row->boundingRect());
        require(rect.top() >= 0 && rect.bottom() <= item(window, "homeWorkspace")->height(),
                "Lower Activity rows inaccessible");
        click(window, "homeTips", true);
        click(window, "homeGuideClose", true);
        require(!bridge.busy(), "Resize interactions dispatched runtime work");
    }
}
void nativeWindow(QQuickWindow *window, ObservedBridge &bridge, const QString &output) {
    require(QTest::qWaitForWindowExposed(window, 5000), "Native window not exposed by compositor");
    bridge.applyResult(snapshot(3));
    for (const auto &mode : QStringList{"mock", "live", "hybrid"}) {
        window->setProperty("uiMode", mode);
        window->resize(1536, 1024);
        resetScroll(window);
        QTest::qWait(200);
        require(QTest::qWaitForWindowExposed(window, 5000) && window->isExposed(),
                "Native compositor did not expose normal window (check session lock)");
        auto *learn = item(window, "homeLearn");
        learn->forceActiveFocus(Qt::TabFocusReason);
        std::cout << "Native window: " << window->width() << "x" << window->height()
                  << " DPR=" << window->devicePixelRatio() << " exposed=" << window->isExposed()
                  << " primary=" << item(window, "homePrimary")->width()
                  << " project=" << item(window, "homeProjectCard0")->width()
                  << " action=" << item(window, "homeActionCard0")->width() << std::endl;
        responsiveBounds(window);
        capture(window, output, "native-normal-" + mode);
        const auto normalSize = window->size();
        window->showMaximized();
        QTest::qWait(200);
        require(QTest::qWaitForWindowExposed(window, 5000) && window->isExposed(),
                "Native compositor did not expose maximized window");
        require(window->visibility() == QWindow::Maximized, "Native maximize state failed");
        responsiveBounds(window);
        capture(window, output, "native-maximized-" + mode);
        window->showNormal();
        QTest::qWait(200);
        require(QTest::qWaitForWindowExposed(window, 5000) && window->isExposed(),
                "Native compositor did not expose restored window");
        require(window->visibility() == QWindow::Windowed && window->size() == normalSize,
                "Native restore geometry failed");
        responsiveBounds(window);
        require(learn->hasActiveFocus(), "Native maximize/restore lost focus");
        click(window, "homeLearn", true);
        click(window, "homeGuideClose", true);
        require(!bridge.busy(), "Native interaction dispatched runtime work");
    }
}
void responsiveStates(QQuickWindow *window, ObservedBridge &bridge, const QString &output) {
    window->setProperty("workspace", "home");
    window->setProperty("uiMode", "live");
    // This fresh window has never received a confirmed snapshot.
    for (const auto &state : QStringList{"unconfirmed", "empty", "unconfirmed-empty", "stale"}) {
        if (state == "empty") {
            StudioResult empty;
            empty.snapshot.emplace();
            bridge.applyResult(empty);
        } else if (state != "unconfirmed") {
            if (state == "stale")
                bridge.applyResult(snapshot(3));
            StudioResult lost;
            lost.issues.push_back({"snapshot", "Fixture peer closed",
                                   mantis::Error{mantis::Status::io, "Fixture peer closed", "platform"}});
            bridge.applyResult(lost);
        }
        for (const auto &size : QList<QSize>{{1080, 720},
                                             {1280, 720},
                                             {1366, 768},
                                             {1440, 900},
                                             {1536, 1024},
                                             {1920, 1080},
                                             {2560, 1440},
                                             {3440, 1440},
                                             {3840, 2160}}) {
            window->resize(size);
            resetScroll(window);
            responsiveBounds(window);
            require(!item(window, "homeProjectGallery")->isVisible(), "Sample history leaked into live");
            const auto freshness = item(window, "homeFreshness")->property("text").toString();
            require(state == "empty" ? freshness == "Confirmed runtime snapshot"
                                     : freshness.contains(state == "unconfirmed" ? "Runtime state unconfirmed"
                                                                                 : "Last known"),
                    "Live state authority / stale label changed");
            capture(window, output, "responsive-live-" + state);
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
    activityEdgeCases(window, bridge, output);
    localActions(window, bridge, output);
    for (const QSize size : {QSize(1080, 720), QSize(1536, 1024), QSize(1920, 1080)}) {
        window->resize(size);
        for (const auto &mode : QStringList{"mock", "live", "hybrid"}) {
            window->setProperty("uiMode", mode);
            bridge.applyResult(snapshot(3));
            settle();
            resetScroll(window);
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
            for (const char *section : {"devices", "jobs", "artifacts", "events", "activity"})
                for (const auto &value : demoData.value(section).toList())
                    require(value.toMap().value("id").toString().isEmpty() &&
                                value.toMap().value("source") == "mock",
                            "Demo identity became actionable or live");
            require(model->data()["devicesCount"].toInt() == (mode == "mock" ? 0 : 3),
                    "Demo merged into live count");
            fidelity(window);
            activityContract(window);
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
    assetTransparency();
    responsive(window, bridge, output);
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
                                      {"homeJobs", "acquisition"}}) {
        window->setProperty("workspace", "home");
        settle();
        click(window, name, true);
        require(window->property("workspace") == route, "Wire CTA wrong");
        require(!bridge.capturing(), "Home started a capture");
    }
    window->setProperty("workspace", "home");
    resetScroll(window);
    activityContract(window);
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
        activityContract(window);
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
    qmlRegisterType<DevicesModel>("Mantis.Studio", 1, 0, "DevicesModel");
    qmlRegisterType<HomeModel>("Mantis.Studio", 1, 0, "HomeModel");
    qmlRegisterType<ProjectsModel>("Mantis.Studio", 1, 0, "ProjectsModel");
    qmlRegisterType<ProjectsScrollInput>("Mantis.Studio", 1, 0, "ProjectsScrollInput");
    qmlRegisterType<mantis::render::PointCloudView>("Mantis.Render", 1, 0, "PointCloudView");
    qmlRegisterType<MeasurementView>("Mantis.Render", 1, 0, "MeasurementView");
    try {
        const bool integration = argc > 2 && QString::fromLocal8Bit(argv[2]) == "wire";
        const QString task = argc > 2 ? QString::fromLocal8Bit(argv[2]) : "components";
        const bool hidpi = task == "hidpi";
        const QString output = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QDir::tempPath() + "/mantis-home";
        require(QDir().mkpath(output), "Cannot create output directory");
        if (task == "components")
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
        if (hidpi) {
            assetTransparency();
            responsive(window, bridge, output, true);
        } else if (task == "resize")
            resizeContinuously(window, bridge, output);
        else if (task == "states")
            responsiveStates(window, bridge, output);
        else if (task == "native")
            nativeWindow(window, bridge, output);
        else if (task == "layoutcheck" || task == "previewcheck") {
            window->resize(task == "layoutcheck" ? 3440 : 1536, 1024);
            window->setProperty("uiMode", "mock");
            resetScroll(window);
            if (task == "layoutcheck")
                responsiveBounds(window);
            else
                previewGeometry(window);
        } else if (integration)
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
