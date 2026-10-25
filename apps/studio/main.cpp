#include "bridge.hpp"
#include "calibration_controller.hpp"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <iostream>
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    app.setApplicationName("Mantis Studio");
    QQuickStyle::setStyle("Basic");
    qmlRegisterType<mantis::render::PointCloudView>("Mantis.Render", 1, 0, "PointCloudView");
    qmlRegisterType<MeasurementView>("Mantis.Render", 1, 0, "MeasurementView");
    StudioBridge bridge;
    CalibrationController calibration;
    QObject::connect(&bridge, &StudioBridge::snapshotReady, &calibration, &CalibrationController::observeSnapshot);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("studio", &bridge);
    engine.rootContext()->setContextProperty("calibration", &calibration);
    engine.load(QUrl("qrc:/ui/shell/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;
    auto args = app.arguments();
    auto value = [&](const QString &option) {
        auto i = args.indexOf(option);
        return i >= 0 && i + 1 < args.size() ? args[i + 1] : QString();
    };
    auto screenshot = value("--screenshot");
    auto quit = value("--quit-after");
    if (!screenshot.isEmpty())
        QTimer::singleShot(3500, &app, [&] {
            auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
            if (!window || !window->grabWindow().save(screenshot))
                std::cerr << "Screenshot failed\n";
        });
    if (!quit.isEmpty())
        QTimer::singleShot(quit.toInt(), &app, &QCoreApplication::quit);
    // Acceptance harness drives the same asynchronous client commands as the QML controls.
    auto demo = value("--acceptance-export");
    QTimer automation;
    int stage = 0;
    QString previousArtifact;
    if (!demo.isEmpty()) {
        automation.setInterval(100);
        QObject::connect(&automation, &QTimer::timeout, &app, [&] {
            if (!bridge.connected() || bridge.busy())
                return;
            if (stage == 0 && !bridge.devices().empty()) {
                bridge.startCapture(bridge.devices().front().toMap()["id"].toString());
                stage = 1;
            } else if (stage == 1 && bridge.capturing()) {
                previousArtifact = bridge.selectedArtifact();
                bridge.runPipeline("example");
                stage = 2;
            } else if (stage == 2 && !bridge.selectedArtifact().isEmpty() &&
                       bridge.selectedArtifact() != previousArtifact) {
                bridge.exportArtifact(demo);
                stage = 3;
            } else if (stage == 3) {
                bool done = false;
                for (auto &v : bridge.jobs()) {
                    auto j = v.toMap();
                    if (j["name"] == "Export PLY" && j["state"] == "Completed")
                        done = true;
                }
                if (done && bridge.viewportReady()) {
                    std::cout << "STUDIO_ACCEPTANCE_READY " << bridge.selectedArtifact().toStdString()
                              << std::endl;
                    stage = 4;
                }
            }
        });
        automation.start();
    }
    return app.exec();
}
