#include "bridge.hpp"
#include "calibration_controller.hpp"
#include "devices_model.hpp"
#include "home_model.hpp"
#include "live_views.hpp"
#include "projects_model.hpp"
#include "projects_scroll.hpp"
#include "scan_model.hpp"
#include "studio_calibration_fixture.hpp"
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QThread>
#include <iostream>
static QStringList warnings;
void handler(QtMsgType type, const QMessageLogContext &, const QString &message) {
    if ((type == QtWarningMsg || type == QtCriticalMsg) &&
        (message.contains("qml", Qt::CaseInsensitive) || message.contains("binding", Qt::CaseInsensitive) ||
         message.contains("TypeError")))
        warnings.push_back(message);
}
bool hasStage(QQuickItem *item, const QString &name) {
    if (item->objectName() == name)
        return true;
    for (auto *child : item->childItems())
        if (hasStage(child, name))
            return true;
    return false;
}
bool until(const std::function<bool()> &condition) {
    QElapsedTimer timer;
    timer.start();
    while (!condition()) {
        QCoreApplication::processEvents();
        QThread::msleep(1);
        if (timer.elapsed() > 5000)
            return false;
    }
    return true;
}
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    QQuickStyle::setStyle("Basic");
    qmlRegisterType<LiveViews>("Mantis.Studio", 1, 0, "LiveViews");
    qmlRegisterType<ScanModel>("Mantis.Studio", 1, 0, "ScanModel");
    qmlRegisterType<DevicesModel>("Mantis.Studio", 1, 0, "DevicesModel");
    qmlRegisterType<HomeModel>("Mantis.Studio", 1, 0, "HomeModel");
    qmlRegisterType<ProjectsModel>("Mantis.Studio", 1, 0, "ProjectsModel");
    qmlRegisterType<ProjectsScrollInput>("Mantis.Studio", 1, 0, "ProjectsScrollInput");
    qInstallMessageHandler(handler);
    qmlRegisterType<mantis::render::PointCloudView>("Mantis.Render", 1, 0, "PointCloudView");
    qmlRegisterType<MeasurementView>("Mantis.Render", 1, 0, "MeasurementView");
    StudioBridge studio;
    auto fake = std::make_shared<calibration_test::Fake>();
    CalibrationController controller(fake);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("studio", &studio);
    engine.rootContext()->setContextProperty("calibration", &controller);
    engine.load(QUrl("qrc:/ui/shell/Main.qml"));
    if (engine.rootObjects().empty()) {
        for (const auto &w : warnings)
            std::cerr << w.toStdString() << '\n';
        return 1;
    }
    auto *root = engine.rootObjects().front();
    auto *workspace = root->findChild<QObject *>("calibrationWorkspace");
    if (!workspace || controller.stages().size() != 7)
        return 2;
    root->setProperty("workspace", "calibration");
    for (int stage = 0; stage < 7; ++stage) {
        controller.setStage(stage);
        QCoreApplication::processEvents();
        if (!hasStage(qobject_cast<QQuickItem *>(workspace), QString("calibrationStage%1").arg(stage)))
            return 3;
    }
    // Exercise the compact desktop width as well as the optional evidence rail.
    controller.setStage(6);
    controller.refresh();
    if (!until([&] { return !controller.artifacts().empty(); }))
        return 5;
    controller.selectDevice("third-party");
    controller.selectArtifact("rig", "G");
    auto *button = root->findChild<QObject *>("activateRig");
    auto *dialog = root->findChild<QObject *>("activateRigConfirmation");
    if (!button || !dialog || !button->property("enabled").toBool() ||
        !QMetaObject::invokeMethod(button, "clicked") ||
        !until([&] { return dialog->property("visible").toBool(); })) {
        std::cerr << "Activation dialog did not open with confirmed exact IDs\n";
        return 6;
    }
    fake->project("/fixture/B/project.mantis", 22);
    controller.observeSnapshot(fake->snapshot());
    if (!until([&] { return !dialog->property("visible").toBool(); })) {
        std::cerr << "Stale activation dialog stayed visible after project switch\n";
        return 7;
    }
    controller.refresh();
    if (!until([&] {
            return !controller.artifacts().empty() &&
                   controller.artifacts().front().toMap()["revision"].toInt() == 22;
        }))
        return 8;
    // Reuse both IDs deliberately: the old confirmation still belongs to A.
    controller.selectDevice("third-party");
    controller.selectArtifact("rig", "G");
    QMetaObject::invokeMethod(dialog, "accepted");
    QCoreApplication::processEvents();
    if (fake->activations != 0 || controller.state()["pending"].toMap()["activation"].toBool()) {
        std::cerr << "Stale same-ID project-A confirmation admitted activation in B\n";
        return 9;
    }
    QMetaObject::invokeMethod(button, "clicked");
    if (!until([&] { return dialog->property("visible").toBool(); }))
        return 10;
    QMetaObject::invokeMethod(dialog, "accept");
    if (!until([&] { return fake->activations == 1 && controller.state()["active"].toMap()["id"] == "G"; })) {
        std::cerr << "Fresh project-B activation confirmation did not complete\n";
        return 11;
    }
    std::cout << "PASS: visible activation confirmation fenced across same-ID project switch\n";
    root->setProperty("width", 1080);
    QCoreApplication::processEvents();
    root->setProperty("width", 1420);
    QCoreApplication::processEvents();
    root->setProperty("workspace", "acquisition");
    QCoreApplication::processEvents();
    for (const auto &warning : warnings)
        std::cerr << warning.toStdString() << '\n';
    if (!warnings.empty())
        return 4;
    std::cout << "Calibration QML loaded and bound all seven stages without binding/type warnings\n";
    return 0;
}
