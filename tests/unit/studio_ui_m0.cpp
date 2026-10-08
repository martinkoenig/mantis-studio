#include "bridge.hpp"
#include "calibration_controller.hpp"
#include <QDir>
#include <QEventLoop>
#include <QGuiApplication>
#include <QJSValue>
#include <QKeyEvent>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTest>
#include <QTimer>
#include <iostream>
#include <stdexcept>

// A presentation snapshot with explicit notifications; no command API or runtime.
class SnapshotStub : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool connected MEMBER connected NOTIFY changed)
    Q_PROPERTY(QVariantList devices MEMBER devices NOTIFY changed)
  public:
    bool connected{};
    QVariantList devices{QVariantMap{{"id", "observed-device"},
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
    auto value = object->property(property);
    if (value.metaType() == QMetaType::fromType<QJSValue>()) {
        const auto js = value.value<QJSValue>();
        if (!js.isArray())
            throw std::runtime_error(std::string(property) + ": expected JavaScript array");
        value = js.toVariant();
    }
    if (value.metaType() != QMetaType::fromType<QVariantList>())
        throw std::runtime_error(std::string(property) + ": expected QVariantList, got " +
                                 (value.typeName() ? value.typeName() : "invalid"));
    return value.toList();
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
                                QVariant(QVariantList{42}), QVariant::fromValue(engine.evaluate("({})"))}) {
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
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    QQuickStyle::setStyle("Basic");
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
                        "Runtime disconnected",
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
        for (int iteration = 0; iteration < 8; ++iteration) {
            // No settling between model mutation and resize: this reproduced the Qt 6.4 crash.
            window->setProperty("uiMode", iteration % 2 ? "mock" : "hybrid");
            window->resize(iteration % 2 ? QSize(1080, 720) : QSize(1920, 1080));
            settle();
            require(list(state, "devices").size() == (iteration % 2 ? 1 : 0),
                    "Device removal/repopulation failed during resize");
            require(!window->grabWindow().isNull(), "Transition failed to render");
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
            require(device["actionable"].toBool() == (mode != "mock"), "Mock acquired live authority");
            if (mode != "mock")
                require(device["capabilities"].toStringList().contains("org.mantis.camera.image-stream.v1"),
                        "Capabilities lost");
            snapshot.publish(false);
            settle();
            require(!model->property("runtimeConnected").toBool(), "Stale connectedness after disconnect");
            require(list(model.get(), "devices").size() == (mode == "mock" ? 1 : 0),
                    "Stale devices after disconnect");
        }
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
