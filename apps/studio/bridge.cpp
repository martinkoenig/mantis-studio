#include "bridge.hpp"
#include <QtConcurrent/QtConcurrentRun>
StudioBridge::StudioBridge(QObject *parent) : QObject(parent) {
    connect(&watcher_, &QFutureWatcher<StudioResult>::finished, this, [this] {
        auto result = watcher_.result();
        if (!result.error.empty()) {
            error_ = QString::fromStdString(result.error);
            connected_ = false;
            emit changed();
            return;
        }
        connected_ = true;
        if (!result.newest_id.empty())
            newest_ = QString::fromStdString(result.newest_id);
        error_.clear();
        devices_.clear();
        artifacts_.clear();
        jobs_.clear();
        plugins_.clear();
        diagnostics_.clear();
        capture_.clear();
        project_ = QString::fromStdString(result.snapshot.project_path());
        auto s = [](const std::string &v) { return QString::fromStdString(v); };
        for (auto &d : result.snapshot.devices())
            devices_.push_back(
                QVariantMap{{"id", s(d.id())}, {"name", s(d.name())}, {"plugin", s(d.plugin_id())}});
        for (auto &c : result.snapshot.captures())
            if (c.active())
                capture_ = s(c.id());
        for (auto &a : result.snapshot.artifacts())
            artifacts_.push_back(QVariantMap{{"id", s(a.id())},
                                             {"type", s(a.type())},
                                             {"state", s(a.state())},
                                             {"chunks", QVariant::fromValue(a.chunks())}});
        for (auto &j : result.snapshot.jobs())
            jobs_.push_back(QVariantMap{{"id", s(j.id())},
                                        {"name", s(j.name())},
                                        {"state", s(j.state())},
                                        {"progress", j.progress()},
                                        {"diagnostics", s(j.diagnostics())}});
        for (auto &p : result.snapshot.plugins())
            plugins_.push_back(QVariantMap{{"id", s(p.id())},
                                           {"state", s(p.state())},
                                           {"execution", s(p.execution())},
                                           {"diagnostic", s(p.diagnostic())}});
        for (auto &e : result.snapshot.events())
            diagnostics_.push_back(QVariantMap{{"sequence", QVariant::fromValue(e.sequence())},
                                               {"kind", s(e.kind())},
                                               {"component", s(e.component())},
                                               {"message", s(e.message())}});
        if (result.cloud) {
            selected_ = s(result.cloud_id);
            if (view_)
                view_->setCloud(result.cloud);
        }
        emit changed();
    });
    timer_.setInterval(500);
    connect(&timer_, &QTimer::timeout, this, &StudioBridge::refresh);
    timer_.start();
    QTimer::singleShot(0, this, &StudioBridge::refresh);
}
StudioBridge::~StudioBridge() {
    timer_.stop();
    watcher_.waitForFinished();
}
void StudioBridge::attachView(QObject *object) {
    view_ = qobject_cast<mantis::render::PointCloudView *>(object);
}
void StudioBridge::execute(std::function<void(const mantis::client::Client &)> action) {
    if (watcher_.isRunning())
        return;
    auto client = client_;
    auto selected = newest_.toStdString();
    watcher_.setFuture(QtConcurrent::run([client, action, selected] {
        StudioResult result;
        try {
            if (action)
                action(client);
            result.snapshot = client.snapshot();
            std::string newest;
            for (auto &a : result.snapshot.artifacts())
                if (a.type() == mantis::schema::points.name && a.state() == "FINALIZED")
                    newest = a.id();
            result.newest_id = newest;
            if (!newest.empty() && newest != selected) {
                result.cloud = client.data(newest);
                result.cloud_id = newest;
            }
        } catch (const std::exception &e) {
            result.error = e.what();
        }
        return result;
    }));
    emit changed();
}
void StudioBridge::refresh() {
    execute();
}
void StudioBridge::startCapture(QString id) {
    execute([id](const auto &client) { (void)client.start_capture({id.toStdString()}); });
}
void StudioBridge::stopCapture() {
    auto id = capture_;
    execute([id](const auto &client) { client.stop_capture(id.toStdString()); });
}
void StudioBridge::runPipeline(QString recipe) {
    auto id = capture_;
    execute([id, recipe](const auto &client) {
        (void)client.run_pipeline(id.toStdString(), recipe.toStdString());
    });
}
void StudioBridge::exportArtifact(QString path) {
    auto id = selected_;
    execute([id, path](const auto &client) {
        (void)client.export_artifact(id.toStdString(), path.toStdString());
    });
}
void StudioBridge::cancelJob(QString id) {
    execute([id](const auto &client) {
        mantis::wire::v1::Request r;
        r.mutable_job_cancel()->set_id(id.toStdString());
        (void)client.call(r);
    });
}
void StudioBridge::selectArtifact(QString id) {
    if (watcher_.isRunning())
        return;
    auto client = client_;
    watcher_.setFuture(QtConcurrent::run([client, id] {
        StudioResult result;
        try {
            result.snapshot = client.snapshot();
            result.cloud = client.data(id.toStdString());
            result.cloud_id = id.toStdString();
        } catch (const std::exception &e) {
            result.error = e.what();
        }
        return result;
    }));
}
void StudioBridge::enablePlugin(QString id, bool enabled) {
    execute([id, enabled](const auto &client) {
        mantis::wire::v1::Request r;
        r.mutable_plugin_enable()->set_id(id.toStdString());
        r.mutable_plugin_enable()->set_enabled(enabled);
        (void)client.call(r);
    });
}
