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
#include <QJSValue>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlProperty>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>
#include <QWheelEvent>
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
class WheelTrace : public QObject {
  public:
    explicit WheelTrace(QQuickItem *viewport) : viewport_(viewport) {
        timer_.setInterval(20);
        connect(&timer_, &QTimer::timeout, this, [this] {
            std::cout << QJsonDocument(
                             QJsonObject{{"coastSample", ++samples_},
                                         {"endTimestamp", static_cast<double>(lastEnd_)},
                                         {"contentY", viewport_->property("contentY").toDouble()},
                                         {"flicking", viewport_->property("flicking").toBool()},
                                         {"velocity", viewport_->property("verticalVelocity").toDouble()}})
                             .toJson(QJsonDocument::Compact)
                             .constData()
                      << std::endl;
            if (samples_ == 15)
                timer_.stop();
        });
    }

  private:
    QQuickItem *viewport_;
    QTimer timer_;
    int samples_ = 0;
    quint64 lastEnd_ = 0;
    bool eventFilter(QObject *, QEvent *event) override {
        if (event->type() == QEvent::Wheel) {
            const auto *wheel = static_cast<QWheelEvent *>(event);
            const QJsonObject data{
                {"timestamp", static_cast<double>(wheel->timestamp())},
                {"pixelX", wheel->pixelDelta().x()},
                {"pixelY", wheel->pixelDelta().y()},
                {"angleX", wheel->angleDelta().x()},
                {"angleY", wheel->angleDelta().y()},
                {"phase", static_cast<int>(wheel->phase())},
                {"source", static_cast<int>(wheel->source())},
                {"inverted", wheel->inverted()},
                {"deviceType", static_cast<int>(wheel->pointingDevice()->type())},
                {"contentY", viewport_->property("contentY").toDouble()},
                {"pointerX", wheel->position().x()},
                {"pointerY", wheel->position().y()},
                {"overViewport", viewport_->contains(viewport_->mapFromScene(wheel->position()))}};
            std::cout << QJsonDocument(data).toJson(QJsonDocument::Compact).constData() << std::endl;
            if (wheel->phase() == Qt::ScrollBegin)
                timer_.stop();
            if (wheel->phase() == Qt::ScrollEnd && lastEnd_ != wheel->timestamp()) {
                lastEnd_ = wheel->timestamp();
                samples_ = 0;
                timer_.start();
            }
        }
        return false;
    }
};
QStringList warnings;
void messages(QtMsgType type, const QMessageLogContext &, const QString &message) {
    if ((type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) &&
        !message.startsWith("QStandardPaths:"))
        warnings.push_back(message);
}
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
class Observed : public StudioBridge {
  public:
    explicit Observed(bool runtimeEnabled = false) : StudioBridge(nullptr, runtimeEnabled) {}
    using StudioBridge::applyPreview;
    using StudioBridge::applyResult;
    using StudioBridge::collectResult;
    using StudioBridge::projectGeneration;
};
class Fields : public QObject {
    Q_OBJECT
  public:
    void publish() {
        emit changed();
    }
  signals:
    void changed();
};
StudioResult snapshot(const QString &path = "/runtime/Projects/Verified.mantis", int count = 6) {
    StudioResult r;
    r.snapshot.emplace();
    r.snapshot->set_project_path(path.toStdString());
    for (int i = 0; i < count; ++i) {
        auto *a = r.snapshot->add_artifacts();
        a->set_id(QString("artifact-%1").arg(i, 5, 10, QChar('0')).toStdString());
        a->set_type(i % 2 ? "org.mantis.CalibrationTarget" : "org.mantis.RawCapture");
        a->set_state(i % 3 ? "FINALIZED" : "RECOVERABLE");
        a->set_chunks(static_cast<uint64_t>(i));
    }
    auto *j = r.snapshot->add_jobs();
    j->set_id("daemon-wide-job");
    j->set_name("Runtime job");
    j->set_state("Completed");
    auto *e = r.snapshot->add_events();
    e->set_kind("fixture.confirmed");
    e->set_message("Daemon-wide event; project association unavailable");
    return r;
}
StudioResult loss() {
    StudioResult r;
    r.issues.push_back({"snapshot", "Fixture peer closed",
                        mantis::Error{mantis::Status::io, "Fixture peer closed", "platform"}});
    return r;
}
QVariantMap first(const ProjectsModel &m) {
    auto a = m.data().value("artifacts").toList();
    check(!a.empty(), "Missing artifact");
    return a.front().toMap();
}
void modelContracts() {
    Observed bridge;
    ProjectsModel m;
    m.setBridge(&bridge);
    check(!m.data().value("available").toBool() && !m.data().value("hasSnapshot").toBool(),
          "Never confirmed invented project");
    bridge.applyResult(snapshot());
    check(m.data().value("confirmed").toBool() && m.data().value("snapshotCount") == 6,
          "Confirmed metadata missing");
    check(first(m).value("chunks") == "Unavailable", "Proto3 zero chunks acquired presence");
    check(m.data().value("artifacts").toList()[1].toMap().value("chunks") == "1",
          "Positive public uint64 chunk count unavailable");
    auto maximum = snapshot();
    maximum.snapshot->mutable_artifacts(1)->set_chunks(UINT64_MAX);
    bridge.applyResult(maximum);
    check(m.data().value("artifacts").toList()[1].toMap().value("chunks") == "18446744073709551615",
          "uint64 chunk count rounded through JavaScript");
    bridge.applyResult(snapshot());
    m.selectArtifact("artifact-00001");
    check(m.data().value("selectedId") == "artifact-00001", "Canonical selection failed");
    QSignalSpy equal(&m, &ProjectsModel::changed);
    bridge.applyResult(snapshot());
    check(equal.empty(), "Identical snapshots caused model churn");
    bridge.applyResult(loss());
    check(m.data().value("stale").toBool() && m.data().value("selectedId") == "artifact-00001",
          "Stale context lost provenance");
    bridge.applyResult(snapshot("/runtime/Projects/New.mantis"));
    check(m.data().value("selectedId").toString().isEmpty(), "Different project retained selection");
    m.setTypeFilter("org.mantis.RawCapture");
    check(m.data().value("matchingCount") == 3, "Type filter failed");
    m.setStateFilter("FINALIZED");
    check(m.data().value("matchingCount") == 2, "State filter failed");
    m.setQuery("artifact-00004");
    check(m.data().value("matchingCount") == 1, "Literal search failed");
    auto typed = snapshot("/typed.mantis", 3);
    typed.snapshot->mutable_artifacts(0)->set_type("org.mantis.Mesh");
    typed.snapshot->mutable_artifacts(1)->set_type("vendor.Unknown");
    typed.snapshot->mutable_artifacts(2)->set_type("org.mantis.Mesh\n");
    m.clearFilters();
    bridge.applyResult(typed);
    const auto classified = m.data().value("artifacts").toList();
    check(classified[0].toMap().value("mesh").toBool() && !classified[1].toMap().value("mesh").toBool() &&
              !classified[2].toMap().value("mesh").toBool(),
          "Unknown/sanitized type acquired classification");
    bridge.applyResult(snapshot());
    m.setQuery("([.*$");
    check(m.data().value("matchingCount") == 0, "Search interpreted regex");
    m.clearFilters();
    m.setSort("State");
    check(first(m).value("state") == "FINALIZED", "Stable supported sort failed");
    m.setSort("invalid");
    check(m.sort() == "State", "Unsupported sort accepted");
    m.clearFilters();
    auto hostile = snapshot("C:\\測定\\<b>Literal</b>\n\u202eProject.mantis", 1);
    hostile.snapshot->mutable_artifacts(0)->set_id("<b>ID</b>\n\xe2\x80\xae");
    bridge.applyResult(hostile);
    check(m.data().value("identity").toString().contains(QChar(0x202e)) &&
              !m.data().value("path").toString().contains(QChar(0x202e)),
          "Raw/display identities confused");
    check(first(m).value("displayId").toString().contains("<b>ID</b>") &&
              !first(m).value("displayId").toString().contains('\n'),
          "Hostile text not bounded literal");
    m.selectArtifact(first(m).value("id").toString());
    hostile.snapshot->set_project_path("C:\\測定\\<b>Literal</b>\n Project.mantis");
    bridge.applyResult(hostile);
    check(m.data().value("selectedId").toString().isEmpty(),
          "Sanitized identity collision retained selection");
    bridge.applyResult(snapshot(QString(12000, QChar('p')), 1));
    check(m.data().value("path").toString().size() == 4097, "Display path unbounded");
    bridge.applyResult(snapshot("", 0));
    check(m.data().value("confirmed").toBool() && !m.data().value("available").toBool(),
          "Missing confirmed path became a project");
    bridge.applyResult(snapshot("/empty.mantis", 0));
    check(m.data().value("artifactsAvailable").toBool() && m.data().value("snapshotCount") == 0,
          "Confirmed empty fabricated/unavailable count");
    Fields malformed;
    malformed.setProperty("connected", true);
    malformed.setProperty("hasSnapshot", true);
    malformed.setProperty("project", QString("/typed.mantis"));
    m.setBridge(&malformed);
    for (const auto &value : {QVariant{}, QVariant(42), QVariant(QVariantMap{})}) {
        malformed.setProperty("artifacts", value);
        malformed.publish();
        check(!m.data().value("artifactsAvailable").toBool(), "Malformed list accepted");
    }
    malformed.setProperty("artifacts", QVariantList{42, QVariantMap{{"id", 5}},
                                                    QVariantMap{{"id", "valid"}, {"chunks", "12"}},
                                                    QVariantMap{{"id", "valid"}}});
    malformed.publish();
    check(m.data().value("artifacts").toList().size() == 1 && first(m).value("chunks") == "Unavailable",
          "Malformed/duplicate rows accepted");
    m.setBridge(&bridge);
    auto large = snapshot("/large.mantis", 12000);
    bridge.applyResult(large);
    check(m.data().value("inspectedCount") == 512 && m.data().value("artifacts").toList().size() == 128 &&
              m.data().value("sampled").toBool() && m.data().value("renderLimited").toBool(),
          "Large metadata unbounded");
    m.setQuery("artifact-11999");
    check(m.data().value("matchingCount") == 0 &&
              m.data().value("summary").toString().contains("limited sample"),
          "Sample search falsely exhaustive");
    m.clearFilters();
    QElapsedTimer clock;
    clock.start();
    QSignalSpy updates(&m, &ProjectsModel::changed);
    for (int i = 0; i < 100; ++i)
        m.refresh();
    std::cout << "Scale: 12000 entries, processed=512 rendered=128; 100 identical presentations="
              << clock.elapsed() << "ms updates=" << updates.size() << "\n";
    check(updates.empty(), "Repeated normalized sample churned model");
    m.setBridge(nullptr);
    check(!m.data().value("hasSnapshot").toBool() && m.data().value("artifacts").toList().empty(),
          "Detached mock adapter retained authority");
}
void contextContracts() {
    Observed b;
    mantis::render::PointCloudView cloud;
    MeasurementView left, right;
    left.setSize({80, 80});
    right.setSize({80, 80});
    b.attachView(&cloud);
    b.attachPreview(&left, &right);
    auto a = snapshot("/A.mantis");
    mantis::data::Packet packet;
    packet.type = mantis::schema::points;
    mantis::data::Attribute position;
    position.descriptor.name = "org.mantis.position";
    position.descriptor.scalar = mantis::schema::ScalarType::f32;
    position.descriptor.shape = {1, 3};
    position.descriptor.stride = {12, 4};
    std::array<std::byte, 12> xyz{};
    position.buffer = mantis::memory::copy(xyz);
    packet.attributes.push_back(position);
    a.cloud = std::make_shared<const mantis::data::Packet>(packet);
    a.cloud_id = "old-cloud";
    a.cloud_project = "/A.mantis";
    b.applyResult(a);
    PreviewResult preview;
    preview.left = QImage(8, 8, QImage::Format_Grayscale8);
    preview.right = preview.left;
    preview.projectGeneration = b.projectGeneration();
    b.applyPreview(preview);
    check(b.selectedArtifact() == "old-cloud" && cloud.pointCount() == 1 && b.dualPreview() && left.ready(),
          "Initial project context absent");
    b.applyResult(loss());
    check(b.selectedArtifact() == "old-cloud" && left.ready(),
          "Unconfirmed transition erased last-known context");
    auto next = snapshot("/B.mantis");
    next.cloud = a.cloud;
    next.cloud_id = "old-cloud";
    next.cloud_project = "/A.mantis";
    b.applyResult(next);
    check(b.selectedArtifact().isEmpty() && cloud.pointCount() == 0 && !b.dualPreview() && !left.ready() &&
              !right.ready(),
          "Confirmed project switch retained old view");
    b.applyPreview(preview);
    check(!b.dualPreview() && !left.ready(), "Late old-project preview delivered");
    next.cloud_project = "/B.mantis";
    next.cloud_id = "new-cloud";
    b.applyResult(next);
    check(b.selectedArtifact() == "new-cloud", "Confirmed new-project cloud refused");
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
    auto *p = find(w->contentItem(), name);
    check(p, ("Missing " + name).toUtf8().constData());
    return p;
}
void settle() {
    QTest::qWait(30);
}
QObject *flick(QQuickWindow *w) {
    auto *p = item(w, "projectsGalleryViewport");
    check(p, "No vertical scroll");
    return p;
}
void top(QQuickWindow *w) {
    flick(w)->setProperty("contentY", 0);
    settle();
}
void reach(QQuickWindow *, QQuickItem *p) {
    for (auto *ancestor = p->parentItem(); ancestor; ancestor = ancestor->parentItem()) {
        if (!ancestor->objectName().endsWith("Viewport"))
            continue;
        const auto y = p->mapToItem(ancestor, QPointF(0, p->height() / 2)).y();
        if (y < 0 || y > ancestor->height())
            ancestor->setProperty(
                "contentY",
                std::clamp(
                    ancestor->property("contentY").toDouble() + y - ancestor->height() / 2, 0.0,
                    std::max(0.0, ancestor->property("contentHeight").toDouble() - ancestor->height())));
    }
    settle();
}
void click(QQuickWindow *w, const QString &name, bool keyboard = false) {
    auto *p = item(w, name);
    check(p->isVisible() && p->isEnabled(), ("Unreachable " + name).toUtf8().constData());
    reach(w, p);
    if (keyboard) {
        p->forceActiveFocus(Qt::TabFocusReason);
        QTest::keyClick(w, Qt::Key_Space);
    } else {
        auto point = p->mapToScene(QPointF(p->width() / 2, p->height() / 2));
        check(w->contentItem()->contains(point), "Control clipped outside window");
        QTest::mouseClick(w, Qt::LeftButton, Qt::NoModifier, point.toPoint());
    }
    settle();
}
void disabled(QQuickWindow *w) {
    for (const auto &name : QStringList{"projectsNew", "projectsOpen", "projectsImport", "projectsRecent",
                                        "projectsClone", "projectsExport", "projectsArchive",
                                        "projectsDelete", "projectsNetwork", "projectsCloud"}) {
        auto *p = item(w, name);
        check(!p->isEnabled(), "Unavailable control enabled");
        auto *a = QAccessible::queryAccessibleInterface(p);
        check(a && !a->text(QAccessible::Name).isEmpty() && !a->text(QAccessible::Description).isEmpty(),
              "Unavailable explanation inaccessible");
        QSignalSpy calls(p, SIGNAL(clicked()));
        if (p->isVisible()) {
            reach(w, p);
            const auto point = p->mapToScene(QPointF(p->width() / 2, p->height() / 2));
            QTest::mouseClick(w, Qt::LeftButton, Qt::NoModifier, point.toPoint());
            p->forceActiveFocus();
            QTest::keyClick(w, Qt::Key_Space);
            QTest::keyClick(w, Qt::Key_Return);
        }
        if (auto *actions = a->actionInterface())
            actions->doAction(QAccessibleActionInterface::pressAction());
        check(calls.empty(), "Unavailable mouse/keyboard/accessibility action dispatched");
    }
}
void capture(QQuickWindow *w, const QString &dir, const QString &state) {
    QTest::mouseMove(w, QPoint(10, 10));
    settle();
    const auto image = w->grabWindow();
    check(!image.isNull(), "Actual Qt screenshot missing");
    check(image.size() ==
              QSize(qRound(w->width() * w->devicePixelRatio()), qRound(w->height() * w->devicePixelRatio())),
          "Screenshot logical/DPR geometry wrong");
    const auto file =
        dir + "/" + state + "-" + QString::number(w->width()) + "x" + QString::number(w->height()) + ".png";
    check(image.save(file), "Screenshot save failed");
}
const QList<QSize> sizes{{1080, 720},  {1280, 720},  {1366, 768},  {1440, 900}, {1536, 1024},
                         {1920, 1080}, {2560, 1440}, {3440, 1440}, {3840, 2160}};
