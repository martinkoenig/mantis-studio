#include "bridge.hpp"
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <iostream>
#include <mantis/image_layout.hpp>
StudioBridge::StudioBridge(QObject *parent, bool runtimeEnabled)
    : QObject(parent), runtime_enabled_(runtimeEnabled),
      client_(runtimeEnabled ? mantis::client::Endpoint::environment() : mantis::client::Endpoint{}) {
    connect(&watcher_, &QFutureWatcher<StudioResult>::finished, this, [this] {
        const auto result = watcher_.result();
        request_pending_ = false;
        applyResult(result);
    });
    connect(&preview_watcher_, &QFutureWatcher<PreviewResult>::finished, this, [this] {
        auto result = preview_watcher_.result();
        if (!connected_)
            return; // A late preview cannot confirm activity after a snapshot failure.
        if (!result.left.isNull() && !result.right.isNull()) {
            dual_preview_ = true;
            if (left_)
                left_->setImage(std::move(result.left));
            if (right_)
                right_->setImage(std::move(result.right));
            emit changed();
            if (!preview_reported_ && left_ && right_ && left_->ready() && right_->ready()) {
                std::cout << "STUDIO_DUAL_PREVIEW_READY" << std::endl;
                preview_reported_ = true;
            }
        }
    });
    preview_timer_.setInterval(66);
    connect(&preview_timer_, &QTimer::timeout, this, &StudioBridge::refreshPreview);
    if (runtime_enabled_)
        preview_timer_.start();
    timer_.setInterval(500);
    connect(&timer_, &QTimer::timeout, this, &StudioBridge::refresh);
    if (runtime_enabled_) {
        timer_.start();
        QTimer::singleShot(0, this, &StudioBridge::refresh);
    }
}
void StudioBridge::applyResult(const StudioResult &result) {
    // A snapshot confirms usable runtime access. Failure codes alone cannot do so:
    // the client uses the same Failure type for server, socket and mapped-file errors.
    connected_ = result.snapshot.has_value();
    error_details_.removeIf([&](const auto &value) {
        const auto phase = value.toMap().value("phase").toString();
        return phase == "snapshot" || (phase == "operation" && result.operationAttempted) ||
               (phase == "artifact" && result.artifactAttempted);
    });
    for (const auto &issue : result.issues) {
        error_details_.push_back(QVariantMap{
            {"phase", QString::fromStdString(issue.phase)},
            {"message", QString::fromStdString(issue.message)},
            {"kind", issue.cause ? "failure" : "exception"},
            {"code", issue.cause ? QVariant(static_cast<int>(issue.cause->code)) : QVariant{}},
            {"component", issue.cause ? QString::fromStdString(issue.cause->component) : QString{}}});
    }
    QStringList messages;
    for (const auto &value : error_details_) {
        const auto issue = value.toMap();
        messages.push_back(
            QString("%1 [%2 / %3]: %4")
                .arg(issue.value("phase").toString(), issue.value("component").toString(),
                     issue.value("code").isValid() ? issue.value("code").toString() : "untyped",
                     issue.value("message").toString()));
    }
    error_ = messages.join(" · ");
    if (!result.snapshot) {
        emit changed();
        return; // Keep the authoritative last-known state; send no runtime command.
    }
    has_snapshot_ = true;
    const auto &snapshot = *result.snapshot;
    emit snapshotReady(snapshot);
    if (!result.newest_id.empty())
        newest_ = QString::fromStdString(result.newest_id);
    devices_.clear();
    artifacts_.clear();
    jobs_.clear();
    plugins_.clear();
    diagnostics_.clear();
    capture_.clear();
    acquisition_text_.clear();
    project_ = QString::fromStdString(snapshot.project_path());
    auto s = [](const std::string &v) { return QString::fromStdString(v); };
    for (auto &d : snapshot.devices())
        if (d.parent().empty()) {
            QStringList capabilities;
            for (const auto &capability : d.capabilities())
                capabilities.push_back(s(capability));
            devices_.push_back(QVariantMap{
                {"id", s(d.id())},
                {"name", s(d.name())},
                {"plugin", s(d.plugin_id())},
                {"capabilities", capabilities},
                {"captureSupported", capabilities.contains("org.mantis.camera.image-stream.v1") ||
                                         capabilities.contains("org.mantis.camera.frameset-stream.v1")}});
        }
    for (auto &c : snapshot.captures()) {
        if (c.active())
            capture_ = s(c.id());
        if (c.diagnostics().contains("left.identity")) {
            auto value = [&](const std::string &key) {
                auto it = c.diagnostics().find(key);
                return it == c.diagnostics().end() ? QString("unavailable") : s(it->second);
            };
            acquisition_text_ =
                QString("LEFT %1 × %2 · %3\n%4 FPS · %5 sequence gaps\nRIGHT %6 × %7 · %8\n%9 FPS · %10 "
                        "sequence gaps\n%11 · %12\nPaired V4L2 Δt: %13 ns\nHost arrival Δt: %14 ns\nRaw: %15 "
                        "MB/s · %16 MiB/s\nQueue %17 / %18 · high %19\nObserved raw loss: %20 · preview "
                        "drops: %21\n%22\nNative L/R: %23 / %24 · offset L−R: %25\nStartup unmatched L/R: "
                        "%26 / %27\nPairing failures: %28 · exposure skew: %29");
            for (const auto &text : QStringList{value("left.width"),
                                                value("left.height"),
                                                value("left.fourcc"),
                                                value("left.receive_fps"),
                                                value("left.sequence_gaps"),
                                                value("right.width"),
                                                value("right.height"),
                                                value("right.fourcc"),
                                                value("right.receive_fps"),
                                                value("right.sequence_gaps"),
                                                value("sync_configuration"),
                                                value("pairing_mode"),
                                                value("paired_v4l2_delta_ns"),
                                                value("host_arrival_delta_ns"),
                                                QString::number(c.writer_mb_s(), 'f', 1),
                                                QString::number(c.writer_mib_s(), 'f', 1),
                                                QString::number(c.queue_depth()),
                                                QString::number(c.queue_capacity()),
                                                QString::number(c.queue_high_water()),
                                                QString::number(c.dropped()),
                                                QString::number(c.preview_drops()),
                                                c.error().empty() ? value("buffer_mode") : s(c.error()),
                                                value("left.native_sequence"),
                                                value("right.native_sequence"),
                                                value("native_sequence_offset"),
                                                value("startup_unmatched_left"),
                                                value("startup_unmatched_right"),
                                                value("pairing_failures"),
                                                value("exposure_skew")})
                acquisition_text_ = acquisition_text_.arg(text);
        }
    }
    for (auto &a : snapshot.artifacts())
        artifacts_.push_back(QVariantMap{{"id", s(a.id())},
                                         {"type", s(a.type())},
                                         {"state", s(a.state())},
                                         {"chunks", QVariant::fromValue(a.chunks())}});
    for (auto &j : snapshot.jobs())
        jobs_.push_back(QVariantMap{{"id", s(j.id())},
                                    {"name", s(j.name())},
                                    {"state", s(j.state())},
                                    {"progress", j.progress()},
                                    {"diagnostics", s(j.diagnostics())}});
    for (auto &p : snapshot.plugins())
        plugins_.push_back(QVariantMap{{"id", s(p.id())},
                                       {"state", s(p.state())},
                                       {"execution", s(p.execution())},
                                       {"diagnostic", s(p.diagnostic())}});
    for (auto &e : snapshot.events())
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
}

