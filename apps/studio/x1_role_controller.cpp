#include "x1_role_controller.hpp"
#include <QUuid>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <cstdint>
#include <utility>

namespace {
namespace w = mantis::wire::v1;
QString q(const std::string &s) {
    return QString::fromStdString(s);
}
constexpr auto capability = "org.mantis.x1.role-config.v1";
bool literal(const std::string &s, size_t bound = 2048) {
    if (s.empty() || s.size() > bound)
        return false;
    const auto text = q(s);
    if (text.toUtf8() != QByteArray(s.data(), static_cast<qsizetype>(s.size())))
        return false;
    return std::none_of(text.begin(), text.end(), [](QChar c) {
        return c.category() == QChar::Other_Control || c.category() == QChar::Other_Format ||
               c == QChar::ReplacementCharacter;
    });
}
bool topology(const w::X1RoleInfo &i) {
    if (i.schema_version() != 1 || !literal(i.parent_id()) || !literal(i.discovery_token(), 128) ||
        !literal(i.base_digest(), 128) || i.cameras_size() != 2)
        return false;
    for (int n = 0; n < 2; ++n) {
        const auto &c = i.cameras(n);
        if (c.role() != (n ? "right" : "left") || !literal(c.sensor()) || !literal(c.bus()) ||
            !literal(c.identity()) || c.identity() != c.bus() + "/" + c.sensor() ||
            !literal(c.video_node()) || c.route_size() < 1 || c.route_size() > 32 || c.route(0) != c.sensor())
            return false;
        for (const auto &r : c.route())
            if (!literal(r))
                return false;
    }
    return i.cameras(0).identity() != i.cameras(1).identity() &&
           i.cameras(0).video_node() != i.cameras(1).video_node();
}
bool same(const w::X1RoleInfo &a, const w::X1RoleInfo &b, bool token = true) {
    if (a.schema_version() != b.schema_version() || a.supported() != b.supported() ||
        a.blocked() != b.blocked() || a.generation() != b.generation() || a.parent_id() != b.parent_id() ||
        a.base_digest() != b.base_digest() || a.swapped() != b.swapped() ||
        (token && a.discovery_token() != b.discovery_token()) || a.cameras_size() != b.cameras_size())
        return false;
    for (int n = 0; n < a.cameras_size(); ++n)
        if (a.cameras(n).SerializeAsString() != b.cameras(n).SerializeAsString())
            return false;
    return true;
}
bool advertised(const w::Response &s, const std::string &parent) {
    int matches = 0;
    for (const auto &d : s.devices()) {
        if (d.id() != parent)
            continue;
        if (!d.parent().empty() ||
            std::find(d.capabilities().begin(), d.capabilities().end(), capability) == d.capabilities().end())
            return false;
        ++matches;
    }
    return matches == 1;
}
QVariantMap failure(const mantis::Error &e) {
    return {{"code", static_cast<int>(e.code)},
            {"component", q(e.component).left(512)},
            {"message", q(e.message).left(4096)}};
}
void require(bool value, const char *message) {
    if (!value)
        mantis::fail(mantis::Status::incompatible, message, "studio.x1-roles");
}
QVariantList cameras(const w::X1RoleInfo &i, bool swap) {
    QVariantList out;
    swap = swap && i.cameras_size() == 2;
    for (int n = 0; n < std::min(i.cameras_size(), 2); ++n) {
        const auto &c = i.cameras(swap ? 1 - n : n);
        QStringList route;
        for (int j = 0; j < std::min(c.route_size(), 32); ++j)
            route << q(c.route(j)).left(2048);
        out << QVariantMap{{"role", n ? "RIGHT" : "LEFT"},         {"sensor", q(c.sensor()).left(2048)},
                           {"bus", q(c.bus()).left(2048)},         {"identity", q(c.identity()).left(2048)},
                           {"node", q(c.video_node()).left(2048)}, {"route", route}};
    }
    return out;
}
bool appliedMapping(const w::X1RoleInfo &before, const w::X1RoleInfo &after, bool desired) {
    if (!topology(after) || after.blocked() || !after.supported() || after.swapped() != desired ||
        before.generation() == UINT64_MAX || after.generation() != before.generation() + 1 ||
        after.base_digest() != before.base_digest())
        return false;
    for (int n = 0; n < 2; ++n) {
        auto expected = before.cameras(1 - n);
        expected.set_role(n ? "right" : "left");
        if (expected.SerializeAsString() != after.cameras(n).SerializeAsString())
            return false;
    }
    return true;
}
} // namespace

X1RoleController::X1RoleController(std::shared_ptr<const X1RoleClient> client, QObject *parent)
    : QObject(parent), client_(std::move(client)) {}
