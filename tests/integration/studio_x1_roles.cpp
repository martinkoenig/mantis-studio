// Software-only composition: production Studio controllers/preview and public
// authenticated Client against the isolated fake X1 daemon. No runtime linkage.
#include "bridge.hpp"
#include "x1_role_controller.hpp"
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QJsonDocument>
#include <iostream>

#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(std::string("studio_x1_roles_daemon.cpp:") + std::to_string(__LINE__) + \
                                     ": " + #x);                                                             \
    } while (false)
namespace w = mantis::wire::v1;
void until(const std::function<bool()> &fn, const char *phase) {
    QElapsedTimer timer;
    timer.start();
    while (!fn()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        if (timer.elapsed() > 15000)
            throw std::runtime_error(std::string("Studio role timeout: ") + phase);
    }
}
void roleUntil(X1RoleController &c, const std::function<bool()> &fn, const char *phase) {
    try {
        until(fn, phase);
    } catch (const std::exception &e) {
        const auto s = c.state();
        QVariantMap diagnostic;
        for (const auto *key :
             {"fresh", "connected", "pending", "generation", "busy", "blocked", "jobId", "jobPhase", "job",
              "daemonJob", "reason", "error", "message", "hasChange", "canValidate", "canApply"})
            diagnostic[key] = s.value(key);
        throw std::runtime_error(
            std::string(e.what()) + " " +
            QJsonDocument::fromVariant(diagnostic).toJson(QJsonDocument::Compact).toStdString());
    }
}
void idle(X1RoleController &c) {
    until([&] { return !c.state()["pending"].toBool(); }, "role RPC");
}
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    try {
        mantis::client::Client client;
        if (argc == 2) {
            X1RoleController refused(std::make_shared<PublicX1RoleClient>());
            refused.setVisible(true);
            refused.setConnected(true);
            idle(refused);
            CHECK(!refused.state()["canApply"].toBool());
            if (std::string(argv[1]) == "bad-auth") {
                CHECK(!refused.state()["fresh"].toBool());
                CHECK(!refused.state()["error"].toMap().isEmpty());
            } else {
                CHECK(refused.state()["fresh"].toBool());
                CHECK(!refused.state()["supported"].toBool());
                CHECK(!refused.state()["capability"].toBool());
            }
            return 0;
        }
        StudioBridge bridge;
        MeasurementView left, right;
        left.setSize({64, 48});
        right.setSize({64, 48});
        bridge.attachPreview(&left, &right);
        X1RoleController roles(std::make_shared<PublicX1RoleClient>());
        QObject::connect(&bridge, &StudioBridge::snapshotReady, &roles, &X1RoleController::observeSnapshot);
        QObject::connect(&bridge, &StudioBridge::changed, &roles,
                         [&] { roles.setConnected(bridge.connected()); });
        until([&] { return bridge.connected(); }, "bridge authentication");
        const auto initial = client.x1_roles();
        CHECK(initial.supported() && !initial.swapped());
        roles.setDeviceId(QString::fromStdString(initial.parent_id()));
        roles.setVisible(true);
        idle(roles);
        auto captureAndPreview = [&](bool swapped) {
            const auto state = client.x1_roles();
            bridge.startCapture(QString::fromStdString(state.parent_id()));
            until(
                [&] {
                    return !bridge.busy() && bridge.capturing() && bridge.previewCurrent() &&
                           left.hasImage() && right.hasImage();
                },
                "canonical dual preview");
            const auto delta =
                (right.image().pixelColor(0, 0).red() - left.image().pixelColor(0, 0).red() + 256) % 256;
            CHECK(delta == (swapped ? 159 : 97)); // Independent physical sensor pixel signature.
            const auto snapshot = client.snapshot();
            std::string capture;
            for (const auto &c : snapshot.captures())
                if (c.active())
                    capture = c.id();
            CHECK(!capture.empty());
            mantis::data::Published packet;
            until(
                [&] {
                    packet = client.preview(capture);
                    return bool(packet);
                },
                "independent latest preview packet");
            CHECK(packet->frames.size() == 2);
            for (const auto &frame : packet->frames) {
                const auto &role = frame->header.metadata.at("role");
                CHECK(frame->header.frame.id.value == "org.mantis.camera." + role + ".optical");
                const bool second = (role == "right") != swapped;
                CHECK(frame->header.metadata.at("identity") ==
                      (second ? "fixture/ov9281 20-0060" : "fixture/ov9281 18-0060"));
                auto raw = frame->attributes[0].buffer.map_read();
                CHECK(raw);
                const auto expected = (frame->header.sequence.value * 7u + (second ? 97u : 0u)) & 255u;
                CHECK(std::to_integer<unsigned>((*raw)[0]) == expected);
            }
            bridge.stopCapture();
            until([&] { return !bridge.busy() && !bridge.capturing(); }, "capture stop");
        };
        captureAndPreview(false);
        // Role Jobs reject recorder retirement pressure; do not wait/retry a
        // mutation until it happens to win. Observe retirement before proposing.
        roles.refresh();
        until([&] { return roles.state()["canPropose"].toBool(); }, "recorder and Job retirement");
        roles.proposeSwap();
        roles.validate();
        roleUntil(
            roles, [&] { return roles.state()["canApply"].toBool(); },
            "validation Job and refreshed discovery");
        CHECK(roles.state()["proposed"].toList()[0].toMap()["identity"] == "fixture/ov9281 20-0060");
        const auto oldKey = roles.state()["confirmation"].toString();
        roles.apply(oldKey);
        until(
            [&] { return roles.state()["generation"] == "1" && roles.state()["jobId"].toString().isEmpty(); },
            "durable apply and requery");
        roles.setDeviceId(roles.state()["parent"].toString());
        CHECK(roles.state()["current"].toList()[0].toMap()["sensor"] == "ov9281 20-0060");
        roles.apply(oldKey);
        idle(roles);
        CHECK(client.x1_roles().generation() == 1); // Confirmations cannot be replayed.
        captureAndPreview(true);
        std::cout
            << "Studio public role Jobs and canonical preview physical signatures passed (software only)\n";

    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