StudioBridge::~StudioBridge() {
    timer_.stop();
    preview_timer_.stop();
    preview_watcher_.waitForFinished();
    watcher_.waitForFinished();
}
void StudioBridge::attachView(QObject *object) {
    view_ = qobject_cast<mantis::render::PointCloudView *>(object);
}
void StudioBridge::execute(std::function<void(const mantis::client::Client &)> action) {
    if (!runtime_enabled_ || request_pending_)
        return;
    auto client = client_;
    auto selected = newest_.toStdString();
    request_pending_ = true; // The lease includes queued GUI completion, not just worker execution.
    watcher_.setFuture(QtConcurrent::run(
        [client, action, selected] { return collectResult(client, action, {}, selected); }));
    emit changed();
}
StudioResult StudioBridge::collectResult(const mantis::client::Client &client,
                                         const std::function<void(const mantis::client::Client &)> &action,
                                         std::optional<std::string> artifact,
                                         const std::string &displayedNewest) {
    StudioResult result;
    auto attempt = [&](const char *phase, auto work) {
        try {
            work();
            return true;
        } catch (const mantis::Failure &failure) {
            result.issues.push_back({phase, failure.error.message, failure.error});
        } catch (const std::exception &failure) {
            // Preserve untyped exceptions without inventing a transport status.
            result.issues.push_back({phase, failure.what(), std::nullopt});
        }
        return false;
    };
    result.operationAttempted = bool(action);
    if (action)
        attempt("operation", [&] { action(client); });
    // Confirm state after the operation, including a rejected or ambiguous outcome.
    // Never retry the operation: capture and job authority remain in mantisd.
    if (!attempt("snapshot", [&] { result.snapshot = client.snapshot(); }))
        return result;
    std::string newest;
    if (!artifact) {
        for (const auto &entry : result.snapshot->artifacts())
            if (entry.type() == mantis::schema::points.name && entry.state() == "FINALIZED")
                newest = entry.id();
        if (newest.empty() || newest == displayedNewest)
            return result;
        artifact = newest;
    }
    result.artifactAttempted = true;
    if (attempt("artifact", [&] { result.cloud = client.data(*artifact); })) {
        result.cloud_id = *artifact;
        result.newest_id = newest; // A failed load must not mark a cloud as already displayed.
    } else {
        // data() includes a control call and a local mapping. Its failure does not
        // identify which connection is usable. One fresh snapshot resolves that
        // uncertainty, preserving both root causes if confirmation also fails.
        result.snapshot.reset();
        attempt("snapshot", [&] { result.snapshot = client.snapshot(); });
    }
    return result;
}
void StudioBridge::refresh() {
    execute();
}
void StudioBridge::startCapture(QString id) {
    if (!connected_)
        return;
    const auto found = std::find_if(devices_.cbegin(), devices_.cend(), [&](const auto &device) {
        const auto descriptor = device.toMap();
        return descriptor.value("id") == id && descriptor.value("captureSupported").toBool();
    });
    if (found == devices_.cend())
        return;
    execute([id](const auto &client) { (void)client.start_capture({id.toStdString()}); });
}
void StudioBridge::stopCapture() {
    if (!connected_)
        return;
    auto id = capture_;
    execute([id](const auto &client) { client.stop_capture(id.toStdString()); });
}
void StudioBridge::runPipeline(QString recipe) {
    if (!connected_)
        return;
    auto id = capture_;
    execute([id, recipe](const auto &client) {
        (void)client.run_pipeline(id.toStdString(), recipe.toStdString());
    });
}
void StudioBridge::exportArtifact(QString path) {
    if (!connected_)
        return;
    auto id = selected_;
    execute([id, path](const auto &client) {
        (void)client.export_artifact(id.toStdString(), path.toStdString());
    });
}
void StudioBridge::cancelJob(QString id) {
    if (!connected_)
        return;
    execute([id](const auto &client) {
        mantis::wire::v1::Request r;
        r.mutable_job_cancel()->set_id(id.toStdString());
        (void)client.call(r);
    });
}
void StudioBridge::selectArtifact(QString id) {
    if (!runtime_enabled_ || !connected_ || request_pending_)
        return;
    auto client = client_;
    request_pending_ = true; // The lease includes queued GUI completion, not just worker execution.
    watcher_.setFuture(
        QtConcurrent::run([client, id] { return collectResult(client, {}, id.toStdString()); }));
    emit changed();
}
void StudioBridge::enablePlugin(QString id, bool enabled) {
    if (!connected_)
        return;
    execute([id, enabled](const auto &client) {
        mantis::wire::v1::Request r;
        r.mutable_plugin_enable()->set_id(id.toStdString());
        r.mutable_plugin_enable()->set_enabled(enabled);
        (void)client.call(r);
    });
}

