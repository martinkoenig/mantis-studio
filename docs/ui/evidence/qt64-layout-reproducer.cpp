#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QTimer>
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    QQmlApplicationEngine engine;
    engine.loadData(R"(import QtQuick
import QtQuick.Layouts
Window { width: 1536; height: 1024; visible: true
 property var rows: [1]
 ColumnLayout { anchors.fill: parent
  ColumnLayout { Layout.fillWidth: true
   Repeater { model: rows
    delegate: ColumnLayout { Layout.fillWidth: true
     Rectangle { Layout.fillWidth: true; implicitHeight: 50 }
    }
   }
  }
 }
})");
    auto *w = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QTimer::singleShot(100, &app, [&] {
        w->setProperty("rows", QVariantList{});
        w->resize(1080, 720);
    });
    QTimer::singleShot(300, &app, &QCoreApplication::quit);
    return app.exec();
}
