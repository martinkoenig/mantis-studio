#include "bridge.hpp"
#include <QCoreApplication>
#include <QSignalSpy>
#include <QTest>
#include <atomic>
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
    explicit ObservedBridge(bool runtime = false, CloudRetry::Clock clock = CloudRetry::monotonicNow)
        : StudioBridge(nullptr, runtime, std::move(clock)) {}
    using StudioBridge::applyResult;
    using StudioBridge::collectResult;
    using StudioBridge::projectGeneration;
    using StudioBridge::retryState;
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
static uint64_t dataCount(const mantis::client::Client &client, const std::string &project,
                          const std::string &artifact) {
    mantis::wire::v1::Request request;
    request.mutable_events(); // Read-only fixture counters, not a new production command.
    const auto response = client.call(request);
    for (const auto &event : response.events())
        if (event.kind() == "artifact_data" && event.message() == project && event.component() == artifact)
            return event.sequence();
    return 0;
}
static void retryCoverage(const mantis::client::Client &client) {
    using State = CloudRetry::State;
    const std::string stable = "fixture-stable-project";
    auto time = std::make_shared<std::atomic<int64_t>>(0);
    CloudRetry::Clock clock = [time] { return std::optional{time->load()}; };
    ObservedBridge bridge;
    auto poll = [&](std::optional<std::string> manual = {}) {
        auto result = ObservedBridge::collectResult(client, {}, manual, {}, {}, bridge.retryState(), clock);
        bridge.applyResult(result);
        return result;
    };
    mode(client, "auto-missing");
    CHECK(poll().artifactAttempted && bridge.connected());
    const auto missing = issue(bridge, "artifact");
    CHECK(missing["code"] == static_cast<int>(mantis::Status::not_found));
    QSignalSpy changes(&bridge, &StudioBridge::changed);
    for (int i = 1; i <= 40; ++i) {
        time->store(i * 500);
        CHECK(!poll().artifactAttempted && bridge.connected() && issue(bridge, "artifact") == missing);
    }
    CHECK(changes.size() == 40 && dataCount(client, stable, "missing") == 1);
    std::cout << "Permanent missing: 41 confirmed refreshes, exactly one artifact_data request\n";
    mode(client, "retry-corrupt");
    CHECK(poll().artifactAttempted && bridge.connected() && bridge.selectedArtifact().isEmpty());
    const auto corrupt = issue(bridge, "artifact");
    CHECK(corrupt["code"] == static_cast<int>(mantis::Status::corrupt));
    for (int i = 0; i < 20; ++i)
        CHECK(!poll().artifactAttempted);
    const auto corruptCount = dataCount(client, stable, "corrupt");
    CHECK(corruptCount == 1);
    for (const auto &id : {"unknown", "retained", ""}) {
        const auto rejected = poll(std::string{id});
        CHECK(!rejected.cloud && issue(bridge, "artifact")["component"] == "studio.artifact");
    }
    CHECK(dataCount(client, stable, "corrupt") == corruptCount && !poll().artifactAttempted);
    CHECK(poll("corrupt").artifactAttempted); // A deliberate retry is exactly one request.
    CHECK(dataCount(client, stable, "corrupt") == corruptCount + 1 && !poll().artifactAttempted);
    mode(client, "retry-repaired"); // Same descriptor/hash: no claim of automatic repair detection.
    CHECK(!poll().artifactAttempted);
    CHECK(poll("corrupt").cloud && bridge.selectedArtifact() == "corrupt" && bridge.errorDetails().empty());
    CHECK(!poll().artifactAttempted && bridge.retryState().state() == State::loaded);
    mode(client, "retry-new");
    CHECK(poll().cloud && bridge.selectedArtifact() == "fresh");
    CHECK(dataCount(client, stable, "fresh") == 1 && !poll().artifactAttempted);
    time->store(0);
    mode(client, "retry-transient");
    CHECK(poll().artifactAttempted && bridge.connected());
    CHECK(dataCount(client, stable, "transient") == 1);
    for (unsigned attempt = 2; attempt <= 6; ++attempt) {
        const auto deadline = *bridge.retryState().deadline();
        time->store(deadline - 1);
        CHECK(!poll().artifactAttempted);
        time->store(deadline);
        CHECK(poll().artifactAttempted && bridge.connected());
        CHECK(dataCount(client, stable, "transient") == attempt);
    }
    time->store(INT64_MAX);
    for (int i = 0; i < 40; ++i)
        CHECK(!poll().artifactAttempted);
    CHECK(dataCount(client, stable, "transient") == 6);
    mode(client, "retry-transient-fixed");
    CHECK(!poll().artifactAttempted && poll("transient").cloud);
    CHECK(bridge.selectedArtifact() == "transient" && bridge.errorDetails().empty());
    std::cout
        << "Transient IO: six attempts at 0/2/6/14/30/60s; no requests after exhaustion; manual recovery\n";
    time->store(0);
    mode(client, "retry-loss");
    const auto loss = poll();
    CHECK(!loss.snapshot && loss.issues.size() == 2 && !bridge.connected());
    const auto retained = bridge.project();
    CHECK(!poll().artifactAttempted && bridge.project() == retained);
    CHECK(dataCount(client, stable, "loss") == 1);
    mode(client, "retry-restored");
    CHECK(!poll().artifactAttempted && bridge.connected() && !issue(bridge, "artifact").empty());
    CHECK(poll("loss").cloud && bridge.errorDetails().empty());
    mode(client, "retry-auth-data");
    CHECK(!poll().snapshot && !bridge.connected());
    CHECK(issue(bridge, "snapshot")["code"] == static_cast<int>(mantis::Status::invalid_argument));
    CHECK(!poll().artifactAttempted && dataCount(client, stable, "auth-data") == 1);
    mode(client, "retry-auth-restored");
    CHECK(!poll().artifactAttempted && bridge.connected());
    CHECK(poll("auth-data").cloud && bridge.errorDetails().empty());
    mode(client, "retry-wrong-type");
    CHECK(poll().artifactAttempted && bridge.connected());
    CHECK(issue(bridge, "artifact")["code"] == static_cast<int>(mantis::Status::incompatible));
    for (int i = 0; i < 20; ++i)
        CHECK(!poll().artifactAttempted);
    CHECK(dataCount(client, stable, "wrong-type") == 1);
    mode(client, "retry-older");
    CHECK(poll().artifactAttempted && bridge.retryState().state() == State::suppressed);
    CHECK(poll("older").artifactAttempted && !poll().artifactAttempted);
    mode(client, "retry-older-loaded");
    CHECK(poll("older").cloud && bridge.selectedArtifact() == "older");
    CHECK(!poll().artifactAttempted && bridge.retryState().state() == State::suppressed);
    CHECK(dataCount(client, stable, "current") == 1);
    mode(client, "retry-duplicates");
    const auto rejectedDuplicate = poll();
    CHECK(!rejectedDuplicate.cloud && issue(bridge, "artifact")["component"] == "studio.artifact");
    CHECK(!poll().artifactAttempted && !poll("duplicate").cloud);
    CHECK(dataCount(client, stable, "duplicate") == 0);
    mode(client, "retry-clock");
    const auto invalidClock = ObservedBridge::collectResult(client, {}, {}, {}, {}, {},
                                                            []() -> std::optional<int64_t> { throw 42; });
    bridge.applyResult(invalidClock);
    CHECK(bridge.connected() && bridge.retryState().state() == State::cooling &&
          !bridge.retryState().deadline());
    for (int i = 0; i < 20; ++i)
        CHECK(!poll().artifactAttempted);
    CHECK(dataCount(client, stable, "clock-failure") == 1);
    mode(client, "retry-clock-fixed");
    CHECK(poll("clock-failure").cloud);
    mode(client, "retry-revision");
    CHECK(poll().artifactAttempted && !poll().artifactAttempted);
    mode(client, "retry-revision-fixed");
    CHECK(poll().cloud && bridge.selectedArtifact() == "revision");
    mode(client, "retry-A");
    CHECK(poll().artifactAttempted && !poll().artifactAttempted);
    mode(client, "retry-B");
    CHECK(poll().cloud && bridge.selectedArtifact() == "same");
    CHECK(dataCount(client, "/retry/A", "same") == 1 && dataCount(client, "/retry/B", "same") == 1);
    mode(client, "retry-empty-project");
    auto emptyProject = ObservedBridge::collectResult(client, {}, "valid", {}, std::string{});
    CHECK(emptyProject.cloud && emptyProject.cloud_project.empty());
    mode(client, "retry-B");
    auto wrongProject = ObservedBridge::collectResult(client, {}, "same", {}, std::string{});
    CHECK(wrongProject.snapshot && !wrongProject.artifactAttempted && !wrongProject.cloud);
    ObservedBridge generations;
    mode(client, "retry-A");
    StudioResult a;
    a.snapshot = client.snapshot();
    generations.applyResult(a);
    StudioResult late = a;
    late.requestGeneration = generations.projectGeneration();
    late.retry = generations.retryState();
    late.retry->succeeded();
    late.cloud = emptyProject.cloud;
    late.cloud_id = late.newest_id = "same";
    late.cloud_project = "/retry/A";
    mode(client, "retry-B");
    StudioResult b;
    b.snapshot = client.snapshot();
    generations.applyResult(b);
    mode(client, "retry-A");
    a.snapshot = client.snapshot();
    generations.applyResult(a);
    QSignalSpy staleSignals(&generations, &StudioBridge::changed);
    generations.applyResult(late);
    CHECK(generations.project() == "/retry/A" && generations.selectedArtifact().isEmpty());
    CHECK(generations.retryState().state() == State::ready && staleSignals.empty());
    mode(client, "retry-empty");
    CHECK(!poll().artifactAttempted && !bridge.retryState().key());
    // Exercise the production watcher/GUI lease with a deterministic held wire reply.
    mode(client, "retry-valid");
    {
        ObservedBridge live(true, clock);
        bool leaseObserved{};
        QObject::connect(&live, &StudioBridge::snapshotReady, &live, [&](const auto &) {
            CHECK(live.busy()); // Includes GUI application, not only worker execution.
            leaseObserved = true;
            live.selectArtifact("valid");
            live.refresh();
        });
        CHECK(QTest::qWaitFor([&] { return live.connected() && !live.busy(); }, 5000));
        CHECK(leaseObserved);
        mode(client, "retry-async-hold");
        live.refresh();
        CHECK(live.busy());
        CHECK(QTest::qWaitFor([&] { return dataCount(client, "/retry/async-A", "held") == 1; }, 5000));
        live.selectArtifact("held");
        live.refresh();
        live.runPipeline("forbidden-pending");
        CHECK(live.busy() && dataCount(client, "/retry/async-A", "held") == 1);
        mode(client, "retry-async-B");
        StudioResult newProject;
        newProject.snapshot = client.snapshot(); // A real confirmed wire snapshot during pending old work.
        live.applyResult(newProject);
        CHECK(live.project() == "/retry/async-B" && live.selectedArtifact().isEmpty());
        const auto generation = live.projectGeneration();
        mode(client, "release");
        CHECK(QTest::qWaitFor([&] { return !live.busy(); }, 5000));
        CHECK(live.projectGeneration() == generation && live.project() == "/retry/async-B" &&
              live.selectedArtifact().isEmpty());
        CHECK(live.retryState().state() == State::ready &&
              live.retryState().key()->project == "/retry/async-B");
        live.refresh();
        CHECK(QTest::qWaitFor([&] { return !live.busy() && live.selectedArtifact() == "held"; }, 5000));
        CHECK(dataCount(client, "/retry/async-B", "held") == 1);
    }
    std::cout << "PASS: exact project/revision keys, manual validation/recovery, stale confirmation, auth "
                 "loss and pending/late-worker authority\n";
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        CHECK(argc == 2);
        mantis::data::Packet packet;
        packet.type = mantis::schema::points;
        mantis::data::write_packet(argv[1], packet);
        packet.type = mantis::schema::mesh;
        mantis::data::write_packet(std::string{argv[1]} + ".wrong", packet);
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
        mode(client, "manual:valid");
        auto loaded = ObservedBridge::collectResult(client, {}, "valid");
        CHECK(loaded.cloud && loaded.cloud_id == "valid");
        bridge.applyResult(loaded);
        CHECK(bridge.selectedArtifact() == "valid");
        // The fixture deliberately changes raw project identity on every snapshot.
        // UI-M2a strengthens invalidation: old selections clear after confirmation,
        // while the existing phase/code/component and stale-state assertions remain.
        const auto beforeDataFailure = bridge.project();
        mode(client, "manual:missing");
        auto failedData = ObservedBridge::collectResult(client, {}, "missing");
        CHECK(failedData.snapshot && failedData.issues.size() == 1 && !failedData.cloud);
        CHECK(failedData.issues[0].cause.has_value());
        CHECK(failedData.issues[0].phase == "artifact");
        CHECK(failedData.issues[0].cause->code == mantis::Status::io);
        CHECK(failedData.issues[0].cause->component == "platform");
        bridge.applyResult(failedData);
        CHECK(bridge.connected() && bridge.capturing() && bridge.project() != beforeDataFailure);
        CHECK(bridge.selectedArtifact().isEmpty() && !issue(bridge, "artifact").empty());
        mode(client, "manual:corrupt");
        auto corruptData = ObservedBridge::collectResult(client, {}, "corrupt");
        CHECK(corruptData.snapshot && corruptData.issues.size() == 1);
        CHECK(corruptData.issues[0].cause.has_value());
        CHECK(corruptData.issues[0].cause->code == mantis::Status::corrupt);
        bridge.applyResult(corruptData);
        CHECK(bridge.connected() && bridge.selectedArtifact().isEmpty());
        CHECK(issue(bridge, "artifact")["code"].toInt() == static_cast<int>(mantis::Status::corrupt));
        // A control failure during artifact access cannot reuse the preceding snapshot.
        const auto retainedProject = bridge.project();
        mode(client, "manual:transport-data");
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
        mode(client, "manual:valid");
        bridge.applyResult(ObservedBridge::collectResult(client, {}, "valid"));
        mode(client, "active");
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
        retryCoverage(client);
        CHECK(snapshots.size() > 10);
        std::cout << "PASS: structured operation/data failures, confirmed snapshots, loss, authentication "
                     "and recovery\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
