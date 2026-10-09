#include "bridge.hpp"
#include <QCoreApplication>
#include <QSignalSpy>
#include <iostream>
#include <mantis/data_io.hpp>
#include <utility>

#define CHECK(value)                                                                                         \
    do {                                                                                                     \
        if (!(value))                                                                                        \
            throw std::runtime_error(#value);                                                                \
    } while (false)
class ObservedBridge : public StudioBridge {
  public:
    ObservedBridge() : StudioBridge(nullptr, false) {}
    using StudioBridge::applyResult;
    using StudioBridge::collectResult;
};
static void mode(const mantis::client::Client &client, const std::string &value) {
    mantis::wire::v1::Request request;
    request.mutable_plugin_enable()->set_id("fixture:" + value);
    (void)client.call(request);
}
static QVariantMap issue(const StudioBridge &bridge, const QString &phase) {
    for (const auto &value : bridge.errorDetails())
        if (value.toMap().value("phase") == phase)
            return value.toMap();
    return {};
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        CHECK(argc == 2);
        mantis::data::Packet packet;
        packet.type = mantis::schema::points;
        mantis::data::write_packet(argv[1], packet);
        mantis::client::Client client;
        ObservedBridge bridge;
        QSignalSpy snapshots(&bridge, &StudioBridge::snapshotReady);
        auto refresh = [&] { bridge.applyResult(ObservedBridge::collectResult(client)); };
        refresh();
        CHECK(bridge.connected() && bridge.capturing());
        CHECK(bridge.devices().size() == 1 && bridge.artifacts().size() == 1);
        for (const auto &[rejection, expectedCode] :
             {std::pair{"reject-busy", mantis::Status::busy}, std::pair{"reject-io", mantis::Status::io},
              std::pair{"reject-invalid", mantis::Status::invalid_argument}}) {
            const auto previous = bridge.project();
            const auto result = ObservedBridge::collectResult(
                client, [&](const auto &c) { (void)c.run_pipeline("active", rejection); });
            CHECK(result.snapshot && result.issues.size() == 1);
            CHECK(result.issues.front().cause.has_value());
            CHECK(result.issues.front().cause->code == expectedCode);
            bridge.applyResult(result);
            CHECK(bridge.connected() && bridge.capturing() && bridge.project() != previous);
            const auto error = issue(bridge, "operation");
            CHECK(error["component"] == "fixture.pipeline");
            CHECK(error["message"] == "Rejected fixture operation");
            CHECK(error["code"].toInt() == static_cast<int>(expectedCode));
            refresh();
            CHECK(bridge.connected() && issue(bridge, "operation") == error);
        }
        // A subsequent successful user operation acknowledges the previous operation failure.
        bridge.applyResult(ObservedBridge::collectResult(
            client, [](const auto &c) { (void)c.run_pipeline("active", "accepted"); }));
        CHECK(bridge.errorDetails().empty());
        auto loaded = ObservedBridge::collectResult(client, {}, "valid");
        CHECK(loaded.cloud && loaded.cloud_id == "valid");
        bridge.applyResult(loaded);
        CHECK(bridge.selectedArtifact() == "valid");
        // The fixture deliberately changes raw project identity on every snapshot.
        // UI-M2a strengthens invalidation: old selections clear after confirmation,
        // while the existing phase/code/component and stale-state assertions remain.
        const auto beforeDataFailure = bridge.project();
        auto failedData = ObservedBridge::collectResult(client, {}, "missing");
        CHECK(failedData.snapshot && failedData.issues.size() == 1 && !failedData.cloud);
        CHECK(failedData.issues[0].cause.has_value());
        CHECK(failedData.issues[0].phase == "artifact");
        CHECK(failedData.issues[0].cause->code == mantis::Status::io);
        CHECK(failedData.issues[0].cause->component == "platform");
        bridge.applyResult(failedData);
        CHECK(bridge.connected() && bridge.capturing() && bridge.project() != beforeDataFailure);
        CHECK(bridge.selectedArtifact().isEmpty() && !issue(bridge, "artifact").empty());
        auto corruptData = ObservedBridge::collectResult(client, {}, "corrupt");
        CHECK(corruptData.snapshot && corruptData.issues.size() == 1);
        CHECK(corruptData.issues[0].cause.has_value());
        CHECK(corruptData.issues[0].cause->code == mantis::Status::corrupt);
        bridge.applyResult(corruptData);
        CHECK(bridge.connected() && bridge.selectedArtifact().isEmpty());
        CHECK(issue(bridge, "artifact")["code"].toInt() == static_cast<int>(mantis::Status::corrupt));
        // A control failure during artifact access cannot reuse the preceding snapshot.
        const auto retainedProject = bridge.project();
        auto failedControl = ObservedBridge::collectResult(client, {}, "transport-data");
        CHECK(!failedControl.snapshot && failedControl.issues.size() == 2);
        bridge.applyResult(failedControl);
        CHECK(!bridge.connected() && !bridge.capturing() && bridge.lastKnownCapturing());
        CHECK(bridge.project() == retainedProject && bridge.selectedArtifact().isEmpty());
        CHECK(issue(bridge, "artifact")["component"] == "platform");
        CHECK(issue(bridge, "snapshot")["component"] == "platform");
        mode(client, "idle");
        refresh();
        CHECK(bridge.connected() && !bridge.capturing() && !bridge.lastKnownCapturing());
        CHECK(bridge.project() != retainedProject && issue(bridge, "snapshot").empty());
        CHECK(!issue(bridge, "artifact").empty()); // Unrelated refresh preserves root cause.
        bridge.applyResult(ObservedBridge::collectResult(client, {}, "valid"));
        CHECK(bridge.errorDetails().empty());
        // Failure after a valid server rejection retains both causes without assuming connection.
        const auto beforeLoss = bridge.project();
        bridge.applyResult(ObservedBridge::collectResult(
            client, [](const auto &c) { (void)c.run_pipeline("active", "reject-offline"); }));
        CHECK(!bridge.connected() && bridge.project() == beforeLoss);
        CHECK(issue(bridge, "operation")["component"] == "fixture.pipeline");
        CHECK(issue(bridge, "snapshot")["code"].toInt() == static_cast<int>(mantis::Status::io));
        mode(client, "active");
        refresh();
        CHECK(bridge.connected() && bridge.capturing() && issue(bridge, "snapshot").empty());
        bridge.applyResult(ObservedBridge::collectResult(
            client, [](const auto &c) { (void)c.run_pipeline("active", "transport"); }));
        CHECK(!bridge.connected() && !bridge.capturing() && bridge.lastKnownCapturing());
        CHECK(issue(bridge, "operation")["component"] == "platform");
        CHECK(issue(bridge, "snapshot")["component"] == "platform");
        mode(client, "active");
        refresh();
        CHECK(bridge.connected() && bridge.capturing());
        mode(client, "auth");
        const auto beforeAuth = bridge.project();
        refresh();
        CHECK(!bridge.connected() && bridge.project() == beforeAuth);
        CHECK(issue(bridge, "snapshot")["message"] == "Invalid local access token");
        CHECK(issue(bridge, "snapshot")["code"].toInt() ==
              static_cast<int>(mantis::Status::invalid_argument));
        mode(client, "active");
        refresh();
        CHECK(bridge.connected() && bridge.project() != beforeAuth);
        auto credentials = mantis::client::Endpoint::environment();
        credentials.token.clear();
        bridge.applyResult(ObservedBridge::collectResult(mantis::client::Client(credentials)));
        CHECK(!bridge.connected());
        CHECK(issue(bridge, "snapshot")["code"].toInt() ==
              static_cast<int>(mantis::Status::invalid_argument));
        refresh();
        CHECK(bridge.connected());
        // Untyped local failures retain their message without fabricated io/connectivity classification.
        bridge.applyResult(ObservedBridge::collectResult(
            client, [](const auto &) { throw std::runtime_error("local operation exception"); }));
        CHECK(bridge.connected() && !issue(bridge, "operation")["code"].isValid());
        CHECK(issue(bridge, "operation")["kind"] == "exception");
        mode(client, "auto-missing");
        for (int attempt = 0; attempt < 2; ++attempt) {
            auto automatic = ObservedBridge::collectResult(client);
            CHECK(automatic.snapshot && automatic.artifactAttempted && automatic.newest_id.empty());
            bridge.applyResult(automatic);
            CHECK(bridge.connected() && bridge.selectedArtifact().isEmpty());
        }
        CHECK(snapshots.size() > 10);
        std::cout << "PASS: structured operation/data failures, confirmed snapshots, loss, authentication "
                     "and recovery\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
