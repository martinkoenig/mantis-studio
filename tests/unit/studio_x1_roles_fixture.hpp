#pragma once
#include "x1_role_client.hpp"
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <utility>

namespace x1_ui_test {
namespace w = mantis::wire::v1;
inline constexpr auto capability = "org.mantis.x1.role-config.v1";
inline constexpr auto parentA = "org.mantis.x1:fixture/sensor-a:fixture/sensor-b";
inline constexpr auto parentB = "org.mantis.x1:fixture/sensor-b:fixture/sensor-a";
struct Gate {
    std::mutex mutex;
    std::condition_variable cv;
    std::atomic<bool> entered{}, exited{};
    bool released{};
    void wait() {
        std::unique_lock lock(mutex);
        entered = true;
        cv.wait(lock, [&] { return released; });
        exited = true;
    }
    void release() {
        std::lock_guard lock(mutex);
        released = true;
        cv.notify_all();
    }
};
struct Release {
    std::shared_ptr<Gate> gate;
    ~Release() {
        if (gate)
            gate->release();
    }
};
inline w::X1RoleInfo mapping(bool swapped = false, uint64_t generation = 0) {
    w::X1RoleInfo i;
    i.set_schema_version(1);
    i.set_supported(true);
    i.set_generation(generation);
    i.set_swapped(swapped);
    i.set_parent_id(swapped ? parentB : parentA);
    i.set_discovery_token("discovery-initial");
    i.set_base_digest("fnv1a64:fixture-base");
    for (int n = 0; n < 2; ++n) {
        const bool second = bool(n) != swapped;
        auto *c = i.add_cameras();
        c->set_role(n ? "right" : "left");
        c->set_sensor(second ? "sensor-b" : "sensor-a");
        c->set_bus("fixture");
        c->set_identity(second ? "fixture/sensor-b" : "fixture/sensor-a");
        c->set_video_node(second ? "/dev/video20" : "/dev/video18");
        c->add_route(c->sensor());
        c->add_route(second ? "csi-b" : "csi-a");
        c->add_route(second ? "capture-b" : "capture-a");
    }
    return i;
}
class Fake final : public X1RoleClient {
  public:
    mutable std::mutex mutex;
    mutable w::X1RoleInfo info = mapping();
    mutable w::Response current;
    mutable std::optional<w::ActiveCalibrationBinding> binding;
    mutable std::vector<w::X1RoleRequest> validations, applies;
    mutable std::vector<std::string> cancellations;
    mutable std::shared_ptr<Gate> nextQuery, nextSubmit, nextCancel;
    mutable bool failQuery{}, failApplyReply{}, failCancel{};
    mutable std::atomic<int> queries{}, submissions{};
    Fake() {
        current.set_project_path("/fixture/roles.mantis");
        describe();
    }
    void describe() const {
        current.clear_devices();
        auto *d = current.add_devices();
        d->set_id(info.parent_id());
        d->add_capabilities(capability);
    }
    w::Response snapshot() const override {
        std::lock_guard lock(mutex);
        return current;
    }
    w::X1RoleInfo query() const override {
        w::X1RoleInfo out;
        std::shared_ptr<Gate> gate;
        bool fail;
        {
            std::lock_guard lock(mutex);
            out = info;
            gate = std::exchange(nextQuery, {});
            fail = failQuery;
        }
        ++queries;
        if (gate)
            gate->wait();
        if (fail)
            mantis::fail(mantis::Status::io, "Query transport unavailable", "fixture.transport");
        return out;
    }
    std::optional<w::ActiveCalibrationBinding> active(const std::string &) const override {
        std::lock_guard lock(mutex);
        return binding;
    }
    std::string submit(const w::X1RoleRequest &r, bool apply) const {
        std::shared_ptr<Gate> gate;
        {
            std::lock_guard lock(mutex);
            gate = std::exchange(nextSubmit, {});
        }
        ++submissions;
        if (gate)
            gate->wait();
        std::lock_guard lock(mutex);
        if (r.expected_generation() != info.generation() || r.discovery_token() != info.discovery_token() ||
            r.parent_id() != info.parent_id())
            mantis::fail(mantis::Status::incompatible, "Stale discovery", "fixture.roles");
        const std::string id = apply ? "apply-job" : "validate-job";
        (apply ? applies : validations).push_back(r);
        current.clear_jobs();
        auto *job = current.add_jobs();
        job->set_id(id);
        job->set_state("Queued");
        info.set_busy(true);
        info.set_job_id(id);
        if (apply && failApplyReply)
            mantis::fail(mantis::Status::io, "Apply reply lost", "fixture.transport");
        return id;
    }
    std::string validate(const w::X1RoleRequest &r) const override {
        return submit(r, false);
    }
    std::string apply(const w::X1RoleRequest &r) const override {
        return submit(r, true);
    }
    void cancel(const std::string &id) const override {
        std::shared_ptr<Gate> gate;
        {
            std::lock_guard lock(mutex);
            gate = std::exchange(nextCancel, {});
        }
        if (gate)
            gate->wait();
        std::lock_guard lock(mutex);
        if (failCancel)
            mantis::fail(mantis::Status::io, "Late cancel reply failed", "fixture.cancel");
        cancellations.push_back(id);
    }
    void edit(const std::function<void(Fake &)> &fn) {
        std::lock_guard lock(mutex);
        fn(*this);
    }
    void finish(std::string state = "Completed", bool retired = true) {
        std::lock_guard lock(mutex);
        auto *j = current.mutable_jobs(0);
        j->set_state(state);
        j->set_status(state == "Failed" ? "BUSY: recorder retirement incomplete" : "Job outcome");
        if (state == "Completed") {
            if (j->id() == "apply-job") {
                info = mapping(applies.back().swapped(), info.generation() + 1);
                describe();
            }
            info.set_discovery_token(j->id() == "apply-job" ? "applied-discovery" : "validated-discovery");
        }
        info.set_job_id(j->id());
        info.set_busy(!retired);
    }
};
} // namespace x1_ui_test