void bounds(QQuickWindow *w) {
    auto *root = item(w, "projectsWorkspace");
    auto *gallery = item(w, "projectsGallery");
    auto *inspector = item(w, "projectsInspector");
    auto *scroll = qobject_cast<QQuickItem *>(flick(w));
    check(w->minimumWidth() == 1080 && w->minimumHeight() == 720, "Global minimum changed");
    check(scroll->property("contentWidth").toDouble() <= scroll->width() + 1 &&
              std::abs(scroll->property("contentX").toDouble()) < 1,
          "Horizontal page overflow");
    if (root->property("multiPane").toBool()) {
        const auto rect = inspector->mapRectToItem(root, inspector->boundingRect());
        check(inspector->isVisible() && std::abs(root->width() - rect.right()) <= 15,
              "Inspector not pinned to right usable edge");
        check(inspector->width() >= 320 && inspector->width() <= 420, "Inspector proportions changed");
        check(item(w, "projectsNavigator")->width() >= 187 && item(w, "projectsNavigator")->width() <= 189,
              "Navigator proportions changed");
    } else
        check(item(w, "projectsDetailsToggle")->isVisible() && item(w, "projectsFiltersToggle")->isVisible(),
              "Collapsed panels undiscoverable");
    if (gallery->isVisible() && !root->property("listMode").toBool()) {
        const auto width = gallery->property("cardWidth").toDouble();
        if (width < 165 || width > 420)
            std::cerr << "Gallery bounds at check: window=" << w->width() << " workspace=" << root->width()
                      << " gallery=" << gallery->width() << " card=" << width << '\n';
        check(width >= 165 && width <= 420, "Gallery cards excessively narrow/wide");
        for (int i = 0; i < 12; ++i) {
            auto *card = item(w, "projectsMockCard" + QString::number(i));
            if (card->isVisible())
                check(card->x() + card->width() <= gallery->width() + 1, "Card exceeds gallery");
        }
    }
    auto *current = item(w, "projectsCurrentProject");
    check(!current->isVisible() || current->width() <= 421, "Current project card expanded unbounded");
    if (root->property("mode").toString() == "mock" && root->property("multiPane").toBool()) {
        auto *header = item(w, "projectsFiltersHeader");
        const auto bottom = header->mapRectToItem(root, header->boundingRect()).bottom();
        const auto top = gallery->mapRectToItem(root, gallery->boundingRect()).top();
        check(std::abs(top - bottom - 10) <= 1, "Spanning navigator displaced gallery from filters");
    }
    check(gallery->property("columns").toInt() >= 1 && gallery->property("columns").toInt() <= 6,
          "Gallery exceeded six columns");
    auto *table = item(w, "projectsContents");
    check(std::abs(table->mapRectToItem(root, table->boundingRect()).bottom() - root->height()) <= 1,
          "Contents left an unallocated strip above footer");
    for (const auto &n :
         QStringList{"projectsSearch", "projectsGrid", "projectsList", "projectsNew", "projectsOpen"}) {
        auto *p = item(w, n);
        const auto r = p->mapRectToItem(root, p->boundingRect());
        check(r.left() >= -1 && r.right() <= root->width() + 1 && r.top() >= -1 &&
                  r.bottom() <= root->height() + 1,
              "Toolbar clipped");
    }
    for (const auto &n : QStringList{"projectsSort", "projectsSource"}) {
        auto *p = item(w, n);
        auto *label = p->property("contentItem").value<QQuickItem *>();
        check(!p->isVisible() || (label && !label->property("truncated").toBool()),
              "Built-in sort/source label was elided by duplicate control padding");
    }
    std::cout << "Projects bounds: " << w->width() << "x" << w->height() << " DPR=" << w->devicePixelRatio()
              << " gallery=" << gallery->width() << " columns=" << gallery->property("columns").toInt()
              << " card=" << gallery->property("cardWidth").toDouble() << " rightGap="
              << root->width() - inspector->mapRectToItem(root, inspector->boundingRect()).right()
              << " inspector=" << inspector->width() << " multi=" << root->property("multiPane").toBool()
              << "\n";
}
void assets() {
    for (const auto &n :
         QStringList{"engine", "bracket-qc", "pipe", "enclosure", "gear", "manifold", "flange", "cover-qc"}) {
        QImage im(":/ui/projects/assets/" + n + ".png");
        check(!im.isNull() && im.hasAlphaChannel() && im.width() <= 640 && im.height() <= 430,
              "Asset decode/alpha bound wrong");
        int transparent = 0, opaque = 0, edge = 0;
        for (int y = 0; y < im.height(); ++y)
            for (int x = 0; x < im.width(); ++x) {
                const auto a = im.pixelColor(x, y).alpha();
                transparent += a == 0;
                opaque += a == 255;
                edge += a > 0 && a < 255;
                if (x < 8 || y < 8 || x >= im.width() - 8 || y >= im.height() - 8)
                    check(a == 0, "Foreground clipped");
            }
        check(transparent > 100 && opaque > 100 && edge > 10, "Asset transparency trivial");
    }
}
void interactions(QQuickWindow *w, Observed &b, const QString &out) {
    w->resize(1536, 1024);
    w->setProperty("uiMode", "mock");
    top(w);
    auto *root = item(w, "projectsWorkspace");
    auto *model = qobject_cast<ProjectsModel *>(root->property("liveModel").value<QObject *>());
    check(model && model->bridge() == nullptr, "Mock model polls runtime");
    for (bool keyboard : {false, true}) {
        const auto panelTop = item(w, "projectsContents")->mapToScene(QPointF()).y();
        click(w, "projectsMockCard1", keyboard);
        check(std::abs(item(w, "projectsContents")->mapToScene(QPointF()).y() - panelTop) <= 1,
              "Selection moved docked contents");
        check(root->property("sampleKey").toString() != "housing", "Demo selection failed");
        click(w, "projectsList", keyboard);
        check(root->property("listMode").toBool(), "List switch failed");
        capture(w, out, "mock-list");
        click(w, "projectsGrid", keyboard);
        click(w, "projectsFacetFavorites", keyboard);
        check(std::abs(item(w, "projectsContents")->mapToScene(QPointF()).y() - panelTop) <= 1,
              "Filter moved docked contents");
        check(root->property("sampleRows").value<QJSValue>().toVariant().toList().size() == 3,
              "Mock favorite filter failed");
        click(w, "projectsClear", keyboard);
        for (const auto &tab : QStringList{"Artifacts", "Versions", "Scans", "Meshes", "Textures",
                                           "CADModels", "Measurements", "Reports", "Notes"})
            click(w, "projectsTab" + tab, keyboard);
    }
    root->setProperty("listMode", true);
    item(w, "projectsMockCard0")->forceActiveFocus(Qt::TabFocusReason);
    for (int i = 0; i < 11; ++i)
        QTest::keyClick(w, Qt::Key_Down);
    settle();
    check(item(w, "projectsMockCard11")->hasActiveFocus(), "Gallery arrow navigation failed");
    // Qt layout/content-height delivery and the queued focus reveal can span
    // more than one frame, especially in an instrumented software renderer.
    // Await the actual visible geometry; a fixed 30ms delay isn't a UI contract.
    const auto cardRect = [&] {
        return item(w, "projectsMockCard11")
            ->mapRectToItem(qobject_cast<QQuickItem *>(flick(w)),
                            item(w, "projectsMockCard11")->boundingRect());
    };
    const bool revealed = QTest::qWaitFor(
        [&] {
            const auto r = cardRect();
            return r.top() >= -1 && r.bottom() <= qobject_cast<QQuickItem *>(flick(w))->height() + 1;
        },
        1000);
    const auto visibleCard = cardRect();
    if (visibleCard.top() < -1 || visibleCard.bottom() > qobject_cast<QQuickItem *>(flick(w))->height() + 1)
        std::cerr << "Focused card rect=" << visibleCard.top() << ":" << visibleCard.bottom()
                  << " viewport=" << qobject_cast<QQuickItem *>(flick(w))->height()
                  << " scrollY=" << flick(w)->property("contentY").toDouble()
                  << " contentHeight=" << flick(w)->property("contentHeight").toDouble() << '\n';
    check(revealed, "Keyboard-focused card did not scroll into view");
    capture(w, out, "mock-list-keyboard-last");
    root->setProperty("listMode", false);
    root->setProperty("mockQuery", "nonexistent");
    settle();
    check(root->property("sampleRows").value<QJSValue>().toVariant().toList().empty(), "Mock search failed");
    capture(w, out, "mock-empty-filter");
    click(w, "projectsClear", true);
    disabled(w);
    check(!b.busy() && !b.capturing(), "Mock interactions mutated bridge");
    b.applyResult(snapshot());
    w->setProperty("uiMode", "live");
    settle();
    check(model->bridge() == &b, "Live model detached");
    click(w, "projectsCurrentProject", true);
    click(w, "projectsArtifactRow1", true);
    check(model->data().value("selectedId") == "artifact-00001", "Read-only artifact selection failed");
    check(b.selectedArtifact().isEmpty(), "Projects loaded artifact into acquisition");
    capture(w, out, "live-artifact-inspector");
    for (const auto &tab :
         QStringList{"Versions", "Meshes", "Textures", "CADModels", "Measurements", "Reports", "Notes"}) {
        click(w, "projectsTab" + tab, true);
        check(item(w, "projectsContentsEmpty")->isVisible(), "Unsupported tab invented content");
    }
    click(w, "projectsTabArtifacts", true);
    click(w, "projectsSort", true);
    capture(w, out, "live-sort-menu");
    QTest::keyClick(w, Qt::Key_Down);
    QTest::keyClick(w, Qt::Key_Return);
    settle();
    check(model->sort() == "Type", "Actual sort control failed");
    click(w, "projectsClear", true);
    click(w, "projectsStateFilter", true);
    QTest::keyClick(w, Qt::Key_Down);
    QTest::keyClick(w, Qt::Key_Return);
    settle();
    check(model->stateFilter() == "FINALIZED" && model->data().value("matchingCount") == 4,
          "Actual state control failed");
    click(w, "projectsClear", true);
    click(w, "projectsTypeFilter", true);
    QTest::keyClick(w, Qt::Key_Down);
    QTest::keyClick(w, Qt::Key_Return);
    settle();
    check(!model->typeFilter().isEmpty() && model->data().value("matchingCount") == 3,
          "Actual type control failed");
    click(w, "projectsClear", true);
    auto *input = item(w, "projectsSearch");
    input->forceActiveFocus(Qt::TabFocusReason);
    for (const auto c : QByteArray("artifact-00004"))
        QTest::keyClick(w, c);
    settle();
    check(model->query() == "artifact-00004" && model->data().value("matchingCount") == 1,
          "Actual search field failed");
    click(w, "projectsClear", true);
    model->setQuery("artifact-00004");
    settle();
    check(model->data().value("matchingCount") == 1, "Live search missed descriptor");
    click(w, "projectsClear", true);
    disabled(w);
    click(w, "projectsInspectRuntime", true);
    check(w->property("workspace") == "acquisition" && !b.busy() && b.selectedArtifact().isEmpty(),
          "Truthful runtime navigation dispatched data");
    w->setProperty("workspace", "projects");
    w->setProperty("uiMode", "hybrid");
    settle();
    check(root->property("activeSource") == "live" && model->data().value("snapshotCount") == 6,
          "Hybrid merged metadata");
    click(w, "projectsSource", true);
    QTest::keyClick(w, Qt::Key_Down);
    QTest::keyClick(w, Qt::Key_Return);
    settle();
    check(root->property("activeSource") == "mock" && model->data().value("snapshotCount") == 6,
          "Source picker merged authority");
    click(w, "projectsMockCard2", true);
    check(root->property("activeSource") == "mock" && model->data().value("snapshotCount") == 6,
          "Demo acquired runtime authority");
    check(!item(w, "projectsInspectRuntime")->isEnabled(), "Hybrid sample became actionable");
    click(w, "projectsCurrentProject", true);
    check(root->property("activeSource") == "live", "Runtime source selection failed");
    w->resize(1080, 720);
    settle();
    click(w, "projectsDetailsToggle", true);
    check(item(w, "projectsInspector")->isVisible(), "Compact inspector inaccessible");
    capture(w, out, "compact-inspector");
    QTest::keyClick(w, Qt::Key_Escape);
    settle();
    check(!item(w, "projectsInspector")->isVisible() && item(w, "projectsDetailsToggle")->hasActiveFocus(),
          "Escape did not close/recover inspector focus");
    click(w, "projectsDetailsToggle", true);
    click(w, "projectsInspectRuntime", true);
    check(w->property("workspace") == "acquisition", "Compact inspector action inaccessible");
    w->setProperty("workspace", "projects");
    click(w, "projectsFiltersToggle", true);
    check(item(w, "projectsNavigator")->isVisible(), "Compact filters inaccessible");
    capture(w, out, "compact-filters");
    root->setProperty("detailsOpen", false);
    root->setProperty("filtersOpen", false);
    top(w);
    item(w, "projectsSearch")->forceActiveFocus(Qt::TabFocusReason);
    QTest::keyClick(w, Qt::Key_Tab);
    QTest::keyClick(w, Qt::Key_Backtab, Qt::ShiftModifier);
    check(item(w, "projectsSearch")->hasActiveFocus(), "Tab traversal lost search");
}
void scrollContracts(QQuickWindow *w, Observed &bridge, const QString &out) {
    auto *root = item(w, "projectsWorkspace");
    auto *gallery = item(w, "projectsGallery");
    auto *view = item(w, "projectsGalleryViewport");
    auto *table = item(w, "projectsTableViewport");
    auto *contents = item(w, "projectsContents");
    auto *inspector = item(w, "projectsInspectorViewport");
    const QStringList docks{"projectsNavigator", "projectsInspector", "projectsFiltersHeader",
                            "projectsContents",  "projectsTabStrip",  "projectsSearch"};
    const auto geometry = [&] {
        QList<QRectF> result;
        for (const auto &name : docks)
            result.append(item(w, name)->mapRectToScene(item(w, name)->boundingRect()));
        return result;
    };
    const auto stationary = [&](const QList<QRectF> &before) {
        const auto after = geometry();
        for (int i = 0; i < after.size(); ++i) {
            if (std::abs(before[i].x() - after[i].x()) > 1 || std::abs(before[i].y() - after[i].y()) > 1 ||
                std::abs(before[i].height() - after[i].height()) > 1)
                std::cerr << "Dock drift " << docks[i].toStdString() << " before=" << before[i].x() << ":"
                          << before[i].y() << ":" << before[i].width() << ":" << before[i].height()
                          << " after=" << after[i].x() << ":" << after[i].y() << ":" << after[i].width()
                          << ":" << after[i].height() << '\n';
            check(std::abs(before[i].x() - after[i].x()) <= 1 &&
                      std::abs(before[i].y() - after[i].y()) <= 1 &&
                      std::abs(before[i].height() - after[i].height()) <= 1,
                  "Scroll or tab change moved a docked region");
        }
    };
    quint64 timestamp = 1000;
    const auto wheel = [&](QQuickItem *target, const QPoint &pixel, const QPoint &angle,
                           Qt::ScrollPhase phase, bool processRelease = true) {
        const auto point = target->mapToScene(QPointF(target->width() - 25, target->height() / 2));
        QWheelEvent event(point, w->mapToGlobal(point.toPoint()), pixel, angle, Qt::NoButton, Qt::NoModifier,
                          phase, false,
                          pixel.isNull() && phase == Qt::NoScrollPhase ? Qt::MouseEventNotSynthesized
                                                                       : Qt::MouseEventSynthesizedBySystem);
        event.setTimestamp(timestamp += 16);
        QCoreApplication::sendEvent(w, &event);
        if (processRelease)
            QTest::qWait(20);
    };
    const auto gesture = [&](QQuickItem *target) {
        wheel(target, {}, {}, Qt::ScrollBegin);
        for (int i = 0; i < 4; ++i)
            wheel(target, QPoint(0, -32), QPoint(0, -16), Qt::ScrollUpdate);
        wheel(target, QPoint(0, -24), QPoint(0, -12), Qt::ScrollMomentum);
        wheel(target, {}, {}, Qt::ScrollEnd);
        settle();
    };
    // The measured Wayland device sends Begin/Update/End with no Momentum.
    // Assert an actual native Flickable coast, once, with Qt's own trajectory.
    w->setProperty("uiMode", "mock");
    w->resize(1536, 1024);
    root->setProperty("listMode", true);
    settle();
    auto *input = view->findChild<ProjectsScrollInput *>();
    check(input, "Native scroll input adapter missing");
    QSignalSpy coasts(input, &ProjectsScrollInput::nativeCoastStarted);
    view->setProperty("contentY", 200);
    wheel(view, {}, {}, Qt::ScrollBegin);
    wheel(view, QPoint(0, -3), {}, Qt::ScrollUpdate);
    check(std::abs(view->property("contentY").toDouble() - 203) <= 1,
          "First precise touchpad pixel was delayed or discarded");
    timestamp += 120; // No release coast for this deliberately paused probe.
    wheel(view, {}, {}, Qt::ScrollEnd);
    view->setProperty("contentY", 200);
    wheel(view, {}, {}, Qt::ScrollBegin);
    for (int i = 0; i < 4; ++i)
        wheel(view, QPoint(0, -12), QPoint(0, -6), Qt::ScrollUpdate);
    wheel(view, {}, {}, Qt::ScrollEnd);
    const auto releaseY = view->property("contentY").toDouble();
    QTest::qWait(100);
    check(coasts.size() == 1 && view->property("contentY").toDouble() > releaseY + 2,
          "Momentum-free phased touchpad release stopped abruptly");
    QMetaObject::invokeMethod(view, "cancelFlick");
    view->setProperty("contentY", 200);
    gesture(view);
    check(coasts.size() == 1, "Native platform momentum was overridden");
    QMetaObject::invokeMethod(view, "cancelFlick");
    wheel(view, {}, QPoint(0, -120), Qt::NoScrollPhase);
    check(coasts.size() == 1, "Stepped wheel acquired an artificial release flick");
    QMetaObject::invokeMethod(view, "cancelFlick");
    view->setProperty("contentY", 200);
    wheel(view, {}, {}, Qt::ScrollBegin);
    for (int i = 0; i < 4; ++i)
        wheel(view, QPoint(0, -12), QPoint(0, -6), Qt::ScrollUpdate);
    timestamp += 120;
    wheel(view, {}, {}, Qt::ScrollEnd);
    check(coasts.size() == 1, "Paused fingers caused an unintended coast");
    // Keyboard reveal / table context changes must also cancel an End callback
    // already queued for this viewport, before it starts a new native trajectory.
    wheel(view, {}, {}, Qt::ScrollBegin);
    for (int i = 0; i < 4; ++i)
        wheel(view, QPoint(0, -12), {}, Qt::ScrollUpdate);
    wheel(view, {}, {}, Qt::ScrollEnd, false);
    check(QMetaObject::invokeMethod(view, "cancelScroll"), "Viewport cancellation API missing");
    settle();
    check(coasts.size() == 1, "Queued release survived a focus/context cancellation");
    const auto releaseAtSpeed = [&](int delta) {
        QMetaObject::invokeMethod(view, "cancelFlick");
        view->setProperty("contentY", 100);
        const auto before = coasts.size();
        wheel(view, {}, {}, Qt::ScrollBegin);
        for (int i = 0; i < 4; ++i)
            wheel(view, QPoint(0, -delta), QPoint(0, -delta), Qt::ScrollUpdate);
        wheel(view, {}, {}, Qt::ScrollEnd);
        check(coasts.size() == before + 1, "Speed-dependent release missing");
        const auto velocity = std::abs(coasts.last()[0].toDouble());
        const auto y = view->property("contentY").toDouble();
        QTest::qWait(100);
        return QPair<double, double>{velocity, view->property("contentY").toDouble() - y};
    };
    const auto gentle = releaseAtSpeed(8); // 500 logical px/s
    const auto fast = releaseAtSpeed(64);  // 4000 logical px/s, measured on this touchpad
    std::cout << "Native release gentle/fast: velocity=" << gentle.first << "/" << fast.first
              << " travel=" << gentle.second << "/" << fast.second << '\n';
    check(fast.first >= gentle.first * 6 && fast.second > gentle.second * 4,
          "Distinct gentle/fast gestures collapsed into the same release speed");
    QMetaObject::invokeMethod(view, "cancelFlick");
    root->setProperty("listMode", false);
    settle();
    bridge.applyResult(snapshot("/runtime/Bounded.mantis", 128));
    for (const auto &mode : QStringList{"mock", "live", "hybrid"}) {
        w->setProperty("uiMode", mode);
        w->resize(1536, 1024);
        settle();
        const auto sampleRows = root->property("sampleRows").value<QJSValue>().toVariant().toList();
        for (const auto &size : sizes) {
            w->resize(size);
            settle();
            check(!w->grabWindow().isNull(), "Count-matrix frame unavailable");
            root->setProperty("listMode", false);
            for (int count : {0, 1, 2, 3, 4, 5, 6, 12}) {
                QVariantList rows;
                for (int i = 0; i < count; ++i)
                    rows.append(sampleRows[i]);
                gallery->setProperty("rows", rows);
                QTest::qWait(5);
                check(gallery->property("columns").toInt() >= 1 && gallery->property("columns").toInt() <= 6,
                      "Result-count/source/size matrix exceeded six columns");
                check(gallery->property("cardWidth").toDouble() <= (count <= 2 ? 320 : 420),
                      "Filtered card stretched");
                root->setProperty("listMode", true);
                check(gallery->property("columns").toInt() == 1, "List has multiple columns");
                root->setProperty("listMode", false);
            }
        }
        gallery->setProperty("rows", sampleRows);
        w->resize(1536, 1024);
        settle();
        // Offscreen Qt 6.9 needs rendered frames to commit nested layout/anchor
        // polish. Flush the resize before measuring tab-only stability.
        const bool layoutSettled = QTest::qWaitFor(
            [&] {
                check(!w->grabWindow().isNull(), "Docked frame unavailable");
                return std::abs(item(w, "projectsInspector")->width() -
                                std::clamp(root->width() * .27, 320.0, 420.0)) <= 1;
            },
            2000);
        check(layoutSettled, "Inspector did not settle after resize");
        settle();
        if (mode == "mock") {
            const QList<QColor> expected{QColor("#3996ed"), QColor("#aa6de3"), QColor("#ef963e"),
                                         QColor("#24c88d"), QColor("#edc94c"), QColor("#94a1aa")};
            int total = 0;
            for (int i = 0; i < 6; ++i) {
                check(item(w, "projectsTagDot" + QString::number(i))->property("color").value<QColor>() ==
                          expected[i],
                      "Semantic tag dot color changed");
                total +=
                    item(w, "projectsTagCount" + QString::number(i))->property("text").toString().toInt();
            }
            check(total == 12, "Illustrative tag counts do not match local fixture");
        } else if (mode == "live")
            check(!item(w, "projectsTagDot0")->isVisible() &&
                      item(w, "projectsTagCount0")->property("text").toString().isEmpty(),
                  "Live invented persistent tags/counts");
        root->setProperty("activeSource", mode == "mock" ? "mock" : "live");
        view->setProperty("contentY", 0);
        const auto docksBefore = geometry();
        const auto panelY = contents->mapToScene(QPointF()).y();
        for (int cycle = 0; cycle < 30; ++cycle) {
            for (const auto &tab : QStringList{"Versions", "Scans", "Artifacts", "Meshes", "Textures",
                                               "CAD Models", "Measurements", "Reports", "Notes"}) {
                contents->setProperty("tab", tab);
                QTest::qWait(5);
                stationary(docksBefore);
                check(std::abs(contents->mapToScene(QPointF()).y() - panelY) <= 1, "Tab bar drifted");
            }
        }
        root->setProperty("mockSort", "Name");
        root->setProperty("mockFacet", "Favorites");
        root->setProperty("sampleKey", "rotor");
        for (const auto &tab : QStringList{"Versions", "Scans", "Artifacts", "Meshes", "Textures",
                                           "CAD Models", "Measurements", "Reports", "Notes"}) {
            contents->setProperty("tab", tab);
            settle();
            stationary(docksBefore);
        }
        root->setProperty("mockSort", "Study order");
        root->setProperty("mockFacet", "All");
        root->setProperty("sampleKey", "housing");
        contents->setProperty("tab", mode == "mock" ? "Versions" : "Artifacts");
        settle();
        bounds(w);
        capture(w, out, mode + "-docked");
        if (mode != "live") {
            root->setProperty("activeSource", "mock");
            for (bool list : {false, true}) {
                root->setProperty("listMode", list);
                settle();
                check(view->property("clip").toBool(), "Gallery does not clip");
                view->setProperty("contentY", 0);
                capture(w, out, mode + (list ? "-list-top" : "-grid-top"));
                const auto before = geometry();
                const auto tableY = table->property("contentY").toDouble();
                const auto inspectorY = inspector->property("contentY").toDouble();
                gesture(view);
                check(view->property("contentY").toDouble() > 1,
                      "Precise phased touchpad wheel missed gallery");
                stationary(before);
                check(table->property("contentY").toDouble() == tableY &&
                          inspector->property("contentY").toDouble() == inspectorY,
                      "Gallery wheel escaped into other scrollers");
                const auto updateY = view->property("contentY").toDouble();
                wheel(view, {}, {}, Qt::ScrollBegin);
                for (int i = 0; i < 4; ++i)
                    wheel(view, QPoint(0, 24), QPoint(0, 12), Qt::ScrollUpdate);
                wheel(view, {}, {}, Qt::ScrollEnd);
                check(view->property("contentY").toDouble() < updateY,
                      "Natural-direction pixel input was reversed or ignored");
                check(std::abs(view->property("contentX").toDouble()) < 1, "Gallery scrolled horizontally");
                // Measure tab displacement independently of an ongoing native coast.
                QMetaObject::invokeMethod(view, "cancelFlick");
                const auto galleryY = view->property("contentY").toDouble();
                contents->setProperty("tab", "Notes");
                settle();
                check(std::abs(view->property("contentY").toDouble() - galleryY) <= 1,
                      "Tab reset gallery position");
                stationary(before);
                contents->setProperty("tab", "Scans");
                settle();
                capture(w, out, mode + "-scans");
                contents->setProperty("tab", "Measurements");
                settle();
                capture(w, out, mode + "-empty");
                contents->setProperty("tab", "Versions");
                settle();
                view->setProperty("contentY",
                                  std::max(0.0, view->property("contentHeight").toDouble() - view->height()));
                settle();
                capture(w, out, mode + (list ? "-list-last" : "-grid-last"));
                stationary(before);
                // Tabs deliberately reset their own table offset. Measure the boundary
                // gesture after tab changes, separately from that context transition.
                const auto boundaryTableY = table->property("contentY").toDouble();
                gesture(view);
                stationary(before);
                check(table->property("contentY").toDouble() == boundaryTableY, "Boundary wheel moved table");
                if (list) {
                    auto *last = item(w, "projectsMockCard11");
                    last->forceActiveFocus(Qt::TabFocusReason);
                    settle();
                    const auto r = last->mapRectToItem(view, last->boundingRect());
                    check(r.top() >= -1 && r.bottom() <= view->height() + 1, "Last project inaccessible");
                }
            }
        }
        if (mode != "mock") {
            root->setProperty("activeSource", "live");
            contents->setProperty("tab", "Artifacts");
            settle();
            table->setProperty("contentY", 0);
            capture(w, out, mode + "-table-top");
            const auto galleryY = view->property("contentY").toDouble();
            const auto before = geometry();
            gesture(table);
            check(table->property("contentY").toDouble() > 1, "Table did not own precise scroll input");
            check(std::abs(view->property("contentY").toDouble() - galleryY) <= 1,
                  "Table wheel moved gallery");
            stationary(before);
            wheel(table, {}, QPoint(0, -120), Qt::NoScrollPhase);
            check(table->property("contentY").toDouble() > 1, "Stepped mouse wheel failed");
            item(w, "projectsArtifactRow0")->forceActiveFocus(Qt::TabFocusReason);
            for (int i = 0; i < 127; ++i)
                QTest::keyClick(w, Qt::Key_Down);
            auto *last = item(w, "projectsArtifactRow127");
            settle();
            check(last->hasActiveFocus(), "Artifact arrows lost stable delegate focus");
            const auto r = last->mapRectToItem(table, last->boundingRect());
            check(r.top() >= -1 && r.bottom() <= table->height() + 1,
                  "Last bounded artifact inaccessible by keyboard");
            capture(w, out, mode + "-table-last");
            check(std::abs(view->property("contentY").toDouble() - galleryY) <= 1,
                  "Table focus reveal moved gallery");
            stationary(before);
        }
        inspector->setProperty("contentY", 0);
        if (inspector->property("contentHeight").toDouble() > inspector->height()) {
            const auto galleryY = view->property("contentY").toDouble();
            const auto tableY = table->property("contentY").toDouble();
            gesture(inspector);
            check(inspector->property("contentY").toDouble() > 1, "Inspector overflow failed");
            check(view->property("contentY").toDouble() == galleryY &&
                      table->property("contentY").toDouble() == tableY,
                  "Inspector wheel escaped");
        }
        std::cout << "Dock/scroll/tab/count routing passed: " << mode.toStdString() << '\n';
    }
}

