#include "home_model.hpp"
#include <QSet>
#include <algorithm>
#include <cmath>
#include <tuple>

namespace {
constexpr qsizetype inspectLimit = 256;
QString text(const QVariant &value, qsizetype limit = 192, const QString &fallback = "Unavailable") {
    if (value.metaType() != QMetaType::fromType<QString>() || value.toString().isEmpty())
        return fallback;
    const auto original = value.toString();
    QString out;
    const auto size = std::min(original.size(), limit);
    out.reserve(size + 1);
    for (qsizetype i = 0; i < size; ++i) {
        const auto c = original[i];
        // Runtime text cannot inject terminal controls or misleading bidi overrides.
        if (c.category() != QChar::Other_Control && c.category() != QChar::Other_Format)
            out += c;
        else
            out += ' ';
    }
    if (original.size() > limit)
        out += QChar(0x2026);
    return out;
}
QVariantList rows(const QVariant &value) {
    // Only the typed Studio bridge list contract is accepted.
    if (value.metaType() == QMetaType::fromType<QVariantList>())
        return value.toList(); // implicitly shared, not a payload copy
    return {};
}
bool numeric(const QVariant &v) {
    switch (v.metaType().id()) {
    case QMetaType::Long:
    case QMetaType::ULong:
    case QMetaType::Short:
    case QMetaType::UShort:
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
    case QMetaType::Double:
    case QMetaType::Float:
        return true;
    default:
        return false;
    }
}
QString count(const QVariant &v) {
    if (!numeric(v))
        return "Unavailable";
    const auto n = v.toDouble();
    if (!std::isfinite(n) || n <= 0 || std::floor(n) != n)
        return "Unavailable";
    return v.toString(); // retain exact uint64, never round via JavaScript double
}
QVariantList sample(const QVariantList &input, const QString &kind, qsizetype visible) {
    QVariantList result;
    QSet<QString> identities;
    for (qsizetype i = 0; i < std::min(input.size(), inspectLimit); ++i) {
        if (input[i].metaType() != QMetaType::fromType<QVariantMap>())
            continue;
        const auto row = input[i].toMap();
        const auto id = text(row.value("id"), 192, {});
        // Presentation IDs are never sent to commands; reject malformed/missing identities.
        if (kind != "events" && (id.isEmpty() || identities.contains(id)))
            continue;
        identities.insert(id);
        QVariantMap out{{"id", id}, {"source", "live"}};
        if (kind == "devices") {
            out["name"] = text(row.value("name"));
            out["plugin"] = text(row.value("plugin"));
            QStringList caps;
            bool capabilitiesAvailable = false;
            const auto value = row.value("capabilities");
            if (value.metaType() == QMetaType::fromType<QStringList>()) {
                capabilitiesAvailable = true;
                const auto list = value.toStringList();
                for (qsizetype j = 0; j < std::min(list.size(), qsizetype{8}); ++j)
                    caps.push_back(text(list[j]));
            } else {
                capabilitiesAvailable = value.metaType() == QMetaType::fromType<QVariantList>();
                const auto list = rows(value);
                for (qsizetype j = 0; j < std::min(list.size(), qsizetype{8}); ++j)
                    if (list[j].metaType() == QMetaType::fromType<QString>())
                        caps.push_back(text(list[j]));
            }
            out["detail"] = !capabilitiesAvailable ? QString("Capabilities unavailable")
                            : caps.empty()         ? QString("No capabilities advertised")
                                                   : caps.join(" · ");
            out["state"] = "Discovered · readiness unknown";
        } else if (kind == "jobs") {
            out["name"] = text(row.value("name"));
            out["state"] = text(row.value("state"), 64, "Unknown");
            out["detail"] = text(row.value("diagnostics"), 512, {});
            const auto p = row.value("progress");
            const bool valid =
                numeric(p) && std::isfinite(p.toDouble()) && p.toDouble() >= 0 && p.toDouble() <= 1;
            // Proto3 scalar progress has no presence bit. Zero could be an omitted
            // field: conservatively show unavailable, rather than invent 0%.
            const bool known = valid && p.toDouble() > 0;
            out["progressKnown"] = known;
            out["progress"] = known ? p.toDouble() : 0;
            out["progressText"] =
                known ? QString::number(p.toDouble() * 100, 'f', 0) + "%" : "Progress unavailable";
        } else if (kind == "artifacts") {
            out["name"] = text(row.value("type"));
            out["state"] = text(row.value("state"), 64, "Unknown");
            const auto chunks = count(row.value("chunks"));
            out["detail"] =
                (chunks == "Unavailable" ? QString("Chunks unavailable") : chunks + " chunks") + " · " + id;
        } else {
            out["sequence"] = count(row.value("sequence"));
            out["name"] = text(row.value("component"));
            out["state"] = text(row.value("kind"), 64, "Unknown");
            out["detail"] = text(row.value("message"), 512);
            out["id"] = out["sequence"];
        }
        result.push_back(out);
    }
    std::stable_sort(result.begin(), result.end(), [&](const QVariant &a, const QVariant &b) {
        const auto left = a.toMap(), right = b.toMap();
        if (kind == "events") {
            bool lok{}, rok{};
            const auto l = left.value("sequence").toString().toULongLong(&lok);
            const auto r = right.value("sequence").toString().toULongLong(&rok);
            if (lok != rok)
                return lok;
            if (lok && l != r)
                return l > r;
        }
        if (kind == "jobs") {
            auto active = [](const QVariantMap &m) {
                return m.value("state") == "Running" || m.value("state") == "Queued";
            };
            if (active(left) != active(right))
                return active(left);
        }
        return left.value("id").toString() < right.value("id").toString();
    });
    if (result.size() > visible)
        result = result.mid(0, visible);
    return result;
}
} // namespace
HomeModel::HomeModel(QObject *parent) : QObject(parent) {
    refresh();
}
void HomeModel::setBridge(QObject *value) {
    if (value == bridge_)
        return;
    if (bridge_)
        disconnect(bridge_, nullptr, this, nullptr);
    bridge_ = value;
    seenSnapshot_ = false;
    if (bridge_) {
        if (bridge_->metaObject()->indexOfSignal("changed()") >= 0)
            connect(bridge_, SIGNAL(changed()), this, SLOT(refresh()));
        connect(bridge_, &QObject::destroyed, this, [this] {
            seenSnapshot_ = false;
            refresh();
            emit bridgeChanged();
        });
    }
    refresh();
    emit bridgeChanged();
}
void HomeModel::refresh() {
    auto field = [&](const char *name) { return bridge_ ? bridge_->property(name) : QVariant{}; };
    const auto connection = field("connected");
    const bool confirmed = connection.metaType() == QMetaType::fromType<bool>() && connection.toBool();
    seenSnapshot_ = seenSnapshot_ || confirmed || field("hasSnapshot") == QVariant(true);
    QVariantMap next{{"source", "live"},
                     {"confirmed", confirmed},
                     {"hasSnapshot", seenSnapshot_},
                     {"freshness", confirmed                ? "Confirmed runtime snapshot"
                                   : seenSnapshot_          ? "Last known · current state unconfirmed"
                                   : field("busy").toBool() ? "Awaiting runtime confirmation"
                                                            : "Runtime state unconfirmed"},
                     {"project", text(field("project"), 4096, "Project path unavailable")}};
    const auto path = next.value("project").toString();
    next["projectName"] = path == "Project path unavailable" ? path : path.section('/', -1).section('\\', -1);
    for (const auto &[kind, property, limit] : {std::tuple{"devices", "devices", 3},
                                                {"jobs", "jobs", 4},
                                                {"artifacts", "artifacts", 6},
                                                {"events", "diagnostics", 4}}) {
        const auto value = field(property);
        const bool available = value.metaType() == QMetaType::fromType<QVariantList>();
        const auto input = rows(value);
        next[QString(kind) + "Available"] = available;
        next[kind] = sample(input, kind, limit);
        next[QString(kind) + "Count"] = available ? QVariant(input.size()) : QVariant{};
        next[QString(kind) + "Sampled"] = input.size() > inspectLimit;
        next[QString(kind) + "Summary"] =
            !available ? QString("Snapshot entries unavailable")
                       : QString("%1 snapshot entries · showing %2%3")
                             .arg(input.size())
                             .arg(next.value(kind).toList().size())
                             .arg(input.size() > inspectLimit ? " · limited sample" : "");
    }
    QVariantList issues;
    const auto input = rows(field("errorDetails"));
    for (qsizetype i = 0; i < std::min(input.size(), qsizetype{3}); ++i) {
        if (input[i].metaType() != QMetaType::fromType<QVariantMap>())
            continue;
        const auto issue = input[i].toMap();
        issues.push_back(
            QVariantMap{{"phase", text(issue.value("phase"), 64)},
                        {"kind", text(issue.value("kind"), 64)},
                        {"code", numeric(issue.value("code")) ? issue.value("code") : QVariant{}},
                        {"component", text(issue.value("component"))},
                        {"message", text(issue.value("message"), 512)}});
    }
    next["issues"] = issues;
    // Repeated identical snapshots do not replace delegates or repaint the dashboard.
    if (next != data_) {
        data_ = std::move(next);
        emit changed();
    }
}
