#include <QCoreApplication>
#include <QEvent>
#include <QGuiApplication>
#include <QImage>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QTimer>
#include <memory>
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    QImage image(32, 32, QImage::Format_RGB32);
    image.fill(Qt::green);
    if (!image.save("/tmp/m3d1-qt-leak-probe.png")) return 2;
    {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData("import QtQuick\nimport QtQuick.Window\nWindow { visible: true; width: 320; height: 240; Image { anchors.fill: parent; source: 'file:///tmp/m3d1-qt-leak-probe.png' } }", QUrl());
        std::unique_ptr<QObject> root(component.create());
        auto *window = qobject_cast<QQuickWindow *>(root.get());
        if (!window) return 3;
        QTimer::singleShot(200, &app, &QGuiApplication::quit);
        app.exec();
        if (argc > 1) {
            window->setPersistentSceneGraph(false);
            window->setPersistentGraphics(false);
            window->hide();
            window->releaseResources();
            app.processEvents();
        }
        root.reset();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        app.processEvents();
        engine.clearComponentCache();
    }
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    app.processEvents();
    return 0;
}
