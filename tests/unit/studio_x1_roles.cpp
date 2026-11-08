#include "studio_x1_roles_fixture.hpp"
#include "x1_role_controller.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QThread>
#include <iostream>
#include <stdexcept>

#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(std::string("studio_x1_roles.cpp:") + std::to_string(__LINE__) + ": " + \
                                     #x);                                                                    \
    } while (false)
using namespace x1_ui_test;
void until(const std::function<bool()> &condition, const char *phase) {
    QElapsedTimer timer;
    timer.start();
    while (!condition()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        if (timer.elapsed() > 10000)
            throw std::runtime_error(std::string("Timeout: ") + phase);
    }
    QCoreApplication::processEvents();
}
void idle(X1RoleController &c) {
    until([&] { return !c.state()["pending"].toBool(); }, "request retirement");
}
void refresh(X1RoleController &c) {
    c.refresh();
    idle(c);
}
void ready(X1RoleController &c) {
    c.setDeviceId(parentA);
    c.setVisible(true);
    c.setConnected(true);
    idle(c);
    CHECK(c.state()["canPropose"].toBool());
}
void validate(X1RoleController &c, Fake &f) {
    c.proposeSwap();
    c.validate();
    idle(c);
    CHECK(c.state()["jobId"] == "validate-job");
    CHECK(!c.state()["canApply"].toBool());
    f.finish();
    refresh(c);
    CHECK(c.state()["canApply"].toBool());
}
void exactProposal() {
    auto f = std::make_shared<Fake>();
    X1RoleController c(f);
    bool wrongThread{};
    QString applied;
    QObject::connect(&c, &X1RoleController::changed, &c,
                     [&] { wrongThread |= QThread::currentThread() != c.thread(); });
    QObject::connect(&c, &X1RoleController::applied, &c, [&](QString p) { applied = p; });
    ready(c);
    c.proposeSwap();
    auto proposed = c.state()["proposed"].toList();
    CHECK(proposed[0].toMap()["identity"] == "fixture/sensor-b");
    CHECK(proposed[0].toMap()["role"] == "LEFT");
    CHECK(proposed[0].toMap()["route"].toStringList() == QStringList({"sensor-b", "csi-b", "capture-b"}));
    CHECK(proposed[1].toMap()["identity"] == "fixture/sensor-a");
    c.apply("unvalidated");
    CHECK(f->submissions == 0);
    c.validate();
    idle(c);
    CHECK(f->validations.size() == 1);
    const auto &r = f->validations[0];
    CHECK(r.schema_version() == 1 && r.has_expected_generation() && r.expected_generation() == 0);
    CHECK(r.has_swapped() && r.swapped());
    CHECK(r.parent_id() == parentA && r.discovery_token() == "discovery-initial");
    CHECK(!c.state()["canApply"].toBool());
    f->finish("Completed", false);
    refresh(c);
    CHECK(!c.state()["canApply"].toBool()); // Completed does not prove ownership retirement.
    f->edit([](Fake &f) { f.info.set_busy(false); });
    refresh(c);
    CHECK(c.state()["canApply"].toBool());
    CHECK(c.state()["discovery"] == "validated-discovery"); // Validate refreshed its incarnation.
    auto key = c.state()["confirmation"].toString();
    c.apply("wrong-confirmation");
    idle(c); // Observe retirement before asserting that no asynchronous mutation occurred.
    CHECK(f->applies.empty());
    c.apply(key);
    idle(c);
    CHECK(f->applies.size() == 1);
    CHECK(f->applies[0].discovery_token() == "validated-discovery");
    CHECK(f->applies[0].parent_id() == parentA && f->applies[0].expected_generation() == 0 &&
          f->applies[0].swapped());
    c.apply(key);
    CHECK(f->applies.size() == 1); // No double submission.
    f->finish();
    refresh(c);
    CHECK(applied == parentB);
    CHECK(c.state()["generation"] == "1");
    CHECK(c.state()["current"].toList()[0].toMap()["sensor"] == "sensor-b");
    c.setDeviceId(parentB);
    CHECK(c.state()["canPropose"].toBool());
    c.proposeSwap();
    c.validate();
    idle(c);
    f->finish();
    refresh(c);
    auto restore = c.state()["confirmation"].toString();
    CHECK(!restore.isEmpty());
    c.apply(restore);
    idle(c);
    CHECK(!f->applies.back().swapped()); // Desired base order, never blind toggling.
    f->finish();
    refresh(c);
    CHECK(c.state()["generation"] == "2" && applied == parentA);
    CHECK(!wrongThread);
}
void refusals() {
    const std::vector<std::function<void(Fake &)>> faults{
        [](Fake &f) { f.info.set_supported(false); },
        [](Fake &f) { f.info.set_schema_version(99); },
        [](Fake &f) { f.info.set_busy(true); },
        [](Fake &f) { f.info.set_blocked(true); },
        [](Fake &f) { f.current.mutable_devices(0)->clear_capabilities(); },
        [](Fake &f) { *f.current.add_devices() = f.current.devices(0); },
        [](Fake &f) { f.current.mutable_devices(0)->set_id("unrelated-parent"); },
        [](Fake &f) { f.info.mutable_cameras(1)->set_identity("fixture/sensor-a"); },
        [](Fake &f) { f.info.mutable_cameras(1)->set_video_node("/dev/video18"); },
        [](Fake &f) { f.info.mutable_cameras(0)->clear_route(); },
        [](Fake &f) { f.info.mutable_cameras(0)->set_route(0, "different-sensor"); },
        [](Fake &f) {
            f.info.mutable_cameras()->RemoveLast();
            f.info.set_swapped(true);
        },
        [](Fake &f) { f.binding.emplace(); },
        [](Fake &f) {
            f.binding.emplace();
            f.binding->mutable_artifact()->set_id("rig");
            f.binding->set_logical_device_id("other-parent");
        },
        [](Fake &f) { f.info.mutable_cameras(0)->set_sensor("unsafe\nidentity"); },
        [](Fake &f) { f.info.set_discovery_token(""); },
        [](Fake &f) { f.current.add_captures()->set_active(true); },
        [](Fake &f) { f.current.add_jobs()->set_state("Running"); },
        [](Fake &f) {
            f.binding.emplace();
            f.binding->mutable_artifact()->set_id("active-rig");
            f.binding->set_logical_device_id(parentA);
        },
        [](Fake &f) { f.current.clear_project_path(); }};
    for (const auto &fault : faults) {
        auto f = std::make_shared<Fake>();
        f->edit(fault);
        X1RoleController c(f);
        c.setDeviceId(parentA);
        c.setVisible(true);
        c.setConnected(true);
        idle(c);
        CHECK(!c.state()["canPropose"].toBool());
        c.proposeSwap();
        c.validate();
        c.apply("arbitrary");
        idle(c);
        CHECK(!c.state()["canApply"].toBool() && f->submissions == 0);
    }
    auto f = std::make_shared<Fake>();
    X1RoleController c(f);
    ready(c);
    f->edit([](Fake &f) {
        f.binding.emplace();
        f.binding->mutable_artifact()->set_id("keep-this-rig");
        f.binding->set_logical_device_id(parentA);
    });
    refresh(c);
    CHECK(c.state()["activeRig"] == "keep-this-rig");
    c.proposeSwap();
    c.validate();
    CHECK(f->submissions == 0);
    // Fixture simulates a separate, explicit calibration API clear, never a role action.
    f->edit([](Fake &f) { f.binding.reset(); });
    refresh(c);
    CHECK(c.state()["canPropose"].toBool());
}
void invalidation() {
    for (int mode = 0; mode < 7; ++mode) {
        auto f = std::make_shared<Fake>();
        X1RoleController c(f);
        ready(c);
        validate(c, *f);
        const auto key = c.state()["confirmation"].toString();
        switch (mode) {
        case 0:
            c.setDeviceId("other-parent");
            break;
        case 1:
            c.setConnected(false);
            c.setConnected(true);
            idle(c);
            break;
        case 2:
            c.setVisible(false);
            c.setVisible(true);
            idle(c);
            break;
        case 3:
            f->edit([](Fake &f) { f.info.set_discovery_token("reconnected-discovery"); });
            refresh(c);
            break;
        case 4:
            f->edit([](Fake &f) { f.info.set_generation(1); });
            refresh(c);
            break;
        case 5:
            f->edit([](Fake &f) { f.current.set_project_path("/fixture/other.mantis"); });
            c.observeSnapshot(f->snapshot());
            idle(c);
            break;
        case 6:
            c.proposeSwap();
            break;
        }
        c.apply(key);
        idle(c);
        CHECK(f->applies.empty());
        CHECK(!c.state()["canApply"].toBool());
    }
    auto f = std::make_shared<Fake>();
    X1RoleController c(f);
    ready(c);
    validate(c, *f);
    auto key = c.state()["confirmation"].toString();
    // Change only after confirmation but before the worker's immediate RPC recheck.
    f->edit([](Fake &f) { f.info.set_discovery_token("external-change"); });
    c.apply(key);
    idle(c);
    CHECK(f->applies.empty());
    CHECK(!c.state()["canApply"].toBool());
    CHECK(c.state()["error"].toMap()["component"] == "studio.x1-roles");
}
void outcomesAndCancellation() {
    for (auto outcome : {"Failed", "Cancelled"}) {
        auto f = std::make_shared<Fake>();
        X1RoleController c(f);
        ready(c);
        c.proposeSwap();
        c.validate();
        idle(c);
        CHECK(c.state()["canCancel"].toBool());
        c.cancelJob();
        until([&] { return !c.state()["cancelling"].toBool(); }, "cancel acknowledgement");
        idle(c);
        CHECK(f->cancellations == std::vector<std::string>{"validate-job"});
        CHECK(!c.state()["jobId"].toString().isEmpty()); // Acknowledged cancel is not terminal.
        CHECK(!c.state()["canApply"].toBool());
        f->finish(outcome);
        refresh(c);
        CHECK(c.state()["jobId"].toString().isEmpty());
        CHECK(c.state()["job"].toMap()["state"] == outcome);
        CHECK(!c.state()["canApply"].toBool());
    }
    auto f = std::make_shared<Fake>();
    X1RoleController c(f);
    ready(c);
    validate(c, *f);
    f->edit([](Fake &f) { f.failApplyReply = true; });
    c.apply(c.state()["confirmation"].toString());
    idle(c);
    CHECK(!c.state()["canApply"].toBool());
    CHECK(c.state()["error"].toMap()["component"] == "fixture.transport");
    CHECK(c.state()["message"].toString().contains("outcome unknown"));
    CHECK(f->applies.size() == 1);
    refresh(c); // Query, never retry a possibly committed apply.
    CHECK(f->applies.size() == 1 && !c.state()["canApply"].toBool());
    f->finish();
    refresh(c);
    CHECK(c.state()["current"].toList()[0].toMap()["identity"] == "fixture/sensor-b");
}
void lateCancellation() {
    auto f = std::make_shared<Fake>();
    X1RoleController c(f);
    ready(c);
    c.proposeSwap();
    c.validate();
    idle(c);
    auto gate = std::make_shared<Gate>();
    Release release{gate};
    f->edit([&](Fake &f) {
        f.nextCancel = gate;
        f.failCancel = true;
    });
    c.cancelJob();
    until([&] { return gate->entered.load(); }, "cancel gate entry");
    f->edit([](Fake &f) { f.current.set_project_path("/fixture/new-project.mantis"); });
    c.observeSnapshot(f->snapshot());
    idle(c);
    gate->release();
    until([&] { return !c.state()["cancelling"].toBool(); }, "late cancel reply");
    idle(c);
    CHECK(c.state()["error"].toMap()["component"] != "fixture.cancel");
    CHECK(!c.state()["canApply"].toBool());
    f->finish("Cancelled");
    refresh(c);
    CHECK(c.state()["jobId"].toString().isEmpty());
}
void inFlightJobMonitoring() {
    for (bool applying : {false, true}) {
        auto f = std::make_shared<Fake>();
        X1RoleController c(f);
        ready(c);
        if (applying) {
            validate(c, *f);
            c.apply(c.state()["confirmation"].toString());
        } else {
            c.proposeSwap();
            c.validate();
        }
        idle(c);
        const auto queries = f->queries.load();
        refresh(c);
        CHECK(f->queries == queries); // No role reads across this Job's discovery replacement.
        CHECK(c.state()["job"].toMap()["state"] == "Queued");
        CHECK(!c.state()["canApply"].toBool());
        f->finish();
        refresh(c);
        if (applying) {
            CHECK(c.state()["generation"] == "1");
            CHECK(c.state()["message"].toString().startsWith("Roles applied"));
        } else
            CHECK(c.state()["canApply"].toBool());
    }
}
void wideGeneration() {
    auto f = std::make_shared<Fake>();
    f->edit([](Fake &f) { f.info.set_generation(9007199254740993ULL); });
    X1RoleController c(f);
    ready(c);
    CHECK(c.state()["generation"] == "9007199254740993");
    c.proposeSwap();
    c.validate();
    idle(c);
    CHECK(f->validations.size() == 1);
    CHECK(f->validations[0].expected_generation() == 9007199254740993ULL);
    f->finish();
    refresh(c);
    CHECK(c.state()["canApply"].toBool());
}
void lateReplies() {
    auto f = std::make_shared<Fake>();
    X1RoleController c(f);
    ready(c);
    auto gate = std::make_shared<Gate>();
    Release release{gate};
    f->edit([&](Fake &f) { f.nextQuery = gate; });
    c.refresh();
    until([&] { return gate->entered.load(); }, "query gate entry");
    c.setConnected(false);
    gate->release();
    idle(c);
    CHECK(!c.state()["fresh"].toBool() && !c.state()["canApply"].toBool());
    c.setConnected(true);
    idle(c);
    validate(c, *f);
    gate = std::make_shared<Gate>();
    Release releaseSubmit{gate};
    f->edit([&](Fake &f) { f.nextSubmit = gate; });
    c.apply(c.state()["confirmation"].toString());
    until([&] { return gate->entered.load(); }, "apply accepted-call gate entry");
    c.setVisible(false);
    c.setConnected(false);
    gate->release();
    idle(c);
    CHECK(c.state()["jobId"] == "apply-job"); // Late acknowledgement remains observable.
    CHECK(!c.state()["canApply"].toBool());
    f->finish();
    c.setConnected(true);
    idle(c);
    CHECK(c.state()["jobId"].toString().isEmpty());
    CHECK(c.state()["current"].toList()[0].toMap()["identity"] == "fixture/sensor-b");
    CHECK(c.state()["confirmation"].toString().isEmpty());
    auto dying = std::make_unique<X1RoleController>(f);
    gate = std::make_shared<Gate>();
    Release releaseDestroy{gate};
    f->edit([&](Fake &f) { f.nextQuery = gate; });
    dying->setVisible(true);
    dying->setConnected(true);
    until([&] { return gate->entered.load(); }, "destruction gate entry");
    dying.reset(); // Must not join blocked I/O or access the destroyed QObject on reply.
    gate->release();
    until([&] { return gate->exited.load(); }, "destruction gate release");
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        exactProposal();
        refusals();
        invalidation();
        outcomesAndCancellation();
        lateReplies();
        wideGeneration();
        inFlightJobMonitoring();
        lateCancellation();
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    std::cout
        << "Software-only Studio role state, exact requests, cancellation and stale-reply checks passed\n";
}
