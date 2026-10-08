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
    return object->property(property).value<QJSValue>().toVariant().toList();
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
        engine.rootContext()->setContextProperty("studio", &studio);
        engine.rootContext()->setContextProperty("calibration", &calibration);
        engine.setInitialProperties({{"uiMode", "mock"}, {"workspace", "home"}});
        engine.load(QUrl("qrc:/ui/shell/Main.qml"));
        require(!engine.rootObjects().empty(), "Shell did not load");
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().front());
        require(window, "Missing window");
        settle();
        require(findItem(window->contentItem(), "modeBadge")->property("text") == "Demo / Mock",
                "Mock mode is not visibly labelled");
        require(findItem(window->contentItem(), "runtimeStatusLabel")->property("text") ==
                    "Mock · runtime not used",
                "Mock runtime label is misleading");
        auto *state = window->property("appUiState").value<QObject *>();
        require(state && !state->property("runtimeConnected").toBool(), "Mock claimed runtime connection");
        require(list(state, "devices").front().toMap()["source"] == "mock", "Missing mock source metadata");
        require(!list(state, "devices").front().toMap()["actionable"].toBool(), "Fixture is actionable");
        const QStringList routes{"home",     "scan",     "process", "inspect", "reverse",
                                 "automate", "projects", "devices", "plugins", "settings"};
        for (const auto &route : routes) {
            auto *nav = findItem(window->contentItem(), "nav_" + route);
            click(window, nav);
            require(window->property("workspace") == route, "Click did not change workspace");
            require(nav->property("selected").toBool(), "Missing active selection");
            require(findItem(window->contentItem(), "workspaceTitle")->property("text") ==
                        QString(route).replace(0, 1, route.left(1).toUpper()),
                    "Wrong workspace title");
            require(window->grabWindow().save(output + "/mock-" + route + ".png"), "Screenshot failed");
        }
        auto *home = findItem(window->contentItem(), "nav_home");
        home->forceActiveFocus();
        QKeyEvent down(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &down);
        require(window->activeFocusItem() == findItem(window->contentItem(), "nav_scan"),
                "Arrow navigation failed");
        QKeyEvent press(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
        QKeyEvent release(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &press);
        QCoreApplication::sendEvent(window, &release);
        settle();
        require(window->property("workspace") == "scan", "Keyboard activation failed");
        click(window, findItem(window->contentItem(), "nav_devices"));
        click(window, findItem(window->contentItem(), "openCalibration"));
        require(window->property("workspace") == "calibration", "Devices calibration route broken");
        require(!calibration.visible(), "Mock started calibration polling");
        require(!findItem(window->contentItem(), "calibrationWorkspace")->isEnabled(),
                "Mock enabled calibration commands");
        window->setProperty("workspace", "acquisition");
        settle();
        require(!findItem(window->contentItem(), "acquisitionWorkspace")->isEnabled(),
                "Mock enabled acquisition commands");
        require(findItem(window->contentItem(), "pointCloudView"), "Legacy render hook missing");
        for (const auto &mode : QStringList{"live", "hybrid"}) {
            window->setProperty("uiMode", mode);
            window->setProperty("workspace", "home");
            settle();
            require(findItem(window->contentItem(), "modeBadge")->property("text") ==
                        (mode == "hybrid" ? "Hybrid" : "Live source"),
                    "Mode badge mismatch");
            require(findItem(window->contentItem(), "runtimeStatusLabel")->property("text") ==
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
                    auto *item = findItem(window->contentItem(), "nav_" + navRoute);
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
        SnapshotStub snapshot;
        QQmlComponent provider(&engine, QUrl("qrc:/ui/state/AppUiState.qml"));
        std::unique_ptr<QObject> model(
            provider.createWithInitialProperties({{"bridge", QVariant::fromValue(&snapshot)}}));
        require(model != nullptr, "Provider failed to load");
        for (const auto &mode : QStringList{"live", "hybrid", "mock"}) {
            model->setProperty("mode", mode);
            snapshot.publish(true);
            settle();
            auto device = list(model.get(), "devices").front().toMap();
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
