#include "devices_model.hpp"
#include "calibration_controller.hpp"
#include <QCryptographicHash>
#include <QSet>
#include <algorithm>

namespace {
constexpr int nodeLimit = 512, depthLimit = 8, edgeLimit = 64, capLimit = 32, metadataLimit = 32;
// Display sanitation never changes an actionable canonical identity.
QString display(const std::string &s, int limit, bool &limited) {
    const auto bytes = std::min(s.size(), static_cast<size_t>(limit * 4));
    auto text = QString::fromUtf8(s.data(), static_cast<qsizetype>(bytes));
    if (s.size() > bytes || text.size() > limit) {
        limited = true;
        text = text.left(limit) + QChar(0x2026);
    }
    for (qsizetype i = 0; i < text.size(); ++i) {
        auto c = text[i];
        if (c.category() == QChar::Other_Control || c.category() == QChar::Other_Format ||
            c == QChar::ReplacementCharacter) {
            text[i] = QChar(0xfffd);
            limited = true;
        }
    }
    return text;
}
QString identity(const std::string &s) {
    if (s.empty() || s.size() > 1024)
        return {};
    const auto text = QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size()));
    if (text.toUtf8() != QByteArray(s.data(), static_cast<qsizetype>(s.size())))
        return {};
    for (auto c : text)
        if (c.category() == QChar::Other_Control || c.category() == QChar::Other_Format ||
            c == QChar::ReplacementCharacter)
            return {};
    return text;
}
QString group(const QStringList &caps, bool child) {
    if (child)
        return "Components"; // Rendered beneath its verified parent, not promoted to a scanner.
    if (caps.contains("org.mantis.camera.frameset-stream.v1"))
        return "Acquisition parents";
    if (caps.contains("org.mantis.camera.image-stream.v1"))
        return "Imaging devices";
    if (caps.contains("org.mantis.emitter.power-control.v1") ||
        caps.contains("org.mantis.emitter.state-feedback.v1") ||
        caps.contains("org.mantis.trigger.hardware.v1"))
        return "Emitters & timing";
    return "Other devices";
}
} // namespace
DevicesModel::DevicesModel(QObject *parent) : QObject(parent) {
    present();
}
void DevicesModel::setBridge(QObject *value) {
    if (value == bridge_)
        return;
    if (bridge_)
        disconnect(bridge_, nullptr, this, nullptr);
    bridge_ = value;
    snapshot_.clear();
    fingerprint_.clear();
    project_.clear();
    selected_.clear();
    seen_ = false;
    if (auto *bridge = value) {
        Q_ASSERT(bridge->thread() == thread());
        if (bridge->metaObject()->indexOfSignal("snapshotReady(mantis::wire::v1::Response)") >= 0)
            connect(bridge, SIGNAL(snapshotReady(mantis::wire::v1::Response)), this,
                    SLOT(observeSnapshot(mantis::wire::v1::Response)));
        if (bridge->metaObject()->indexOfSignal("changed()") >= 0)
            connect(bridge, SIGNAL(changed()), this, SLOT(refresh()));
        connect(bridge, &QObject::destroyed, this, [this] {
            snapshot_.clear();
            selected_.clear();
            seen_ = false;
            present();
            emit graphChanged();
            emit bridgeChanged();
        });
    }
    present();
    emit graphChanged();
    emit bridgeChanged();
}
void DevicesModel::observeSnapshot(const mantis::wire::v1::Response &s) {
    using Row = QVariantMap;
    // A bounded field digest bypasses UTF-8/DTO/graph conversion for identical evidence.
    // Length-prefix every field, without serializing the protobuf or copying frame data.
    QCryptographicHash digest(QCryptographicHash::Sha256);
    auto number = [&](quint64 n) { digest.addData(QByteArray::number(n) + ':'); };
    auto field = [&](const std::string &value, size_t bytes) {
        number(value.size());
        digest.addData(QByteArrayView(value.data(), static_cast<qsizetype>(std::min(value.size(), bytes))));
    };
    number(s.devices_size());
    for (int i = 0; i < std::min(s.devices_size(), nodeLimit); ++i) {
        const auto &d = s.devices(i);
        field(d.id(), 1024);
        field(d.name(), 512);
        field(d.plugin_id(), 1024);
        field(d.parent(), 1024);
        number(d.children_size());
        for (int j = 0; j < std::min(d.children_size(), edgeLimit); ++j)
            field(d.children(j), 1024);
        number(d.capabilities_size());
        for (int j = 0; j < std::min(d.capabilities_size(), capLimit); ++j)
            field(d.capabilities(j), 768);
        number(d.metadata_size());
        if (d.metadata_size() <= 128) {
            // Hash order is immaterial: normalized equality still suppresses notifications
            // when map iteration differs between otherwise equivalent responses.
            for (const auto &[key, value] : d.metadata()) {
                field(key, 512);
                field(value, 2048);
            }
        }
    }
    number(s.plugins_size());
    for (int i = 0; i < std::min(s.plugins_size(), 128); ++i) {
        const auto &p = s.plugins(i);
        field(p.id(), 1024);
        field(p.state(), 256);
        field(p.diagnostic(), 2048);
        field(p.version(), 512);
        field(p.kind(), 256);
        field(p.execution(), 256);
    }
    number(s.events_size());
    for (int i = std::max(0, s.events_size() - 12); i < s.events_size(); ++i) {
        const auto &e = s.events(i);
        number(e.sequence());
        field(e.kind(), 256);
        field(e.component(), 512);
        field(e.message(), 2048);
    }
    field(s.project_path(), s.project_path().size()); // Public client bounds control responses to 4 MiB.
    const auto fingerprint = digest.result();
    if (seen_ && fingerprint_ == fingerprint) {
        present();
        return;
    }
    fingerprint_ = fingerprint;
    bool limited = s.devices_size() > nodeLimit;
    QStringList anomalies;
    auto warn = [&](const QString &message) {
        if (anomalies.size() < 16 && !anomalies.contains(message))
            anomalies.push_back(message);
    };
    if (limited)
        warn("Device graph truncated to 512 descriptors; calibration navigation unavailable.");
    const int count = std::min(s.devices_size(), nodeLimit);
    QVector<Row> nodes;
    QHash<QString, int> indices;
    QSet<QString> duplicates;
    QVector<QStringList> declared;
    QVector<QString> parents;
    for (int i = 0; i < count; ++i) {
        const auto &d = s.devices(i);
        const auto id = identity(d.id());
        if (id.isEmpty())
            warn("Missing, oversized or unsafe device identity; descriptor cannot be selected.");
        else if (indices.contains(id)) {
            duplicates.insert(id);
            warn("Duplicate device identity; all descriptors with that ID are non-selectable.");
        } else
            indices.insert(id, i);
        Row row{{"id", id},
                {"displayId", display(d.id(), 192, limited)},
                {"name", display(d.name(), 128, limited)},
                {"source", "live"},
                {"pluginId", identity(d.plugin_id())},
                {"plugin", display(d.plugin_id(), 192, limited)},
                {"parent", display(d.parent(), 192, limited)}};
        if (row["name"].toString().isEmpty())
            row["name"] = "Unnamed device";
        parents.push_back(identity(d.parent()));
        if (!d.parent().empty() && parents.back().isEmpty())
            warn("Invalid parent reference.");
        QStringList children;
        if (d.children_size() > edgeLimit) {
            limited = true;
            warn("Child references truncated to 64 per descriptor.");
        }
        for (int j = 0; j < std::min(d.children_size(), edgeLimit); ++j) {
            const auto child = identity(d.children(j));
            if (child.isEmpty() || children.contains(child))
                warn("Invalid or duplicate child reference.");
            else
                children.push_back(child);
        }
        declared.push_back(children);
        row["children"] = children;
        QStringList caps;
        if (d.capabilities_size() > capLimit)
            limited = true;
        for (int j = 0; j < std::min(d.capabilities_size(), capLimit); ++j) {
            const auto cap = display(d.capabilities(j), 192, limited);
            if (!caps.contains(cap))
                caps.push_back(cap);
        }
        caps.sort();
        row["capabilities"] = caps;
        row["group"] = group(caps, !d.parent().empty());
        // Inspect at most 128 map entries, sort literal keys, render at most 32.
        QMap<QString, QString> metadata;
        int inspected{};
        if (d.metadata_size() > metadataLimit)
            limited = true;
        if (d.metadata_size() > 128)
            warn("Metadata exceeding 128 entries omitted; no arbitrary subset is treated as complete.");
        for (const auto &[key, value] : d.metadata()) {
            if (d.metadata_size() > 128 || inspected++ == 128)
                break;
            const auto k = display(key, 128, limited);
            const auto v = display(value, 512, limited);
            if (metadata.contains(k))
                warn("Metadata display keys collide after sanitation; values remain untrusted.");
            metadata.insert(k, v);
        }
        QVariantList values;
        for (auto it = metadata.begin(); it != metadata.end() && values.size() < metadataLimit; ++it)
            values.push_back(Row{{"key", it.key()}, {"value", it.value()}});
        row["metadataCount"] = d.metadata_size();
        row["metadata"] = values;
        row["metadataOmitted"] = d.metadata_size() > 128;
        nodes.push_back(row);
    }
    // Resolve only exact, unique references. Parent is authoritative for placement;
    // a nonempty children list must agree. Missing reciprocal declarations are reported.
    QVector<int> parentIndex(count, -1);
    for (int i = 0; i < count; ++i) {
        auto &row = nodes[i];
        const auto id = row["id"].toString();
        row["selectable"] = !id.isEmpty() && !duplicates.contains(id);
        const auto parent = parents[i];
        if (!parent.isEmpty()) {
            if (!indices.contains(parent) || duplicates.contains(parent) || parent == id) {
                warn("Orphan, ambiguous or self parent reference; shown in Graph anomalies.");
                row["group"] = "Graph anomalies";
            } else {
                parentIndex[i] = indices[parent];
                if (!declared[parentIndex[i]].contains(id))
                    warn(
                        "Parent/children declarations disagree; parent association retained for inspection.");
            }
        }
        for (const auto &child : declared[i]) {
            if (!indices.contains(child) || duplicates.contains(child) || child == id ||
                parents[indices.value(child, 0)] != id)
                warn("Contradictory, missing or self child reference.");
        }
    }
    QVector<int> depths(count, 0);
    for (int i = 0; i < count; ++i) {
        QSet<int> visited{i};
        int p = parentIndex[i];
        while (p >= 0) {
            if (visited.contains(p) || depths[i] >= depthLimit) {
                warn("Cycle or hierarchy deeper than 8; affected node shown in Graph anomalies.");
                parentIndex[i] = -1;
                nodes[i]["group"] = "Graph anomalies";
                depths[i] = 0;
                break;
            }
            visited.insert(p);
            ++depths[i];
            p = parentIndex[p];
        }
    }
    if (limited)
        warn("Some descriptor text, capabilities or metadata is sanitized/truncated; bounded inspection "
             "only.");
    const bool graphTrusted = !limited && anomalies.empty();
    // Reuse exactly the controller's eligibility contract, without a second QML rule.
    for (int i = 0; i < count; ++i) {
        auto &row = nodes[i];
        row["eligible"] = graphTrusted && row["selectable"].toBool() &&
                          CalibrationController::isCalibrationCandidate(s.devices(i), s);
        row["depth"] = depths[i];
        Row plugin{{"state", "Unknown"},
                   {"diagnostic", "No exact published plugin match"},
                   {"version", "Not reported"},
                   {"kind", "Not reported"},
                   {"execution", "Not reported"}};
        int matches{};
        for (int j = 0; j < std::min(s.plugins_size(), 128); ++j) {
            const auto &p = s.plugins(j);
            if (!row["pluginId"].toString().isEmpty() && identity(p.id()) == row["pluginId"].toString()) {
                ++matches;
                bool pluginLimited{};
                plugin = {{"state", display(p.state(), 64, pluginLimited)},
                          {"diagnostic", display(p.diagnostic(), 512, pluginLimited)},
                          {"version", display(p.version(), 128, pluginLimited)},
                          {"kind", display(p.kind(), 64, pluginLimited)},
                          {"execution", display(p.execution(), 64, pluginLimited)},
                          {"limited", pluginLimited}};
                for (const auto &key : {"state", "version", "kind", "execution"})
                    if (plugin[key].toString().isEmpty())
                        plugin[key] = "Not reported";
            }
        }
        if (matches != 1 || s.plugins_size() > 128)
            plugin = {{"state", "Unknown"}, {"diagnostic", "No unique bounded exact plugin match"}};
        row["pluginInfo"] = plugin;
    }
    // Iterative depth-first ordering; every descriptor renders once, including anomalous nodes.
    QVariantList ordered;
    QVector<QVector<int>> childIndices(count);
    for (int i = 0; i < count; ++i)
        if (parentIndex[i] >= 0)
            childIndices[parentIndex[i]].push_back(i);
    QVector<int> stack;
    auto appendTree = [&](int start) {
        stack.push_back(start);
        while (!stack.empty()) {
            const int i = stack.takeLast();
            auto row = nodes[i];
            int ancestor = i;
            for (int j = 0; j < depthLimit && parentIndex[ancestor] >= 0; ++j)
                ancestor = parentIndex[ancestor];
            row["group"] = nodes[ancestor]["group"];
            ordered.push_back(row);
            for (auto it = childIndices[i].rbegin(); it != childIndices[i].rend(); ++it)
                stack.push_back(*it);
        }
    };
    const QStringList groups{"Acquisition parents", "Imaging devices", "Emitters & timing",
                             "Other devices",       "Graph anomalies", "Components"};
    for (const auto &category : groups)
        for (int i = 0; i < count; ++i)
            if (parentIndex[i] < 0 && nodes[i]["group"] == category)
                appendTree(i);
    QVariantList events;
    for (int i = std::max(0, s.events_size() - 12); i < s.events_size(); ++i) {
        const auto &e = s.events(i);
        bool eventLimited{};
        events.push_back(Row{{"sequence", QString::number(e.sequence())},
                             {"kind", display(e.kind(), 64, eventLimited)},
                             {"component", display(e.component(), 128, eventLimited)},
                             {"message", display(e.message(), 512, eventLimited)},
                             {"limited", eventLimited}});
    }
    const auto &path = s.project_path();
    const auto project = QString::fromLatin1(
        QCryptographicHash::hash(QByteArrayView(path.data(), static_cast<qsizetype>(path.size())),
                                 QCryptographicHash::Sha256)
            .toHex());
    if (seen_ && project != project_)
        selected_.clear();
    project_ = project;
    seen_ = true;
    const bool graphChangedValue = this->nodes() != ordered;
    snapshot_ = {{"nodes", ordered},
                 {"count", s.devices_size()},
                 {"shown", count},
                 {"anomalies", anomalies},
                 {"limited", limited},
                 {"graphTrusted", graphTrusted},
                 {"events", events},
                 {"eventCount", s.events_size()},
                 {"eventsLimited", s.events_size() > 12},
                 {"pluginsLimited", s.plugins_size() > 128}};
    if (!selected_.isEmpty() && std::none_of(ordered.begin(), ordered.end(), [&](const auto &v) {
            const auto row = v.toMap();
            return row["selectable"].toBool() && row["id"] == selected_;
        }))
        selected_.clear(); // Explicit no-selection; never silently switch device identity.
    present();
    if (graphChangedValue)
        emit graphChanged();
}
void DevicesModel::refresh() {
    present();
}
void DevicesModel::selectDevice(const QString &id) {
    if (id == selected_)
        return;
    const auto nodes = snapshot_.value("nodes").toList();
    if (!id.isEmpty() && std::none_of(nodes.begin(), nodes.end(), [&](const auto &v) {
            const auto row = v.toMap();
            return row["selectable"].toBool() && row["id"] == id;
        }))
        return;
    selected_ = id;
    present();
}
QString DevicesModel::calibrationIdentity() const {
    // Bridge state is consulted now, not solely the presentation's cached enabled flag.
    if (!bridge_ || bridge_->property("connected") != QVariant(true))
        return {};
    return data_.value("selected").toMap().value("canCalibrate").toBool() ? selected_ : QString{};
}
void DevicesModel::present() {
    const bool confirmed = seen_ && bridge_ && bridge_->property("connected") == QVariant(true);
    auto next = snapshot_;
    next["source"] = "live";
    next["confirmed"] = confirmed;
    next["hasSnapshot"] = seen_;
    next["freshness"] = confirmed ? "Current confirmed snapshot"
                        : seen_   ? "Last known / stale · non-actionable"
                                  : "Waiting for a confirmed device snapshot";
    next["selectedId"] = selected_;
    QVariantMap selected;
    const auto nodes = snapshot_.value("nodes").toList();
    for (const auto &v : nodes)
        if (v.toMap().value("id") == selected_ && v.toMap().value("selectable").toBool())
            selected = v.toMap();
    selected["canCalibrate"] = confirmed && selected.value("eligible").toBool();
    selected["calibrationReason"] =
        !seen_                ? "Wait for a confirmed snapshot."
        : !confirmed          ? "Stale evidence cannot authorize device-bound calibration."
        : selected_.isEmpty() ? "Select an advertised logical device."
        : !snapshot_.value("graphTrusted").toBool()
            ? "Resolve graph anomalies / truncation before device-bound navigation."
        : !selected.value("eligible").toBool()
            ? "Requires a FrameSet parent with an image-stream child. Components are not calibration parents."
            : "Opens the existing seven stages; no capture or activation starts.";
    next["selected"] = selected;
    QVariantList issues;
    const auto input = bridge_ ? bridge_->property("errorDetails").toList() : QVariantList{};
    for (qsizetype i = 0; i < std::min(input.size(), qsizetype{8}); ++i) {
        const auto issue = input[i].toMap();
        QVariantMap out;
        for (const auto &key : {"phase", "kind", "component", "message"}) {
            bool bounded{};
            out[key] = display(issue.value(key).toString().left(2048).toStdString(), 512, bounded);
        }
        out["code"] = issue.value("code");
        issues.push_back(out);
    }
    next["issues"] = issues;
    if (next != data_) {
        data_ = std::move(next);
        emit changed();
    }
}