void native(QQuickWindow *w, Observed &b, const QString &out) {
    b.applyResult(snapshot());
    for (const auto &mode : QStringList{"mock", "live", "hybrid"}) {
        w->setProperty("uiMode", mode);
        w->resize(1536, 1024);
        top(w);
        check(QTest::qWaitForWindowExposed(w, 5000) && w->isExposed(),
              "Native compositor did not expose window");
        capture(w, out, "native-normal-" + mode);
        bounds(w);
        const auto normal = w->size();
        item(w, "projectsSearch")->forceActiveFocus(Qt::TabFocusReason);
        w->showMaximized();
        check(QTest::qWaitFor([&] { return w->isExposed() && w->visibility() == QWindow::Maximized; }, 5000),
              "Native maximize failed");
        capture(w, out, "native-maximized-" + mode);
        bounds(w);
        w->showNormal();
        check(
            QTest::qWaitFor(
                [&] { return w->isExposed() && w->visibility() == QWindow::Windowed && w->size() == normal; },
                5000),
            "Native restore failed");
        capture(w, out, "native-restored-" + mode);
        bounds(w);
        auto *search = item(w, "projectsSearch");
        std::cout << "Native focus: mode=" << mode.toStdString() << " active=" << w->isActive() << " item="
                  << (w->activeFocusItem() ? w->activeFocusItem()->objectName().toStdString() : "null")
                  << " searchFocus=" << search->hasFocus() << " searchActive=" << search->hasActiveFocus()
                  << std::endl;
        check(search->hasActiveFocus(), "Native maximize lost focus");
        click(w, "projectsList", true);
        click(w, "projectsGrid", true);
        check(!b.busy(), "Native navigation dispatched runtime work");
    }
}
void responsive(QQuickWindow *w, Observed &b, const QString &out, bool dpi) {
    b.applyResult(snapshot());
    auto *root = item(w, "projectsWorkspace");
    QList<QPointer<QQuickItem>> persistent;
    for (int i = 0; i < 12; ++i)
        persistent.push_back(item(w, "projectsMockCard" + QString::number(i)));
    for (int i = 0; i < 128; ++i)
        persistent.push_back(item(w, "projectsArtifactRow" + QString::number(i)));
    for (const auto &mode : QStringList{"mock", "live", "hybrid"}) {
        w->setProperty("uiMode", mode);
        root->setProperty("listMode", false);
        item(w, "projectsSearch")->forceActiveFocus(Qt::TabFocusReason);
        for (const auto &size : sizes) {
            w->resize(size);
            top(w);
            bounds(w);
            for (const auto &p : persistent)
                check(p, "Resize/mode switch destroyed delegate");
            check(item(w, "projectsSearch")->hasActiveFocus(), "Resize/mode switch lost focus");
            capture(w, out, mode);
        }
        if (mode == "mock") {
            root->setProperty("mockQuery", "Engine Block");
            settle();
            bounds(w);
            check(item(w, "projectsGallery")->property("cardWidth").toDouble() <= 320,
                  "Single search result stretched across ultrawide gallery");
            capture(w, out, "mock-single-result");
            root->setProperty("mockQuery", "");
        }
    }
    if (dpi)
        check(std::abs(w->devicePixelRatio() - qEnvironmentVariable("QT_SCALE_FACTOR").toDouble()) < .01,
              "DPI scale fixture failed");
}
void states(QQuickWindow *w, Observed &b, const QString &out) {
    w->setProperty("uiMode", "live");
    for (const auto &name :
         QStringList{"never-confirmed", "missing-identity", "empty", "stale", "recovered", "hybrid-stale"}) {
        if (name == "missing-identity")
            b.applyResult(snapshot("", 0));
        if (name == "empty")
            b.applyResult(snapshot("/empty.mantis", 0));
        if (name == "stale" || name == "hybrid-stale") {
            b.applyResult(snapshot());
            b.applyResult(loss());
        }
        if (name == "recovered")
            b.applyResult(snapshot("/runtime/Reconnected.mantis"));
        if (name == "hybrid-stale")
            w->setProperty("uiMode", "hybrid");
        for (const auto &size : sizes) {
            w->resize(size);
            top(w);
            bounds(w);
            capture(w, out, name);
        }
    }
}
void resize(QQuickWindow *w, Observed &b, const QString &out) {
    b.applyResult(snapshot());
    auto *root = item(w, "projectsWorkspace");
    QPointer<QQuickItem> selected = item(w, "projectsMockCard0"), search = item(w, "projectsSearch"),
                         row = item(w, "projectsArtifactRow1");
    const int widths[]{1080, 1248, 1252, 1300, 1318, 1322, 1350, 1366, 1440, 1536, 1920, 2560, 3440, 3840};
    for (const auto &mode : QStringList{"mock", "live", "hybrid"}) {
        w->setProperty("uiMode", mode);
        search->forceActiveFocus(Qt::TabFocusReason);
        const auto sample = root->property("sampleKey");
        for (int pass = 0; pass < 3; ++pass)
            for (int direction : {1, -1})
                for (int i = 0; i < 14; ++i) {
                    w->resize(widths[direction == 1 ? i : 13 - i], 900);
                    settle();
                    bounds(w);
                    check(selected && row && search->hasActiveFocus() &&
                              root->property("sampleKey") == sample,
                          "Resize changed identity/focus/lifetime");
                }
        w->resize(1536, 1024);
        top(w);
        const auto normal = w->geometry();
        w->showMaximized();
        settle();
        bounds(w);
        capture(w, out, "maximized-" + mode);
        w->showNormal();
        settle();
        check(w->geometry() == normal, "Restore geometry failed");
        root->setProperty("listMode", true);
        w->resize(1080, 720);
        top(w);
        reach(w, item(w, "projectsTabNotes"));
        bounds(w);
        root->setProperty("listMode", false);
    }
}
void wire(QQuickWindow *w, Observed &observed, const QString &out) {
    Observed bridge(true);
    w->setProperty("studioBridge", QVariant::fromValue(&bridge));
    w->setProperty("uiMode", "live");
    auto *root = item(w, "projectsWorkspace");
    auto *m = qobject_cast<ProjectsModel *>(root->property("liveModel").value<QObject *>());
    const auto wait = [&](auto condition) {
        QElapsedTimer t;
        t.start();
        while (!condition() && t.elapsed() < 6000)
            QTest::qWait(10);
        check(condition(), "Wire state deadline failed");
    };
    const mantis::client::Client client;
    const auto mode = [&](const std::string &value) {
        mantis::wire::v1::Request r;
        r.mutable_plugin_enable()->set_id("projects-fixture:" + value);
        (void)client.call(r);
        bridge.refresh();
    };
    wait([&] { return bridge.connected() && !bridge.busy(); });
    check(m->data().value("name") == "Wire.mantis" && m->data().value("snapshotCount") == 6,
          "Public client metadata missing");
    disabled(w);
    click(w, "projectsCurrentProject", true);
    click(w, "projectsArtifactRow1", true);
    click(w, "projectsList", true);
    click(w, "projectsGrid", true);
    click(w, "projectsClear", true);
    check(!bridge.capturing() && bridge.selectedArtifact().isEmpty(), "Passive Projects command/data leak");
    capture(w, out, "wire-confirmed");
    wait([&] { return !bridge.busy(); });
    bridge.runPipeline("projects-reject");
    bridge.runPipeline("forbidden-duplicate");
    wait([&] { return !bridge.busy() && bridge.error().contains("Rejected fixture operation"); });
    check(bridge.connected() &&
              m->data().value("issues").toList().front().toMap().value("phase") == "operation",
          "Rejection lost phase/confirmation");
    capture(w, out, "wire-rejected");
    mode("offline");
    wait([&] { return !bridge.connected() && !bridge.busy(); });
    check(m->data().value("stale").toBool() && m->data().value("name") == "Wire.mantis",
          "Transport loss fabricated/erased data");
    capture(w, out, "wire-stale");
    disabled(w);
    mode("new");
    wait([&] { return bridge.connected() && !bridge.busy() && m->data().value("name") == "New.mantis"; });
    check(m->data().value("selectedId").toString().isEmpty() && bridge.selectedArtifact().isEmpty(),
          "External project change retained selection");
    capture(w, out, "wire-reconnected-new");
    mode("auth");
    wait([&] { return !bridge.connected() && !bridge.busy(); });
    check(bridge.error().contains("Invalid local access token"), "Authentication denial hidden");
    capture(w, out, "wire-auth-denied");
    mode("new");
    wait([&] { return bridge.connected() && !bridge.busy(); });
    // The GUI safety gate remains inert even while confirmed; no project_open exists on the bridge.
    disabled(w);
    check(bridge.metaObject()->indexOfMethod("openProject(QString,bool)") < 0,
          "Unsafe mutation seam exposed");
    const auto rejected = Observed::collectResult(client, {}, "old-project-id", {}, "/runtime/Wire.mantis");
    check(rejected.snapshot && !rejected.artifactAttempted,
          "Old-context selection loaded after external switch");
    click(w, "projectsArtifactRow1", true);
    const auto uncertain = Observed::collectResult(client, [](const auto &c) {
        mantis::wire::v1::Request request;
        request.mutable_project_open()->set_path("/runtime/Ambiguous.mantis");
        request.mutable_project_open()->set_create(true);
        (void)c.call(request);
    });
    check(uncertain.operationAttempted && uncertain.snapshot && uncertain.issues.size() == 1 &&
              uncertain.issues.front().phase == "operation",
          "Unknown operation outcome was retried or lost its root cause");
    bridge.applyResult(uncertain);
    check(m->data().value("name") == "Ambiguous.mantis" && m->data().value("selectedId").toString().isEmpty(),
          "Follow-up confirmation retained stale project identity");
    capture(w, out, "wire-uncertain-confirmed");
    disabled(w);
    w->setProperty("studioBridge", QVariant::fromValue(&observed));
}
} // namespace
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    QAccessible::setActive(true);
    QQuickStyle::setStyle("Basic");
    qInstallMessageHandler(messages);
    qmlRegisterType<DevicesModel>("Mantis.Studio", 1, 0, "DevicesModel");
    qmlRegisterType<HomeModel>("Mantis.Studio", 1, 0, "HomeModel");
    qmlRegisterType<ProjectsModel>("Mantis.Studio", 1, 0, "ProjectsModel");
    qmlRegisterType<ProjectsScrollInput>("Mantis.Studio", 1, 0, "ProjectsScrollInput");
    qmlRegisterType<mantis::render::PointCloudView>("Mantis.Render", 1, 0, "PointCloudView");
    qmlRegisterType<MeasurementView>("Mantis.Render", 1, 0, "MeasurementView");
    try {
        const auto out = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QDir::tempPath() + "/mantis-projects";
        check(QDir().mkpath(out), "Evidence directory unavailable");
        const auto task = argc > 2 ? QString::fromLocal8Bit(argv[2]) : QString("qml");
        if (task == "qml") {
            modelContracts();
            contextContracts();
            assets();
        }
        Observed b;
        CalibrationController calibration(
            std::make_shared<PublicCalibrationClient>(mantis::client::Client{mantis::client::Endpoint{}}));
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("studio", &b);
        engine.rootContext()->setContextProperty("calibration", &calibration);
        engine.setInitialProperties({{"uiMode", "live"}, {"workspace", "projects"}});
        engine.load(task == "trace-baseline" && argc > 3
                        ? QUrl::fromLocalFile(QString::fromLocal8Bit(argv[3]))
                        : QUrl("qrc:/ui/shell/Main.qml"));
        check(!engine.rootObjects().empty(), "Projects shell failed");
        auto *w = qobject_cast<QQuickWindow *>(engine.rootObjects().front());
        check(w, "No window");
        settle();
        if (task == "trace" || task == "trace-baseline") {
            w->setProperty("uiMode", "mock");
            w->resize(1536, 1024);
            item(w, "projectsWorkspace")->setProperty("listMode", true);
            auto *viewport = task == "trace-baseline"
                                 ? item(w, "projectsScroll")->property("contentItem").value<QQuickItem *>()
                                 : item(w, "projectsGalleryViewport");
            WheelTrace trace(viewport);
            w->installEventFilter(&trace);
            std::cout << "Native input trace: Qt=" << qVersion()
                      << " platform=" << QGuiApplication::platformName().toStdString() << " injected=false"
                      << std::endl;
            QTest::qWait(90000);
            capture(w, out, task);
        } else if (task == "fidelity") {
            w->setProperty("uiMode", "mock");
            w->resize(3440, 1440);
            settle();
            auto *gallery = item(w, "projectsGallery");
            auto *panel = item(w, "projectsContents");
            const auto initial = panel->mapToScene(QPointF());
            const int columns = gallery->property("columns").toInt();
            panel->setProperty("tab", "Scans");
            settle();
            const double drift = std::abs(panel->mapToScene(QPointF()).y() - initial.y());
            const bool viewport = find(w->contentItem(), "projectsGalleryViewport") != nullptr;
            std::cout << "Fidelity: columns=" << columns << " tab drift=" << drift
                      << " independent gallery viewport=" << viewport << '\n';
            check(columns <= 6 && drift <= 1 && viewport,
                  "Column cap / stationary contents / independent gallery regression");
        } else if (task == "inspect") {
            for (const auto &n :
                 QStringList{"projectsWorkspace", "projectsGalleryViewport", "projectsNavigator",
                             "projectsGallery", "projectsInspector", "projectsContents"}) {
                auto *p = item(w, n);
                std::cout << n.toStdString() << " x=" << p->x() << " y=" << p->y() << " w=" << p->width()
                          << " h=" << p->height() << " iw=" << p->implicitWidth()
                          << " ih=" << p->implicitHeight()
                          << " preferred=" << QQmlProperty(p, "Layout.preferredWidth").read().toDouble()
                          << " min=" << QQmlProperty(p, "Layout.minimumWidth").read().toDouble()
                          << " visible=" << p->isVisible() << "\n";
                for (auto *ancestor = p->parentItem(); ancestor; ancestor = ancestor->parentItem())
                    std::cout << "  " << ancestor->metaObject()->className() << " x=" << ancestor->x()
                              << " y=" << ancestor->y() << " w=" << ancestor->width()
                              << " h=" << ancestor->height() << " ih=" << ancestor->implicitHeight() << "\n";
            }
            w->setProperty("uiMode", "mock");
            top(w);
            capture(w, out, "inspect");
        } else if (task == "states")
            states(w, b, out);
        else if (task == "resize")
            resize(w, b, out);
        else if (task == "scroll")
            scrollContracts(w, b, out);
        else if (task == "native")
            native(w, b, out);
        else if (task == "wire")
            wire(w, b, out);
        else if (task == "hidpi")
            responsive(w, b, out, true);
        else {
            interactions(w, b, out);
            responsive(w, b, out, false);
            w->setProperty("workspace", "home");
            w->setProperty("uiMode", "mock");
            w->resize(1536, 1024);
            capture(w, out, "home-unchanged");
        }
        check(warnings.empty(), "Qt/QML warnings in supported Projects paths");
        std::cout << "PASS: Projects " << task.toStdString() << " contracts and actual Qt evidence\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        for (const auto &m : warnings)
            std::cerr << m.toStdString() << '\n';
        return 1;
    }
    return 0;
}
#include "studio_projects.moc"
