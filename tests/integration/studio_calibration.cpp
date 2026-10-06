#include "calibration_controller.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <QTimer>
#include <iostream>
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(#x);                                                                    \
    } while (false)
QVariantMap selected(const CalibrationController &c) {
    return c.state()["selected"].toMap();
}
QVariantMap jobs(const CalibrationController &c) {
    return c.state()["jobs"].toMap();
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        CHECK(argc == 4);
        CalibrationController c;
        QTimer refresh;
        refresh.setInterval(25);
        QObject::connect(&refresh, &QTimer::timeout, &c, &CalibrationController::refresh);
        refresh.start();
        int ticks{};
        QTimer heartbeat;
        heartbeat.setInterval(1);
        QObject::connect(&heartbeat, &QTimer::timeout, &app, [&] { ++ticks; });
        heartbeat.start();
        auto until = [&](const std::function<bool()> &condition) {
            QElapsedTimer t;
            t.start();
            while (!condition()) {
                QCoreApplication::processEvents();
                QThread::msleep(1);
                if (t.elapsed() > 90000)
                    throw std::runtime_error("Studio daemon operation timeout");
                auto e = c.state()["errors"].toMap();
                if (!e.empty())
                    throw std::runtime_error(e.begin().value().toMap()["message"].toString().toStdString());
            }
        };
        c.refresh();
        until([&] { return c.artifacts().size() == 4; });
        until([&] { return !c.devices().empty(); });
        const auto recordingDevice = c.devices().front().toMap()["id"].toString();
        c.selectDevice(recordingDevice);
        c.startCapture();
        until(
            [&] { return c.state()["recording"].toBool() && c.state()["captureFrames"].toULongLong() >= 8; });
        const auto recordedRaw = c.state()["captureRaw"].toString();
        CHECK(!recordedRaw.isEmpty());
        c.stopCapture();
        until([&] {
            for (const auto &capture : c.captures())
                if (capture.toMap()["id"] == recordedRaw)
                    return capture.toMap()["ready"].toBool();
            return false;
        });
        CHECK(!c.state()["recording"].toBool());
        CHECK(c.state()["captureState"].toString().contains("finalized"));
        c.setCaptureSelected(recordedRaw, true);
        CHECK(c.state()["rawIds"].toStringList().contains(recordedRaw));
        c.setCaptureSelected(recordedRaw, false);
        c.createTarget({{"pattern", "checkerboard"}, {"squaresX", 8}, {"squaresY", 6}, {"squareMm", 40}});
        until([&] { return !selected(c)["target"].toString().isEmpty(); });
        CHECK(c.state()["target"].toMap()["revision"].toInt() == 1);
        c.setCaptureSelected(argv[2], true);
        c.setCaptureSelected(argv[3], true);
        c.buildDataset(3);
        until([&] { return !jobs(c)["dataset"].toMap()["id"].toString().isEmpty(); });
        auto datasetJob = jobs(c)["dataset"].toMap()["id"].toString();
        until([&] { return !selected(c)["dataset"].toString().isEmpty(); });
        CHECK(jobs(c)["dataset"].toMap()["state"] == "Completed");
        CHECK(jobs(c)["dataset"].toMap()["id"] == datasetJob);
        CHECK(c.state()["dataset"].toMap()["records"].toInt() == 4);
        CHECK(c.state()["dataset"].toMap()["rawCount"].toInt() == 2);
        // Reuse the deterministic M6 selected-observation fixture, without generating
        // another costly population of camera images in a frontend test.
        c.selectArtifact("dataset", argv[1]);
        c.solveBoth(3);
        until([&] {
            return !selected(c)["left"].toString().isEmpty() && !selected(c)["right"].toString().isEmpty();
        });
        CHECK(jobs(c)["left"].toMap()["id"] != jobs(c)["right"].toMap()["id"]);
        const auto left = selected(c)["left"], right = selected(c)["right"];
        c.solveRig(3, "studio.fixture.rig", "Studio fixture rig");
        until([&] { return !selected(c)["rig"].toString().isEmpty(); });
        CHECK(c.state()["rig"].toMap()["left"] == left);
        CHECK(c.state()["rig"].toMap()["right"] == right);
        const auto rig = selected(c)["rig"].toString();
        CHECK(c.state()["active"].toMap().isEmpty());
        c.activate(recordingDevice, rig);
        QElapsedTimer mismatchTimer;
        mismatchTimer.start();
        while (c.state()["errors"].toMap()["activation"].toMap().isEmpty()) {
            QCoreApplication::processEvents();
            QThread::msleep(1);
            CHECK(mismatchTimer.elapsed() < 10000);
        }
        auto mismatch = c.state()["errors"].toMap()["activation"].toMap();
        CHECK(mismatch["status"] == "incompatible");
        CHECK(!mismatch["component"].toString().isEmpty());
        CHECK(!mismatch["message"].toString().isEmpty());
        c.selectDevice("studio.offline");
        CHECK(c.state()["active"].toMap().isEmpty());
        c.activate("studio.offline", rig);
        until([&] { return c.state()["active"].toMap()["id"] == rig; });
        CHECK(c.state()["active"].toMap()["revision"].toInt() == 1);
        // A fresh presentation object resumes only from public immutable list/info.
        CalibrationController resumed;
        resumed.refresh();
        QElapsedTimer timer;
        timer.start();
        while (resumed.choices("rig").empty()) {
            QCoreApplication::processEvents();
            QThread::msleep(1);
            CHECK(timer.elapsed() < 10000);
        }
        resumed.selectArtifact("rig", rig);
        CHECK(selected(resumed)["dataset"].toString() == QString::fromUtf8(argv[1]));
        CHECK(selected(resumed)["left"] == left && selected(resumed)["right"] == right);
        c.clearActive("studio.offline");
        until([&] { return c.state()["active"].toMap().isEmpty(); });
        CHECK(selected(c)["rig"] == rig);
        CHECK(ticks > 10);
        std::cout << "Studio controller → mantis-client → Protobuf → real mantisd passed; UI heartbeat "
                  << ticks << "\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
