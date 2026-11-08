#include "studio_x1_roles_fixture.hpp"
#include "x1_role_controller.hpp"
#include <QAccessible>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTest>
#include <iostream>

#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(std::string("studio_x1_roles_qml.cpp:") + std::to_string(__LINE__) +    \
                                     ": " + #x);                                                             \
    } while (false)
using namespace x1_ui_test;
namespace {
QStringList warnings;
void handler(QtMsgType type, const QMessageLogContext &, const QString &text) {
    if ((type == QtWarningMsg || type == QtCriticalMsg) &&
        (text.contains("qml", Qt::CaseInsensitive) || text.contains("TypeError") ||
         text.contains("binding", Qt::CaseInsensitive)))
        warnings << text;
}
void until(const std::function<bool()> &fn) {
    QElapsedTimer timer;
    timer.start();
    while (!fn()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        if (timer.elapsed() > 10000)
            throw std::runtime_error("QML predicate deadline");
    }
    QCoreApplication::processEvents();
}
void idle(X1RoleController &c) {
    until([&] { return !c.state()["pending"].toBool(); });
}
void checkPanel() {
    auto fake = std::make_shared<Fake>();
    X1RoleController c(fake);
    c.setDeviceId(parentA);
    c.setVisible(true);
    c.setConnected(true);
    idle(c);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("roles", &c);
    engine.loadData(R"(
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "qrc:/ui/calibration"
ApplicationWindow {
    width: 620; height: 720; visible: true
    ScrollView {
        id: scroll; objectName: "roleTestScroll"; anchors.fill: parent; clip: true
        contentWidth: availableWidth
        ColumnLayout {
            width: scroll.availableWidth
            X1RolesPanel { controller: roles }
        }
    }
})",
                    QUrl("qrc:/role-test.qml"));
    CHECK(!engine.rootObjects().isEmpty());
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().front());
    CHECK(window);
    auto item = [&](const char *name) {
        auto *out = window->findChild<QQuickItem *>(name);
        CHECK(out);
        return out;
    };
    auto key = [&](const char *name) {
        auto *button = item(name);
        CHECK(button->isEnabled());
        button->forceActiveFocus(Qt::TabFocusReason);
        QTest::keyClick(window, Qt::Key_Space);
        QCoreApplication::processEvents();
    };
    auto *swap = item("x1RolesSwap");
    auto *accessible = QAccessible::queryAccessibleInterface(swap);
    CHECK(accessible && !accessible->text(QAccessible::Description).isEmpty());
    CHECK(item("x1RolesCurrent")->property("text").toString().contains("fixture/sensor-a"));
    CHECK(item("x1RolesCurrent")->property("text").toString().contains("sensor-a → csi-a → capture-a"));
    CHECK(!item("x1RolesApply")->isEnabled());
    key("x1RolesSwap");
    auto proposal = item("x1RolesProposed")->property("text").toString();
    CHECK(proposal.startsWith("LEFT\nPhysical identity: fixture/sensor-b"));
    swap->forceActiveFocus(Qt::TabFocusReason);
    QTest::keyClick(window, Qt::Key_Tab);
    CHECK(item("x1RolesValidate")->hasActiveFocus());
    QTest::keyClick(window, Qt::Key_Space);
    idle(c);
    CHECK(fake->validations.size() == 1);
    CHECK(!item("x1RolesReview")->isEnabled());
    fake->finish();
    c.refresh();
    idle(c);
    key("x1RolesReview");
    until([&] { return item("x1RolesAcknowledge")->hasActiveFocus(); });
    CHECK(item("x1RolesConfirmation")->isVisible());
    auto frozen = item("x1RolesConfirmationEvidence")->property("text").toString();
    CHECK(frozen.contains("Discovery: validated-discovery") && frozen.contains("Generation: 0"));
    CHECK(frozen.contains("PROPOSED\nLEFT\nPhysical identity: fixture/sensor-b"));
    CHECK(!item("x1RolesApply")->isEnabled());
    for (const auto &size : {QSize(420, 460), QSize(860, 720), QSize(620, 720)}) {
        window->resize(size);
        item("x1RolesAcknowledge")->forceActiveFocus(Qt::TabFocusReason);
        until([&] { return window->size() == size; });
        until([&] {
            const auto *focus = item("x1RolesAcknowledge");
            const auto position = focus->mapToItem(window->contentItem(), QPointF{});
            return position.y() >= -1 && position.y() + focus->height() <= window->height() + 1;
        });
        CHECK(item("x1RolesConfirmationEvidence")->property("text") == frozen);
        auto *scroll = window->findChild<QObject *>("roleTestScroll");
        CHECK(scroll);
        CHECK(scroll->property("contentWidth").toDouble() <=
              scroll->property("availableWidth").toDouble() + 1);
    }
    key("x1RolesAcknowledge");
    CHECK(item("x1RolesApply")->isEnabled());
    c.setConnected(false);
    QCoreApplication::processEvents();
    CHECK(!item("x1RolesApply")->isEnabled());
    CHECK(!item("x1RolesConfirmation")->isVisible());
    // Direct signal invocation cannot bypass either UI or controller guards.
    CHECK(QMetaObject::invokeMethod(item("x1RolesApply"), "clicked", Qt::DirectConnection));
    CHECK(fake->applies.empty());
    c.setConnected(true);
    idle(c);
    if (!c.state()["hasChange"].toBool())
        key("x1RolesSwap");
    key("x1RolesValidate");
    idle(c);
    fake->finish();
    c.refresh();
    idle(c);
    key("x1RolesReview");
    key("x1RolesAcknowledge");
    key("x1RolesApply");
    idle(c);
    CHECK(fake->applies.size() == 1);
    CHECK(!item("x1RolesConfirmation")->isVisible());
    fake->finish();
    c.refresh();
    idle(c);
    CHECK(item("x1RolesCurrent")
              ->property("text")
              .toString()
              .startsWith("LEFT\nPhysical identity: fixture/sensor-b"));
    // Unknown schemas and malformed partial replies render safely but never act.
    fake->edit([](Fake &f) {
        f.info.mutable_cameras()->RemoveLast();
        f.info.set_schema_version(99);
    });
    c.refresh();
    idle(c);
    CHECK(!item("x1RolesSwap")->isEnabled() && !item("x1RolesValidate")->isEnabled());
    CHECK(!window->grabWindow().isNull());
}
} // namespace
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    QAccessible::setActive(true);
    QQuickStyle::setStyle("Basic");
    qInstallMessageHandler(handler);
    try {
        checkPanel();
        for (const auto &w : warnings)
            std::cerr << w.toStdString() << '\n';
        CHECK(warnings.isEmpty());
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    std::cout
        << "Software-only role panel keyboard, exact confirmation, resize and stale-state checks passed\n";
}