void StudioBridge::attachPreview(QObject *left, QObject *right) {
    left_ = qobject_cast<MeasurementView *>(left);
    right_ = qobject_cast<MeasurementView *>(right);
}
void StudioBridge::refreshPreview() {
    if (preview_watcher_.isRunning() || !connected_)
        return;
    auto id = capture_.isEmpty() ? replay_ : capture_;
    if (id.isEmpty())
        return;
    auto client = client_;
    preview_watcher_.setFuture(QtConcurrent::run([client, id] {
        PreviewResult result;
        try {
            auto packet = client.preview(id.toStdString());
            if (!packet || packet->frames.size() != 2)
                return result;
            for (const auto &frame : packet->frames) {
                auto layout = mantis::data::image_layout(*frame);
                if (layout.width > 8192 || layout.height > 8192 || layout.row_stride > 65536)
                    throw std::runtime_error("Unsupported grayscale preview dimensions");
                QImage image(static_cast<int>(layout.width), static_cast<int>(layout.height),
                             QImage::Format_Grayscale8);
                if (image.isNull())
                    throw std::bad_alloc();
                image.fill(0); // deterministic Qt row padding; presentation conversion only
                for (uint32_t row = 0; row < layout.height; ++row)
                    mantis::data::grayscale_row(
                        *frame, layout, row,
                        {reinterpret_cast<std::byte *>(image.scanLine(static_cast<int>(row))), layout.width});
                auto role = frame->header.metadata.at("role");
                if (role == "left")
                    result.left = std::move(image);
                if (role == "right")
                    result.right = std::move(image);
            }
        } catch (const std::exception &e) {
            result.error = e.what();
        }
        return result;
    }));
}
void StudioBridge::replay(QString artifact, bool verify) {
    if (!connected_)
        return;
    execute([this, artifact, verify](const auto &client) {
        auto job = client.replay(artifact.toStdString(), !verify, verify);
        QMetaObject::invokeMethod(
            this, [this, job] { replay_ = QString::fromStdString(job); }, Qt::QueuedConnection);
    });
}
