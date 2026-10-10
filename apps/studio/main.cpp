#include "bridge.hpp"
#include "calibration_controller.hpp"
#include "devices_model.hpp"
#include "home_model.hpp"
#include "projects_model.hpp"
#include "projects_scroll.hpp"
#include "scan_model.hpp"
#include "screenshot.hpp"
#include <QCommandLineParser>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QTimer>
#include <iostream>
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    app.setApplicationName("Mantis Studio");
    QQuickStyle::setStyle("Basic");
    qmlRegisterType<ScanModel>("Mantis.Studio", 1, 0, "ScanModel");
    qmlRegisterType<DevicesModel>("Mantis.Studio", 1, 0, "DevicesModel");
    qmlRegisterType<HomeModel>("Mantis.Studio", 1, 0, "HomeModel");
    qmlRegisterType<ProjectsModel>("Mantis.Studio", 1, 0, "ProjectsModel");
    qmlRegisterType<ProjectsScrollInput>("Mantis.Studio", 1, 0, "ProjectsScrollInput");
    QCommandLineParser parser;
    parser.setApplicationDescription("Mantis Studio desktop client · UI-M3b");
    parser.addHelpOption();
    parser.addOptions(
        {{"ui-mode", "UI data source: live, hybrid or mock (default: live).", "mode", "live"},
         {"workspace",
          "Initial workspace: home, scan, process, inspect, reverse, automate, projects, devices, plugins, "
          "settings, calibration or acquisition.",
          "route"},
         {"window-size", "Initial viewport WIDTHxHEIGHT (minimum 1080x720).", "size", "1536x1024"},
         {"screenshot", "Save the rendered window as a PNG.", "path"},
         {"quit-after", "Exit after positive milliseconds (minimum 100 with --screenshot).", "milliseconds"},
         {"acceptance-export", "Run the real asynchronous capture/pipeline/PLY acceptance workflow.",
          "path"}});
    auto invalid = [&](const QString &message) {
        std::cerr << message.toStdString() << "\nUse --help for usage.\n";
        return 2;
    };
    if (!parser.parse(app.arguments()))
        return invalid(parser.errorText());
    if (parser.isSet("help")) {
        std::cout << parser.helpText().toStdString();
        return 0;
    }
    if (!parser.positionalArguments().empty())
        return invalid("Unexpected positional argument");
    const auto mode = parser.value("ui-mode");
    if (!QStringList{"live", "hybrid", "mock"}.contains(mode))
        return invalid("Invalid --ui-mode");
    auto workspace = parser.value("workspace");
    if (parser.isSet("workspace") &&
        !QStringList{"home", "scan", "process", "inspect", "reverse", "automate", "projects", "devices",
                     "plugins", "settings", "calibration", "acquisition"}
             .contains(workspace))
        return invalid("Invalid --workspace");
    if (workspace.isEmpty())
        workspace = mode == "live" ? "acquisition" : "home";
    const auto demo = parser.value("acceptance-export");
    if (parser.isSet("acceptance-export")) {
        if (demo.isEmpty() || mode == "mock")
            return invalid("--acceptance-export requires a path and live or hybrid mode");
        workspace = "acquisition"; // Acceptance requires an initialized real point-cloud viewport.
    }
    const auto dimensions = QRegularExpression("^(\\d+)x(\\d+)$").match(parser.value("window-size"));
    const int width = dimensions.captured(1).toInt(), height = dimensions.captured(2).toInt();
    if (!dimensions.hasMatch() || width < 1080 || height < 720 || width > 7680 || height > 4320)
        return invalid("Invalid --window-size (supported range: 1080x720 to 7680x4320)");
    int quitMs = 0;
    if (parser.isSet("quit-after")) {
        bool ok{};
        quitMs = parser.value("quit-after").toInt(&ok);
        if (!ok || quitMs <= 0)
            return invalid("--quit-after requires positive milliseconds");
    }
    const auto screenshot = parser.value("screenshot");
    if (parser.isSet("screenshot") && screenshot.isEmpty())
        return invalid("--screenshot requires a path");
    if (!screenshot.isEmpty() && quitMs > 0 && quitMs < 100)
        return invalid("--screenshot with --quit-after requires at least 100 milliseconds");
    qmlRegisterType<mantis::render::PointCloudView>("Mantis.Render", 1, 0, "PointCloudView");
    qmlRegisterType<MeasurementView>("Mantis.Render", 1, 0, "MeasurementView");
    StudioBridge bridge(nullptr, mode != "mock");
    auto calibrationClient =
        mode == "mock"
            ? std::make_shared<PublicCalibrationClient>(mantis::client::Client{mantis::client::Endpoint{}})
            : std::make_shared<PublicCalibrationClient>();
    CalibrationController calibration(calibrationClient);
    QObject::connect(&bridge, &StudioBridge::snapshotReady, &calibration,
                     &CalibrationController::observeSnapshot);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("studio", &bridge);
    engine.rootContext()->setContextProperty("calibration", &calibration);
    engine.setInitialProperties(
        {{"uiMode", mode}, {"workspace", workspace}, {"width", width}, {"height", height}});
    engine.load(QUrl("qrc:/ui/shell/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;
    std::unique_ptr<ScreenshotRequest> screenshotRequest;
    if (!screenshot.isEmpty()) {
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        screenshotRequest = std::make_unique<ScreenshotRequest>(
            window, screenshot, quitMs > 0 && quitMs <= 3500 ? 0 : 3500, quitMs > 0 ? quitMs : 10000,
            [&](bool success, const QString &error) {
                if (!success) {
                    std::cerr << error.toStdString() << std::endl;
                    app.exit(1);
                }
            });
    }
    if (quitMs > 0)
        QTimer::singleShot(quitMs, &app, [&] {
            if (screenshotRequest && !screenshotRequest->succeeded()) {
                std::cerr << "Screenshot incomplete at --quit-after deadline" << std::endl;
                app.exit(1);
            } else {
                app.quit();
            }
        });
    // Acceptance harness drives the same asynchronous client commands as the QML controls.
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
