#include "scan_model.hpp"
#include <QSet>
#include <algorithm>

namespace {
constexpr qsizetype inspectLimit = 256, rowLimit = 12, identityLimit = 4096;
bool flag(const QVariant &v) {
    return v.metaType() == QMetaType::fromType<bool>() && v.toBool();
}
QString rawText(const QVariant &v) {
    return v.metaType() == QMetaType::fromType<QString>() ? v.toString() : QString{};
}
QString display(const QVariant &v, qsizetype bound = 192, const QString &fallback = "Unavailable") {
    const auto raw = rawText(v);
    if (raw.isEmpty())
        return fallback;
    QString result;
    const auto size = std::min(raw.size(), bound);
    result.reserve(size + 1);
    for (qsizetype i = 0; i < size; ++i)
        result += raw[i].category() == QChar::Other_Control || raw[i].category() == QChar::Other_Format
                      ? QChar(' ')
                      : raw[i];
    if (size < raw.size())
        result += QChar(0x2026);
    return result;
}
QVariantList list(const QVariant &v) {
    return v.metaType() == QMetaType::fromType<QVariantList>() ? v.toList() : QVariantList{};
}
bool captureCapable(const QVariant &v) {
    if (v.metaType() != QMetaType::fromType<QStringList>())
        return false;
    const auto caps = v.toStringList();
    for (qsizetype i = 0; i < std::min(caps.size(), qsizetype{64}); ++i)
        if (caps[i] == "org.mantis.camera.image-stream.v1" ||
            caps[i] == "org.mantis.camera.frameset-stream.v1")
            return true;
    return false;
}
QVariantList inventory(const QVariant &value, const QString &kind, bool confirmed) {
    QVariantList out;
    QSet<QString> seen;
    const auto input = list(value);
    for (qsizetype i = 0; i < std::min(input.size(), inspectLimit); ++i) {
        if (input[i].metaType() != QMetaType::fromType<QVariantMap>())
            continue;
        const auto row = input[i].toMap();
        const auto id = rawText(row.value("id"));
        // Never truncate/deduplicate an authority ID by a display prefix. Oversized IDs are omitted.
        if (id.isEmpty() || id.size() > identityLimit || seen.contains(id))
            continue;
        seen.insert(id);
        if (kind == "devices" && !captureCapable(row.value("capabilities")))
            continue;
        const auto type = rawText(row.value("type"));
        if (kind == "artifacts" && type != "org.mantis.RawCapture" && type != "org.mantis.PointCloud")
            continue;
        QVariantMap item{{"id", id},
                         {"label", display(id, 80)},
                         {"source", "live"},
                         {"actionable", false},
                         {"name", display(row.value("name"))},
                         {"state", display(row.value("state"), 64, "Unknown")}};
        if (kind == "devices") {
            item["state"] = confirmed ? "Descriptor discovered · readiness unknown"
                                      : "Last-known descriptor · availability unknown";
            item["detail"] = display(row.value("plugin"));
        } else if (kind == "artifacts") {
            item["type"] = type;
            item["name"] = type == "org.mantis.RawCapture" ? "RawCapture" : "PointCloud";
            item["detail"] = display(id, 80);
        } else {
            // Runtime-wide inventory has no typed association with the selected scan.
            // Qualify each retained state rather than relying on the page freshness banner.
            item["stateConfirmed"] = confirmed;
            if (!confirmed)
                item["state"] = "Last known: " + item["state"].toString();
            item["detail"] = display(row.value("diagnostics"), 512, {});
        }
        out.push_back(item);
        if (out.size() == rowLimit)
            break;
    }
    return out;
}
} // namespace
ScanModel::ScanModel(QObject *parent) : QObject(parent) {
    refresh();
}
void ScanModel::setBridge(QObject *value) {
    if (value == bridge_)
        return;
    if (bridge_)
        disconnect(bridge_, nullptr, this, nullptr);
    bridge_ = value;
    project_identity_.clear();
    ++project_epoch_; // Detach/rebind is also a presentation source boundary.
    if (bridge_) {
        if (bridge_->metaObject()->indexOfSignal("changed()") >= 0)
            connect(bridge_, SIGNAL(changed()), this, SLOT(refresh()));
        connect(bridge_, &QObject::destroyed, this, [this] {
            project_identity_.clear();
            ++project_epoch_;
            refresh();
            emit bridgeChanged();
        });
    }
    refresh();
    emit bridgeChanged();
}
void ScanModel::refresh() {
    const auto field = [this](const char *name) { return bridge_ ? bridge_->property(name) : QVariant{}; };
    const bool hasSnapshot = flag(field("hasSnapshot"));
    const bool confirmed = hasSnapshot && flag(field("connected"));
    auto identity = rawText(field("project"));
    const bool identityValid = identity.size() <= 65536;
    if (!identityValid)
        identity.clear();
    if (identity != project_identity_) {
        project_identity_ = identity;
        ++project_epoch_;
    }
    const auto capture = field("capturing");
    const bool captureKnown = confirmed && capture.metaType() == QMetaType::fromType<bool>();
    const bool active = captureKnown && capture.toBool();
    const bool lastActive = hasSnapshot && flag(field("lastKnownCapturing"));
    QVariantMap next{
        {"source", "live"},
        {"hasSnapshot", hasSnapshot},
        {"confirmed", confirmed},
        {"freshness", confirmed     ? "Current confirmed snapshot"
                      : hasSnapshot ? "Last-known · current state unconfirmed"
                                    : "Waiting for runtime snapshot"},
        {"project", display(identity, 512, "Project identity unavailable")},
        {"projectEpoch", project_epoch_},
        {"identityValid", identityValid},
        {"captureActive", active},
        {"lastKnownCaptureActive", lastActive},
        {"captureStatus", !captureKnown ? "Unknown"
                          : active      ? "Active"
                                        : "Idle"},
        {"captureStatusText", !captureKnown
                                  ? (lastActive ? "Last known: capture active · current state unknown"
                                                : "Current capture state unknown")
                                  : display(field("captureStatusText"), 192,
                                            active ? "Capture active in confirmed snapshot"
                                                   : "No active capture in confirmed snapshot")},
        {"busy", flag(field("busy"))},
        {"runtimeError", display(field("error"), 512, {})},
        {"canOpenClassicAcquisition", true},
        {"commandsAllowed", false},
        {"processingStatus", "Not reported"}, // No scan-specific processing contract.
        {"readiness", "Unknown · no readiness contract"},
        {"previewStatus", confirmed && flag(field("dualPreview")) ? "Available in Classic Acquisition"
                                                                  : "Not reported · Classic Acquisition"}};
    for (const auto &kind : {"devices", "artifacts", "jobs"}) {
        const auto input = field(kind);
        next[kind] = hasSnapshot && identityValid ? inventory(input, kind, confirmed) : QVariantList{};
        next[QString(kind) + "Limited"] =
            list(input).size() > inspectLimit || next[kind].toList().size() == rowLimit;
    }
    QString latest;
    const auto artifacts = list(field("artifacts"));
    // Only advertise "latest" if the complete source fits the inspection bound; preserve source order.
    if (hasSnapshot && identityValid && artifacts.size() <= inspectLimit)
        for (const auto &v : artifacts) {
            const auto row = v.toMap();
            const auto id = rawText(row.value("id"));
            if (!id.isEmpty() && id.size() <= identityLimit && row.value("type") == "org.mantis.PointCloud" &&
                row.value("state") == "FINALIZED")
                latest = id;
        }
    const auto selected = rawText(field("selectedArtifact"));
    bool advertised = false;
    for (qsizetype i = 0; i < std::min(artifacts.size(), inspectLimit); ++i) {
        const auto row = artifacts[i].toMap();
        if (row.value("id") == selected && row.value("type") == "org.mantis.PointCloud" &&
            row.value("state") == "FINALIZED")
            advertised = true;
    }
    next["selectedArtifact"] = hasSnapshot && identityValid && advertised && selected.size() <= identityLimit
                                   ? display(selected, 192, "None")
                                   : "None advertised";
    next["latestPointCloud"] = display(latest, 192, "None / unavailable");
    QVariantList issues;
    const auto errors = list(field("errorDetails"));
    for (qsizetype i = 0; i < std::min(errors.size(), qsizetype{8}); ++i) {
        if (errors[i].metaType() != QMetaType::fromType<QVariantMap>())
            continue;
        const auto e = errors[i].toMap();
        const auto code = e.value("code");
        issues.push_back(
            QVariantMap{{"phase", display(e.value("phase"), 64)},
                        {"kind", display(e.value("kind"), 64)},
                        {"code", code.metaType() == QMetaType::fromType<int>() ? code : QVariant{}},
                        {"component", display(e.value("component"))},
                        {"message", display(e.value("message"), 512)}});
    }
    next["issues"] = issues;
    QVariantList diagnostics;
    const auto events = list(field("diagnostics"));
    for (qsizetype i = 0; i < std::min(events.size(), rowLimit); ++i) {
        if (events[i].metaType() != QMetaType::fromType<QVariantMap>())
            continue;
        const auto e = events[i].toMap();
        diagnostics.push_back(QVariantMap{{"source", "live"},
                                          {"kind", display(e.value("kind"), 64)},
                                          {"component", display(e.value("component"))},
                                          {"message", display(e.value("message"), 512)}});
    }
    next["diagnostics"] = hasSnapshot ? diagnostics : QVariantList{};
    if (next != data_) {
        data_ = std::move(next);
        emit changed();
    }
}
