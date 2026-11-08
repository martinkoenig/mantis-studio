#include <QGuiApplication>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTimer>
#include <cstdio>

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    QTemporaryDir directory;
    if (!directory.isValid())
        return 2;
    QImage image(32, 32, QImage::Format_RGB32);
    image.fill(Qt::green);
    const auto path = directory.filePath("control.png");
    if (!image.save(path))
        return 2;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("imageSource", QUrl::fromLocalFile(path));
    engine.loadData("import QtQuick\nimport QtQuick.Window\n"
                    "Window { visible: true; width: 320; height: 240; "
                    "Image { anchors.fill: parent; source: imageSource } }");
    if (engine.rootObjects().empty())
        return 3;
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().front());
    if (!window)
        return 3;
    // Complete on an actual frame. This is a failure deadline, never a settling sleep.
    QObject::connect(
        window, &QQuickWindow::frameSwapped, &app,
        [&] {
            std::fprintf(stderr, "QT_CONTROL_FRAME runtime Qt %s\n", qVersion());
            app.exit(0);
        },
        Qt::QueuedConnection);
    QTimer::singleShot(10000, &app, [&] { app.exit(4); });
    window->update();
    return app.exec();
}
