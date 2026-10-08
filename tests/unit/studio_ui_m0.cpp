#include "bridge.hpp"
#include "calibration_controller.hpp"
#include "home_model.hpp"
#include "screenshot.hpp"
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QJSValue>
#include <QKeyEvent>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlProperty>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>
#include <cmath>
#include <iostream>
#include <stdexcept>

// A presentation snapshot with explicit notifications; no command API or runtime.
class SnapshotStub : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool connected MEMBER connected NOTIFY changed)
    Q_PROPERTY(QVariant devices MEMBER devices NOTIFY changed)
  public:
    bool connected{};
    QVariant devices =
        QVariantList{QVariantMap{{"id", "observed-device"},
                                 {"name", "Observed device"},
                                 {"capabilities", QStringList{"org.mantis.camera.image-stream.v1"}}}};
    void publish(bool value) {
        connected = value;
        emit changed();
    }
  signals:
    void changed();
};
static QStringList warnings;
static void messages(QtMsgType type, const QMessageLogContext &, const QString &message) {
    if ((type == QtWarningMsg || type == QtCriticalMsg) &&
        (message.contains("qml", Qt::CaseInsensitive) || message.contains("binding", Qt::CaseInsensitive) ||
         message.contains("Error") || message.contains("Layout")))
        warnings.push_back(message);
}
static void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
static void settle() {
    QEventLoop loop;
    QTimer::singleShot(80, &loop, &QEventLoop::quit);
    loop.exec();
}
static QQuickItem *findItem(QQuickItem *parent, const QString &name) {
    if (parent->objectName() == name)
        return parent;
    for (auto *child : parent->childItems())
        if (auto *found = findItem(child, name))
            return found;
    return nullptr;
}
static void click(QQuickWindow *window, QQuickItem *item) {
    require(item && item->isVisible() && item->isEnabled(), "Navigation action unreachable");
    const auto point = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
    require(window->contentItem()->contains(point), "Navigation outside window");
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point.toPoint());
    settle();
}
static QVariantList list(QObject *object, const char *property) {
    require(object, "Missing list provider");
    constexpr qsizetype maximumRows = 65536;
    auto value = object->property(property);
    if (value.metaType() == QMetaType::fromType<QJSValue>()) {
        const auto js = value.value<QJSValue>();
        if (!js.isArray())
            throw std::runtime_error(std::string(property) + ": expected JavaScript array");
        if (js.property("length").toNumber() > maximumRows)
            throw std::runtime_error(std::string(property) + ": list exceeds presentation bound");
        value = js.toVariant();
    }
    if (value.metaType() != QMetaType::fromType<QVariantList>())
        throw std::runtime_error(std::string(property) + ": expected QVariantList, got " +
                                 (value.typeName() ? value.typeName() : "invalid"));
    const auto rows = value.toList();
    if (rows.size() > maximumRows)
        throw std::runtime_error(std::string(property) + ": list exceeds presentation bound");
    return rows;
}
static QVariantMap firstMap(QObject *object, const char *property) {
    const auto values = list(object, property);
    if (values.empty() || values.front().metaType() != QMetaType::fromType<QVariantMap>())
        throw std::runtime_error(std::string(property) + ": expected nonempty list of maps");
    return values.front().toMap();
}
static QQuickItem *checkedItem(QQuickItem *parent, const QString &name) {
    auto *item = findItem(parent, name);
    if (!item)
        throw std::runtime_error("Missing UI item: " + name.toStdString());
    return item;
}
static void conversionRegression(QQmlEngine &engine) {
    QObject fixture;
    fixture.setProperty("rows", QVariantList{QVariantMap{{"id", "valid"}}});
    require(firstMap(&fixture, "rows")["id"] == "valid", "Native list conversion failed");
    fixture.setProperty("rows", QVariant::fromValue(engine.evaluate("[{id: 'js'}]")));
    require(firstMap(&fixture, "rows")["id"] == "js", "JS list conversion failed");
    for (const auto &invalid : {QVariant{}, QVariant("malformed"), QVariant(QVariantList{}),
                                QVariant(QVariantList{42}), QVariant::fromValue(engine.evaluate("({})")),
                                QVariant::fromValue(engine.evaluate("new Array(65537)"))}) {
        fixture.setProperty("rows", invalid);
        bool diagnosed = false;
        try {
            (void)firstMap(&fixture, "rows");
        } catch (const std::runtime_error &error) {
            diagnosed = std::string(error.what()).find("rows:") != std::string::npos;
        }
        require(diagnosed, "Malformed/empty list was not diagnosed");
    }
}
class PresentationBridge : public StudioBridge {
  public:
    PresentationBridge() : StudioBridge(nullptr, false) {}
    using StudioBridge::applyResult;
};
static void stateRegression(QQmlApplicationEngine &engine, QQuickWindow *window, StudioBridge &studio,
                            const QString &output) {
    PresentationBridge observed;
    CalibrationController eligibility;
    QObject::connect(&observed, &StudioBridge::snapshotReady, &eligibility,
                     &CalibrationController::observeSnapshot);
    QQmlComponent provider(&engine, QUrl("qrc:/ui/state/AppUiState.qml"));
    std::unique_ptr<QObject> model(
        provider.createWithInitialProperties({{"bridge", QVariant::fromValue(&observed)},
                                              {"calibrationProvider", QVariant::fromValue(&eligibility)}}));
    require(model != nullptr, "Eligibility provider failed");
    StudioResult current;
    current.snapshot.emplace();
    current.snapshot->set_project_path("retained-project");
    auto addDevice = [&](const char *id, const char *cap, const char *parent = "") {
        auto *device = current.snapshot->add_devices();
        device->set_id(id);
        device->set_name(id);
        device->set_parent(parent);
        device->add_capabilities(cap);
    };
    addDevice("eligible", "org.mantis.camera.frameset-stream.v1");
    addDevice("image-child", "org.mantis.camera.image-stream.v1", "eligible");
    addDevice("no-child", "org.mantis.camera.frameset-stream.v1");
    addDevice("emitter", "org.mantis.emitter.power-control.v1", "no-child");
    addDevice("single-image", "org.mantis.camera.image-stream.v1");
    auto *capture = current.snapshot->add_captures();
    capture->set_id("daemon-capture");
    capture->set_active(true);
    (*capture->mutable_diagnostics())["left.identity"] = "test-camera";
    auto *artifact = current.snapshot->add_artifacts();
    artifact->set_id("retained-capture");
    artifact->set_type("org.mantis.RawCapture");
    artifact->set_state("FINALIZED");
    artifact->set_schema_version(2);
    observed.applyResult(current);
    require(eligibility.devices().size() == 1, "Controller accepted an ineligible graph");
    for (const auto &mode : QStringList{"live", "hybrid", "mock"}) {
        model->setProperty("mode", mode);
        const auto devices = list(model.get(), "devices");
        require(devices.size() == (mode == "mock" ? 1 : 3), "Provider device count incorrect");
        for (const auto &value : devices) {
            require(value.metaType() == QMetaType::fromType<QVariantMap>(), "Malformed device row");
            const auto device = value.toMap();
            const bool permitted = mode != "mock" && device.value("id") == "eligible";
            require(device.value("actions").toMap().value("calibration").toBool() == permitted,
                    "Calibration permission diverged from controller");
            require(device.value("actionable").toBool() == permitted, "Wrong action permission");
            require(device.value("readiness") == "unknown", "Readiness was synthesized");
            if (mode != "mock") {
                require(device.value("discovered").toBool(), "Discovery missing");
                require(device.value("availability") == "unknown", "Availability was synthesized");
                require(device.value("connected").isNull(), "Discovery implies connection");
            }
        }
        require(list(model.get(), "demoDevices").size() == (mode == "hybrid" ? 1 : 0),
                "Fixture entered live device model");
    }
    model->setProperty("mode", "live");
    QQmlComponent foundation(&engine, QUrl("qrc:/ui/workspaces/FoundationWorkspace.qml"));
    std::unique_ptr<QObject> panel(
        foundation.createWithInitialProperties({{"state", QVariant::fromValue(model.get())},
                                                {"page", QVariantMap{{"route", "devices"},
                                                                     {"milestone", "UI-M2"},
                                                                     {"planned", "Device details"},
                                                                     {"detail", "Test eligibility"}}}}));
    auto *panelItem = qobject_cast<QQuickItem *>(panel.get());
    require(panelItem, "Eligibility panel did not load");
    panelItem->setParentItem(window->contentItem());
    panelItem->setSize(QSizeF(1000, 680));
    settle();
    auto *eligibleButton = checkedItem(panelItem, "calibrate_eligible");
    require(eligibleButton->isEnabled(), "Eligible composite action disabled");
    require(!checkedItem(panelItem, "calibrate_no-child")->isEnabled(), "Childless composite actionable");
    require(!checkedItem(panelItem, "calibrate_single-image")->isEnabled(), "Single image actionable");
    QSignalSpy intent(panel.get(), SIGNAL(openCalibration(QString)));
    // Invoke the actual control signal: the lower card may be outside the ScrollView viewport.
    require(QMetaObject::invokeMethod(eligibleButton, "clicked"), "Calibration button has no intent");
    require(intent.size() == 1 && intent.at(0).at(0) == "eligible", "Validated identity lost");
    model->setProperty("mode", "mock");
    settle();
    require(!checkedItem(panelItem, "calibrate_demo-device")->isVisible(), "Demo exposed runtime intent");
    model->setProperty("mode", "live");
    panel.reset();
    window->setProperty("studioBridge", QVariant::fromValue(&observed));
    window->setProperty("uiMode", "live");
    window->setProperty("workspace", "acquisition");
    window->resize(1920, 1080);
    settle();
    require(observed.capturing() && observed.lastKnownCapturing(), "Active snapshot not presented");
    require(checkedItem(window->contentItem(), "acquisitionCaptureStatus")->property("text") ==
                "Streaming · raw recording",
            "Confirmed streaming label missing");
    auto rejection = current;
    rejection.operationAttempted = true;
    rejection.issues.push_back({"operation", "Rejected operation",
                                mantis::Error{mantis::Status::busy, "Rejected operation", "pipeline"}});
    observed.applyResult(rejection);
    require(observed.connected() && observed.capturing(), "Rejection falsely disconnected runtime");
    require(checkedItem(window->contentItem(), "runtimeMessage")
                ->property("text")
                .toString()
                .startsWith("Operation failed"),
            "Operation rejection is visibly presented as runtime loss");
    rejection.issues.clear();
    observed.applyResult(rejection);
    require(observed.errorDetails().empty(), "Successful operation retained failure");
    const auto retainedText = observed.acquisitionText();
    StudioResult failure;
    failure.issues.push_back(
        {"snapshot", "deterministic snapshot transport failure",
         mantis::Error{mantis::Status::io, "deterministic snapshot transport failure", "platform"}});
    observed.applyResult(failure);
    require(!observed.connected() && !observed.capturing() && observed.lastKnownCapturing(),
            "Disconnect presented cached capture as current");
    require(checkedItem(window->contentItem(), "runtimeMessage")
                ->property("text")
                .toString()
                .startsWith("Runtime state unconfirmed"),
            "Unavailable snapshot presented confirmed runtime access");
    require(observed.devices().size() == 3 && observed.artifacts().size() == 1 &&
                observed.project() == "retained-project" && observed.acquisitionText() == retainedText,
            "Disconnect erased last-known authoritative data");
    require(list(model.get(), "devices").empty(), "Disconnected provider permits live actions");
    require(checkedItem(window->contentItem(), "acquisitionCaptureStatus")
                ->property("text")
                .toString()
                .contains("current state unknown"),
            "Stale capture label misleading");
    require(checkedItem(window->contentItem(), "acquisitionFreshness")->isVisible(), "Stale banner absent");
    require(!checkedItem(window->contentItem(), "startCapture_eligible")->isEnabled(),
            "Offline action enabled");
    observed.stopCapture();
    observed.startCapture("demo-device");
    observed.runPipeline("example");
    require(!observed.busy(), "Disconnected action started client work");
    settle();
    require(window->grabWindow().save(output + "/disconnected-last-known-acquisition.png"),
            "Stale capture image failed");
    observed.applyResult(current);
    window->resize(1080, 720);
    settle();
    require(observed.capturing() && list(model.get(), "devices").size() == 3 && observed.error().isEmpty(),
            "Reconnect did not refresh confirmed state");
    current.snapshot->clear_captures();
    observed.applyResult(current);
    require(!observed.capturing() && !observed.lastKnownCapturing() && observed.acquisitionText().isEmpty(),
            "Fresh idle snapshot retained active capture/diagnostics");
    require(!checkedItem(window->contentItem(), "acquisitionFreshness")->isVisible(),
            "Reconnect retains stale banner");
    require(checkedItem(window->contentItem(), "startCapture_eligible")->isEnabled(),
            "Reconnect action unavailable");
    {
        mantis::render::PointCloudView transient;
        observed.attachView(&transient);
    }
    require(!observed.viewportReady(), "Destroyed render attachment retained");
    current.cloud = std::make_shared<const mantis::data::Packet>();
    current.cloud_id = "retained-cloud";
    observed.applyResult(current);
    require(observed.selectedArtifact() == "retained-cloud", "Detached cloud result lost identity");
    window->setProperty("studioBridge", QVariant::fromValue(&studio));
    window->setProperty("uiMode", "mock");
    window->setProperty("workspace", "home");
    SnapshotStub malformed;
    model->setProperty("bridge", QVariant::fromValue(&malformed));
    model->setProperty("calibrationProvider", QVariant{});
    model->setProperty("mode", "live");
    for (const auto &bad : {QVariant{}, QVariant(42), QVariant(QVariantMap{}),
                            QVariant(QVariantList{QVariant{}, 42, QVariantMap{{"id", 5}},
                                                  QVariantMap{{"name", "missing id"}}})}) {
        malformed.devices = bad;
        malformed.publish(true);
        require(list(model.get(), "devices").empty(), "Malformed provider data acquired authority");
    }
    malformed.devices = QVariantList{QVariantMap{{"id", "valid"}, {"name", "valid"}, {"capabilities", 42}}};
    malformed.publish(true);
    require(!firstMap(model.get(), "devices")["actionable"].toBool(),
            "Malformed capabilities grant permission");
    model->setProperty("bridge", QVariant{});
    require(!model->property("runtimeConnected").toBool() && list(model.get(), "devices").empty(),
            "Missing bridge acquired authority");
}
static void buttonRegression(QQmlApplicationEngine &engine, QQuickWindow *window, const QString &output) {
    QQmlComponent component(&engine, QUrl("qrc:/ui/components/StudioButton.qml"));
    std::unique_ptr<QObject> object(component.createWithInitialProperties({{"text", "Interaction test"}}));
    auto *button = qobject_cast<QQuickItem *>(object.get());
    require(button, "Shared button missing");
    button->setParentItem(window->contentItem());
    button->setPosition(QPointF(500, 100));
    button->setWidth(200);
    auto *background = button->property("background").value<QObject *>();
    auto *content = button->property("contentItem").value<QObject *>();
    require(background && content, "Button visuals missing");
    auto contrast = [&] {
        auto luminance = [](const QColor &color) {
            auto linear = [](double v) {
                return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
            };
            return 0.2126 * linear(color.redF()) + 0.7152 * linear(color.greenF()) +
                   0.0722 * linear(color.blueF());
        };
        const auto fg = luminance(content->property("color").value<QColor>());
        const auto bg = luminance(background->property("color").value<QColor>());
        return (std::max(fg, bg) + 0.05) / (std::min(fg, bg) + 0.05);
    };
    for (bool primary : {false, true}) {
        const auto prefix = output + (primary ? "/button-primary-" : "/button-secondary-");
        auto captureState = [&](const QString &state) {
            require(window->grabWindow().save(prefix + state + ".png"), "Button state image failed");
        };
        button->setProperty("primary", primary);
        button->setEnabled(true);
        QTest::mouseMove(window, QPoint(10, 10));
        settle();
        const auto idle = background->property("color");
        const auto idleText = content->property("color");
        require(contrast() >= 4.5, "Default button contrast insufficient");
        captureState("default");
        QTest::mouseMove(window, QPoint(600, 119));
        settle();
        require(button->property("hovered").toBool() && background->property("color") != idle,
                "Hover feedback absent");
        require(contrast() >= 4.5, "Hover contrast insufficient");
        captureState("hover");
        const auto hover = background->property("color");
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, QPoint(600, 119));
        require(button->property("down").toBool() && background->property("color") != hover,
                "Pressed feedback absent");
        require(contrast() >= 4.5, "Pressed contrast insufficient");
        require(window->grabWindow().save(
                    output + (primary ? "/button-primary-pressed.png" : "/button-secondary-pressed.png")),
                "Button image failed");
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, QPoint(600, 119));
        checkedItem(window->contentItem(), "nav_home")->forceActiveFocus(Qt::TabFocusReason);
        button->forceActiveFocus(Qt::TabFocusReason);
        require(button->property("visualFocus").toBool() &&
                    QQmlProperty(background, "border.width").read().toInt() == 2,
                "Keyboard focus feedback absent");
        captureState("focus");
        QSignalSpy clicks(button, SIGNAL(clicked()));
        QTest::keyClick(window, Qt::Key_Space);
        require(clicks.size() == 1, "Keyboard activation failed");
        button->setEnabled(false);
        const auto disabled = background->property("color");
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, QPoint(600, 119));
        require(clicks.size() == 1 && background->property("color") == disabled &&
                    content->property("color") != idleText && (!primary || disabled != idle),
                "Disabled button interacted or retained active fill");
        captureState("disabled");
    }
}

