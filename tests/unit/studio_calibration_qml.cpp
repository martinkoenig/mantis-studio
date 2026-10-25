#include "bridge.hpp"
#include "calibration_controller.hpp"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
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
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    QQuickStyle::setStyle("Basic");
    qInstallMessageHandler(handler);
    qmlRegisterType<mantis::render::PointCloudView>("Mantis.Render", 1, 0, "PointCloudView");
    qmlRegisterType<MeasurementView>("Mantis.Render", 1, 0, "MeasurementView");
    StudioBridge studio;
    CalibrationController controller;
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