X1RoleController::~X1RoleController() {
    ++*epoch_;
}
void X1RoleController::invalidate() {
    ++*epoch_;
    confirmation_.clear();
}
void X1RoleController::setDeviceId(QString id) {
    if (device_ == id)
        return;
    device_ = std::move(id);
    invalidate();
    emit changed();
}
void X1RoleController::setVisible(bool value) {
    if (visible_ == value)
        return;
    visible_ = value;
    invalidate();
    emit changed();
    if (value)
        refresh();
}
void X1RoleController::setConnected(bool value) {
    if (connected_ == value)
        return;
    connected_ = value;
    fresh_ = false;
    invalidate();
    emit changed();
    if (value && (visible_ || !job_.id.isEmpty()))
        refresh();
}
void X1RoleController::observeSnapshot(const w::Response &snapshot) {
    if (project_ != q(snapshot.project_path())) {
        project_ = q(snapshot.project_path());
        fresh_ = false;
        active_.clear();
        invalidate();
        emit changed();
    }
    if (connected_ && (visible_ || !job_.id.isEmpty()))
        refresh();
}
QString X1RoleController::refusal() const {
    if (!connected_)
        return "Disconnected / runtime state unconfirmed. Refresh after reconnecting.";
    if (!visible_)
        return "Open the Device setup stage to configure roles.";
    if (!fresh_)
        return "Unknown or stale role state. Refresh before proposing a change.";
    if (!info_.supported())
        return "Role configuration is unsupported or daemon opt-in is absent.";
    if (info_.blocked())
        return "Configuration blocked; explicit daemon reconciliation is required.";
    if (!topology(info_))
        return "Unsupported or incomplete physical identity / discovery evidence.";
    if (!advertised(snapshot_, info_.parent_id()))
        return "The exact parent does not advertise role configuration v1.";
    if (device_ != q(info_.parent_id()))
        return "Select the exact configured X1 parent in Device setup.";
    if (pending_ || !job_.id.isEmpty() || info_.busy())
        return "Busy; wait for the Job and physical-pair reservation to retire.";
    for (const auto &c : snapshot_.captures())
        if (c.active())
            return "Stop acquisition and wait for recorder retirement before configuration.";
    for (const auto &j : snapshot_.jobs())
        if (j.state() == "Queued" || j.state() == "Running")
            return "Another daemon Job prevents configuration.";
    if (!active_.isEmpty())
        return "An active rig is bound. Review and explicitly clear it before changing roles.";
    return {};
}
QVariantMap X1RoleController::state() const {
    const auto reason = refusal();
    const bool ready = reason.isEmpty() && proposed_ != info_.swapped();
    QVariantMap daemonJob;
    for (const auto &job : snapshot_.jobs()) {
        if (!info_.job_id().empty() && job.id() == info_.job_id()) {
            daemonJob = {{"id", q(job.id()).left(2048)},
                         {"state", q(job.state()).left(128)},
                         {"status", q(job.status()).left(4096)},
                         {"diagnostics", q(job.diagnostics()).left(4096)}};
            break;
        }
    }
    return {{"fresh", fresh_},
            {"connected", connected_},
            {"supported", info_.supported()},
            {"capability", advertised(snapshot_, info_.parent_id())},
            {"busy", info_.busy()},
            {"blocked", info_.blocked()},
            {"pending", pending_},
            {"cancelling", cancelling_},
            {"parent", q(info_.parent_id()).left(2048)},
            {"generation", QString::number(info_.generation())},
            {"discovery", q(info_.discovery_token()).left(128)},
            {"base", q(info_.base_digest()).left(128)},
            {"current", cameras(info_, false)},
            {"proposed", cameras(info_, proposed_ != info_.swapped())},
            {"proposedSwapped", proposed_},
            {"hasChange", proposed_ != info_.swapped()},
            {"canPropose", reason.isEmpty()},
            {"canValidate", ready},
            {"canApply", ready && !confirmation_.isEmpty()},
            {"confirmation", confirmation_},
            {"reason", reason},
            {"diagnostic", q(info_.diagnostic()).left(4096)},
            {"message", message_},
            {"error", error_},
            {"activeRig", active_},
            {"daemonJob", daemonJob},
            {"job", job_.display},
            {"jobId", job_.id},
            {"jobPhase", job_.phase},
            {"cancelRequested", cancelRequested_},
            {"canCancel",
             connected_ && !cancelling_ && !cancelRequested_ && !job_.id.isEmpty() &&
                 (job_.display.value("state") == "Queued" || job_.display.value("state") == "Running")}};
}
void X1RoleController::refresh() {
    if (!connected_ || (!visible_ && job_.id.isEmpty()))
        return;
    if (pending_) {
        refreshAgain_ = true;
        return;
    }
    run(Action::Query);
}
void X1RoleController::proposeSwap() {
    if (!refusal().isEmpty())
        return;
    invalidate();
    proposed_ = !proposed_;
    message_ = proposed_ == info_.swapped() ? "Proposal reset to the current assignment."
                                            : "Proposed LEFT / RIGHT exchange; validate before confirming.";
    emit changed();
}
void X1RoleController::validate() {
    if (!state().value("canValidate").toBool())
        return;
    invalidate();
    run(Action::Validate);
}
void X1RoleController::apply(QString key) {
    if (key.isEmpty() || key != confirmation_ || !state().value("canApply").toBool()) {
        error_ = failure({mantis::Status::incompatible, "Confirmation is stale; validate this proposal again",
                          "studio.x1-roles"});
        emit changed();
        return;
    }
    confirmation_.clear();
    run(Action::Apply);
}
void X1RoleController::run(Action action) {
    pending_ = true;
    const auto epoch = epoch_->load();
    const auto project = project_;
    const auto before = info_;
    const bool desired = proposed_;
    disconnect(&request_, nullptr, this, nullptr);
    connect(&request_, &QFutureWatcher<Reply>::finished, this,
            [this, action, epoch, project, before, desired] {
                pending_ = false;
                const auto r = request_.result();
                if (action != Action::Query && literal(r.id.toStdString())) {
                    // Retain acknowledged work even if the operator navigated away or disconnected.
                    job_ = {r.id,
                            project,
                            action == Action::Apply ? "Apply" : "Validate",
                            before,
                            epoch,
                            desired,
                            {{"state", "Awaiting Job status"}}};
                    cancelRequested_ = false;
                }
                if (epoch == epoch_->load()) {
                    if (!r.error.isEmpty()) {
                        error_ = r.error;
                        error_["phase"] = action == Action::Query   ? "query"
                                          : action == Action::Apply ? "apply"
                                                                    : "validate";
                        fresh_ = false;
                        invalidate();
                        message_ = action == Action::Apply ? "Apply acknowledgement failed; outcome unknown. "
                                                             "Refresh; never retry by toggling."
                                                           : "Request failed; refresh authoritative state.";
                    } else if (action == Action::Query) {
                        if (error_.value("phase") == "query")
                            error_.clear();
                        accept(r);
                    } else {
                        if (error_.value("phase") == (action == Action::Apply ? "apply" : "validate"))
                            error_.clear();
                        message_ = "Daemon Job submitted; request acknowledgement is not completion.";
                    }
                }
                emit changed();
                if (std::exchange(refreshAgain_, false) || action != Action::Query)
                    refresh();
            });
    auto client = client_;
    auto guard = epoch_;
    const auto trackedJob = job_.id;
    request_.setFuture(QtConcurrent::run([client, guard, epoch, project, before, desired, action,
                                          trackedJob] {
        Reply r;
        try {
            const auto first = client->snapshot();
            if (action == Action::Query && !trackedJob.isEmpty()) {
                require(q(first.project_path()) == project, "Project changed during Job observation");
                for (const auto &job : first.jobs()) {
                    if (q(job.id()) == trackedJob && (job.state() == "Queued" || job.state() == "Running")) {
                        // Discovery is replaced by this Job. Observe status without composing
                        // two role queries across that transition. Query only after terminal
                        // status, then independently require physical-pair retirement.
                        r.snapshot = first;
                        r.jobSnapshotOnly = true;
                        return r;
                    }
                }
            }
            r.info = client->query();
            if (action == Action::Query) {
                if (r.info.supported() && topology(r.info)) {
                    r.active = client->active(r.info.parent_id());
                    require(!r.active || (literal(r.active->artifact().id()) &&
                                          r.active->logical_device_id() == r.info.parent_id()),
                            "Active rig identity/parent is incomplete or mismatched; absence is unconfirmed");
                }
                r.snapshot = client->snapshot();
                const auto final = client->query();
                require(first.project_path() == r.snapshot.project_path() &&
                            !r.snapshot.project_path().empty(),
                        "Project changed during role query");
                require(same(r.info, final), "Discovery changed during role query; refresh required");
                r.info = final;
            } else {
                require(guard->load() == epoch, "UI context changed before submission");
                require(!project.isEmpty() && q(first.project_path()) == project && same(before, r.info) &&
                            !r.info.busy() && !r.info.blocked() && advertised(first, before.parent_id()),
                        "Project, discovery or generation changed before submission");
                w::X1RoleRequest request;
                request.set_schema_version(1);
                request.set_expected_generation(before.generation());
                request.set_parent_id(before.parent_id());
                request.set_discovery_token(before.discovery_token());
                request.set_swapped(desired);
                r.id = q(action == Action::Apply ? client->apply(request) : client->validate(request));
                require(literal(r.id.toStdString()), "Daemon omitted a usable Job identity");
            }
        } catch (const mantis::Failure &e) {
            r.error = failure(e.error);
        } catch (const std::exception &e) {
            r.error = failure({mantis::Status::io, e.what(), "studio.x1-roles"});
        }
        return r;
    }));
    emit changed();
}
void X1RoleController::accept(const Reply &r) {
    if (r.jobSnapshotOnly) {
        snapshot_ = r.snapshot;
        for (const auto &job : r.snapshot.jobs()) {
            if (q(job.id()) == job_.id) {
                job_.display = {{"id", job_.id},
                                {"state", q(job.state()).left(128)},
                                {"progress", job.progress()},
                                {"status", q(job.status()).left(4096)},
                                {"diagnostics", q(job.diagnostics()).left(4096)}};
                break;
            }
        }
        return;
    }
    const auto project = q(r.snapshot.project_path());
    const auto rig = r.active ? q(r.active->artifact().id()) : QString{};
    const bool unchanged = same(info_, r.info) && project_ == project && active_ == rig;
    if (!unchanged) {
        confirmation_.clear();
        if (job_.id.isEmpty())
            invalidate();
    }
    if (job_.id.isEmpty() && (!fresh_ || !same(info_, r.info)))
        proposed_ = r.info.swapped();
    project_ = project;
    snapshot_ = r.snapshot;
    info_ = r.info;
    active_ = rig;
    fresh_ = true;
    if (job_.id.isEmpty())
        return;
    const w::Job *observed = nullptr;
    for (const auto &j : r.snapshot.jobs())
        if (q(j.id()) == job_.id) {
            observed = &j;
            break;
        }
    if (!observed) {
        job_.display = {{"state", "Status unavailable; completion unconfirmed"}};
        return;
    }
    job_.display = {{"id", job_.id},
                    {"state", q(observed->state()).left(128)},
                    {"progress", observed->progress()},
                    {"status", q(observed->status()).left(4096)},
                    {"diagnostics", q(observed->diagnostics()).left(4096)}};
    const auto &s = observed->state();
    if (s != "Completed" && s != "Failed" && s != "Cancelled")
        return;
    if (r.info.busy())
        return; // Terminal status alone does not prove reservation retirement.
    const bool context = job_.project == project_ && job_.epoch == epoch_->load();
    const bool owned = q(r.info.job_id()) == job_.id;
    const bool validated = s == "Completed" && context && owned && !cancelRequested_ &&
                           job_.phase == "Validate" && same(job_.before, r.info, false) && active_.isEmpty();
    const bool applied = s == "Completed" && context && owned && job_.phase == "Apply" &&
                         appliedMapping(job_.before, r.info, job_.desired);
    const auto display = job_.display;
    const auto phase = job_.phase;
    job_.id.clear();
    if (validated) {
        confirmation_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
        proposed_ = job_.desired;
        message_ = "Proposal validated by the daemon. Review exact identities and confirm Apply.";
    } else {
        invalidate();
        proposed_ = r.info.swapped();
        message_ = applied ? "Roles applied and authoritative assignment refreshed. Future captures use this "
                             "mapping; historical captures are unchanged."
                   : s == "Completed"
                       ? "Job completed. Context or discovery changed; refresh and validate a new proposal."
                       : "Daemon Job " + q(s) + ": " + display.value("status").toString() + " " +
                             display.value("diagnostics").toString();
    }
    job_.display = display;
    job_.phase = phase;
    if (applied)
        emit this->applied(q(info_.parent_id()));
}
void X1RoleController::cancelJob() {
    if (!state().value("canCancel").toBool())
        return;
    invalidate();
    cancelling_ = true;
    cancelRequested_ = true;
    const auto id = job_.id;
    auto client = client_;
    const auto epoch = epoch_->load();
    disconnect(&cancellation_, nullptr, this, nullptr);
    connect(&cancellation_, &QFutureWatcher<Reply>::finished, this, [this, id, epoch] {
        cancelling_ = false;
        const auto r = cancellation_.result();
        if (epoch == epoch_->load() && id == job_.id) {
            if (!r.error.isEmpty()) {
                error_ = r.error;
                error_["phase"] = "cancel";
            } else
                message_ = "Cancellation requested; waiting for the daemon Job and ownership retirement. A "
                           "committed change may still complete.";
        }
        emit changed();
        refresh();
    });
    cancellation_.setFuture(QtConcurrent::run([client, id] {
        Reply r;
        try {
            client->cancel(id.toStdString());
        } catch (const mantis::Failure &e) {
            r.error = failure(e.error);
        } catch (const std::exception &e) {
            r.error = failure({mantis::Status::io, e.what(), "studio.x1-roles"});
        }
        return r;
    }));
    emit changed();
}