static void screenshotRegression(QQuickWindow *window, const QString &output) {
    auto exercise = [&](QQuickWindow *target, const QString &path, int timeout, bool expectSuccess,
                        const QString &diagnostic) {
        QEventLoop loop;
        bool called = false, succeeded = false;
        QString error;
        ScreenshotRequest request(target, path, 0, timeout, [&](bool ok, const QString &message) {
            called = true;
            succeeded = ok;
            error = message;
            loop.quit();
        });
        QTimer::singleShot(3000, &loop, &QEventLoop::quit);
        loop.exec();
        require(called && succeeded == expectSuccess, "Screenshot completion/deadline behavior incorrect");
        require(request.succeeded() == expectSuccess, "Screenshot completion state inconsistent");
        if (!expectSuccess)
            require(error.contains(diagnostic), "Screenshot failure lacks actionable diagnostic");
        else {
            const QImage image(path);
            require(!image.isNull() && image.width() == window->width() && image.height() == window->height(),
                    "Rendered screenshot invalid");
            require(image.pixelColor(10, 10) != image.pixelColor(image.width() / 2, image.height() / 2),
                    "Screenshot contains only a clear color");
        }
    };
    exercise(window, output + "/frame-ready-home.png", 2000, true, {});
    exercise(window, output + "/missing-directory/failed.png", 2000, false, "save failed");
    QQuickWindow hidden;
    hidden.resize(1080, 720);
    hidden.create();
    exercise(&hidden, output + "/must-not-exist.png", 40, false, "deadline");
    require(!QFile::exists(output + "/must-not-exist.png"), "Unrendered window produced screenshot");
    exercise(nullptr, output + "/must-not-exist.png", 40, false, "unavailable");
}

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    QQuickStyle::setStyle("Basic");
    qmlRegisterType<HomeModel>("Mantis.Studio", 1, 0, "HomeModel");
    qInstallMessageHandler(messages);
    qmlRegisterType<mantis::render::PointCloudView>("Mantis.Render", 1, 0, "PointCloudView");
    qmlRegisterType<MeasurementView>("Mantis.Render", 1, 0, "MeasurementView");
    try {
        const QString output =
            argc > 1 ? QString::fromLocal8Bit(argv[1]) : QDir::tempPath() + "/mantis-ui-m0";
        require(QDir().mkpath(output), "Cannot create screenshot directory");
        StudioBridge studio(nullptr, false);
        CalibrationController calibration;
        QQmlApplicationEngine engine;
        conversionRegression(engine);
        engine.rootContext()->setContextProperty("studio", &studio);
        engine.rootContext()->setContextProperty("calibration", &calibration);
        engine.setInitialProperties({{"uiMode", "mock"}, {"workspace", "home"}});
        engine.load(QUrl("qrc:/ui/shell/Main.qml"));
        require(!engine.rootObjects().empty(), "Shell did not load");
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().front());
        require(window, "Missing window");
        settle();
        require(checkedItem(window->contentItem(), "modeBadge")->property("text") == "Demo / Mock",
                "Mock mode is not visibly labelled");
        require(checkedItem(window->contentItem(), "runtimeStatusLabel")->property("text") ==
                    "Mock · runtime not used",
                "Mock runtime label is misleading");
        auto *state = window->property("appUiState").value<QObject *>();
        require(state && !state->property("runtimeConnected").toBool(), "Mock claimed runtime connection");
        require(firstMap(state, "devices")["source"] == "mock", "Missing mock source metadata");
        require(!firstMap(state, "devices")["actionable"].toBool(), "Fixture is actionable");
        const QStringList routes{"home",     "scan",     "process", "inspect", "reverse",
                                 "automate", "projects", "devices", "plugins", "settings"};
        for (const auto &route : routes) {
            auto *nav = checkedItem(window->contentItem(), "nav_" + route);
            click(window, nav);
            require(window->property("workspace") == route, "Click did not change workspace");
            require(nav->property("selected").toBool(), "Missing active selection");
            require(checkedItem(window->contentItem(), "workspaceTitle")->property("text") ==
                        QString(route).replace(0, 1, route.left(1).toUpper()),
                    "Wrong workspace title");
            require(window->grabWindow().save(output + "/mock-" + route + ".png"), "Screenshot failed");
        }
        auto *home = checkedItem(window->contentItem(), "nav_home");
        home->forceActiveFocus();
        QKeyEvent down(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &down);
        require(window->activeFocusItem() == checkedItem(window->contentItem(), "nav_scan"),
                "Arrow navigation failed");
        QKeyEvent press(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
        QKeyEvent release(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &press);
        QCoreApplication::sendEvent(window, &release);
        settle();
        require(window->property("workspace") == "scan", "Keyboard activation failed");
        click(window, checkedItem(window->contentItem(), "nav_devices"));
        click(window, checkedItem(window->contentItem(), "openCalibration"));
        require(window->property("workspace") == "calibration", "Devices calibration route broken");
        require(!calibration.visible(), "Mock started calibration polling");
        require(!checkedItem(window->contentItem(), "calibrationWorkspace")->isEnabled(),
                "Mock enabled calibration commands");
        window->setProperty("workspace", "acquisition");
        settle();
        require(!checkedItem(window->contentItem(), "acquisitionWorkspace")->isEnabled(),
                "Mock enabled acquisition commands");
        require(checkedItem(window->contentItem(), "pointCloudView"), "Legacy render hook missing");
        for (const auto &mode : QStringList{"live", "hybrid"}) {
            window->setProperty("uiMode", mode);
            window->setProperty("workspace", "home");
            settle();
            require(checkedItem(window->contentItem(), "modeBadge")->property("text") ==
                        (mode == "hybrid" ? "Hybrid" : "Live source"),
                    "Mode badge mismatch");
            require(checkedItem(window->contentItem(), "runtimeStatusLabel")->property("text") ==
                        "Runtime state unconfirmed",
                    "Disconnected runtime label missing");
            require(!state->property("runtimeConnected").toBool(), "Offline mode invented connection");
            require(list(state, "devices").empty(), "Offline live devices were synthesized");
            require(list(state, "demoDevices").size() == (mode == "hybrid" ? 1 : 0),
                    "Hybrid sample separation failed");
            require(window->grabWindow().save(output + "/" + mode + "-home.png"), "Source screenshot failed");
        }
        window->setProperty("uiMode", "mock");
        for (const QSize size : {QSize(1080, 720), QSize(1536, 1024), QSize(1920, 1080)}) {
            window->resize(size);
            for (const auto &route : QStringList{"home", "acquisition", "calibration"}) {
                window->setProperty("workspace", route);
                settle();
                for (const auto &navRoute : routes) {
                    auto *item = checkedItem(window->contentItem(), "nav_" + navRoute);
                    const auto position = item->mapToScene(QPointF(item->width(), item->height()));
                    require(position.x() <= size.width() && position.y() <= size.height(),
                            "Navigation clipped on resize");
                }
                const auto picture = window->grabWindow();
                require(picture.size() == size, "Screenshot dimensions differ");
                require(
                    picture.save(output + "/mock-" + route + "-" + QString::number(size.width()) + ".png"),
                    "Resize screenshot failed");
            }
        }
        std::cout << "STAGE: dynamic device removal followed by immediate resize" << std::endl;
        window->setProperty("workspace", "home");
        for (const auto &stressRoute : QStringList{"home", "devices"}) {
            window->setProperty("workspace", stressRoute);
            for (int iteration = 0; iteration < 8; ++iteration) {
                // No settling between model mutation and resize: this reproduced the Qt 6.4 crash.
                window->setProperty("uiMode", iteration % 2 ? "mock" : "hybrid");
                window->resize(iteration % 2 ? QSize(1080, 720) : QSize(1920, 1080));
                settle();
                require(list(state, "devices").size() == (iteration % 2 ? 1 : 0),
                        "Device removal/repopulation failed during resize");
                require(!window->grabWindow().isNull(), "Transition failed to render");
            }
        }
        std::cout << "STAGE: presentation provider" << std::endl;
        SnapshotStub snapshot;
        QQmlComponent provider(&engine, QUrl("qrc:/ui/state/AppUiState.qml"));
        std::unique_ptr<QObject> model(
            provider.createWithInitialProperties({{"bridge", QVariant::fromValue(&snapshot)}}));
        require(model != nullptr, "Provider failed to load");
        for (const auto &mode : QStringList{"live", "hybrid", "mock"}) {
            model->setProperty("mode", mode);
            snapshot.publish(true);
            settle();
            auto device = firstMap(model.get(), "devices");
            require(device["source"] == (mode == "mock" ? "mock" : "live"),
                    "Connected provider source incorrect");
            require(!device["actionable"].toBool(), "Discovery alone granted action permission");
            if (mode != "mock")
                require(device["capabilities"].toStringList().contains("org.mantis.camera.image-stream.v1"),
                        "Capabilities lost");
            snapshot.publish(false);
            settle();
            require(!model->property("runtimeConnected").toBool(), "Stale connectedness after disconnect");
            require(list(model.get(), "devices").size() == (mode == "mock" ? 1 : 0),
                    "Stale devices after disconnect");
        }
        std::cout << "STAGE: controller eligibility and stale snapshot transitions" << std::endl;
        stateRegression(engine, window, studio, output);
        std::cout << "STAGE: button interactions" << std::endl;
        buttonRegression(engine, window, output);
        std::cout << "STAGE: rendered-frame screenshot deadlines" << std::endl;
        screenshotRegression(window, output);
        for (const auto &warning : warnings)
            std::cerr << warning.toStdString() << '\n';
        require(warnings.empty(), "QML warning in supported paths");
        std::cout << "PASS: ten routes, mouse/keyboard navigation, source/capability transitions, mock "
                     "authority boundary, legacy hooks and three viewport sizes\n";
    } catch (const std::exception &issue) {
        std::cerr << issue.what() << '\n';
        for (const auto &warning : warnings)
            std::cerr << warning.toStdString() << '\n';
        return 1;
    }
    return 0;
}
#include "studio_ui_m0.moc"
