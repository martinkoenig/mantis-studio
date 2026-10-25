#include "projects_model.hpp"
#include <QMetaType>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace {
constexpr qsizetype inspected = 512, rendered = 128;
QString raw(const QVariant &v) {
    return v.metaType() == QMetaType::fromType<QString>() ? v.toString() : QString{};
}
QString display(const QVariant &v, qsizetype limit = 192, const QString &fallback = "Unavailable") {
    const auto value = raw(v);
    if (value.isEmpty())
        return fallback;
    QString out;
    out.reserve(std::min(value.size(), limit) + 1);
    for (qsizetype i = 0; i < std::min(value.size(), limit); ++i) {
        const auto c = value[i];
        out += c.category() == QChar::Other_Control || c.category() == QChar::Other_Format ? QChar(' ') : c;
    }
    if (value.size() > limit)
        out += QChar(0x2026);
    return out;
}
QVariantList list(const QVariant &v) {
    return v.metaType() == QMetaType::fromType<QVariantList>() ? v.toList() : QVariantList{};
}
QString chunks(const QVariant &v) {
    switch (v.metaType().id()) {
    case QMetaType::ULong:
    case QMetaType::Long:
    case QMetaType::UShort:
    case QMetaType::Short:
    case QMetaType::UInt:
    case QMetaType::Int:
    case QMetaType::ULongLong:
    case QMetaType::LongLong:
        if (v.toDouble() > 0)
            return v.toString();
        break;
    default:
        break;
    }
    // Proto3 scalar has no presence bit: zero cannot establish a known empty chunk count.
    return "Unavailable";
}
} // namespace
ProjectsModel::ProjectsModel(QObject *parent) : QObject(parent) {
    refresh();
}
void ProjectsModel::setBridge(QObject *value) {
    if (bridge_ == value)
        return;
    if (bridge_)
        disconnect(bridge_, nullptr, this, nullptr);
    bridge_ = value;
    identity_.clear();
    selected_.clear();
    if (bridge_) {
        if (bridge_->metaObject()->indexOfSignal("changed()") >= 0)
            connect(bridge_, SIGNAL(changed()), this, SLOT(refresh()));
        connect(bridge_, &QObject::destroyed, this, [this] {
            refresh();
            emit bridgeChanged();
        });
    }
    refresh();
    emit bridgeChanged();
}
void ProjectsModel::setQuery(QString v) {
    v = v.left(256);
    if (v == query_)
        return;
    query_ = std::move(v);
    present();
    emit filtersChanged();
}
void ProjectsModel::setTypeFilter(QString v) {
    v = v.left(192);
    if (v == type_)
        return;
    type_ = std::move(v);
    present();
    emit filtersChanged();
}
void ProjectsModel::setStateFilter(QString v) {
    v = v.left(64);
    if (v == state_)
        return;
    state_ = std::move(v);
    present();
    emit filtersChanged();
}
void ProjectsModel::setSort(QString v) {
    if (!QStringList{"ID", "Type", "State"}.contains(v) || v == sort_)
        return;
    sort_ = std::move(v);
    present();
    emit filtersChanged();
}
void ProjectsModel::clearFilters() {
    query_.clear();
    type_.clear();
    state_.clear();
    sort_ = "ID";
    present();
    emit filtersChanged();
}
void ProjectsModel::selectArtifact(const QString &id) {
    if (!id.isEmpty()) {
        bool found = false;
        for (const auto &v : data_.value("artifacts").toList())
            if (v.toMap().value("id") == id) {
                found = true;
                break;
            }
        if (!found)
            return;
    }
    if (selected_ == id)
        return;
    selected_ = id;
    present();
}
void ProjectsModel::refresh() {
    const auto field = [&](const char *name) { return bridge_ ? bridge_->property(name) : QVariant{}; };
    const bool confirmed = field("connected") == QVariant(true);
    const bool has = confirmed || field("hasSnapshot") == QVariant(true);
    const auto identity = has ? raw(field("project")) : QString{};
    if (confirmed && identity != identity_)
        selected_.clear();
    identity_ = identity;
    const auto path = display(identity, 4096, "Project path unavailable");
    auto name = identity.size() > 4096 ? QString("Current project · path truncated")
                : identity.isEmpty()   ? QString("Current project unavailable")
                                       : display(identity.section('/', -1).section('\\', -1));
    if (name == "Unavailable")
        name = "Current runtime project";
    base_ = {{"source", "live"},
             {"confirmed", confirmed},
             {"hasSnapshot", has},
             {"stale", has && !confirmed},
             {"available", has && !identity.isEmpty()},
             {"unknown", !has || identity.isEmpty()},
             {"identity", identity},
             {"path", path},
             {"name", name},
             {"freshness", confirmed ? "Confirmed runtime snapshot"
                           : has     ? "Last known · current state unconfirmed"
                                     : "Runtime state unconfirmed"},
             {"busy", field("busy") == QVariant(true)}};
    const auto inputValue = field("artifacts");
    const bool available = has && inputValue.metaType() == QMetaType::fromType<QVariantList>();
    const auto input = available ? list(inputValue) : QVariantList{};
    QVariantList next;
    QSet<QString> ids;
    QStringList types, states;
    for (qsizetype i = 0; i < std::min(input.size(), inspected); ++i) {
        if (input[i].metaType() != QMetaType::fromType<QVariantMap>())
            continue;
        const auto row = input[i].toMap();
        const auto id = raw(row.value("id"));
        if (id.isEmpty() || id.size() > 4096 || ids.contains(id))
            continue;
        ids.insert(id);
        const auto type = display(row.value("type")), state = display(row.value("state"), 64, "Unknown");
        next.push_back(QVariantMap{{"id", id},
                                   {"displayId", display(id)},
                                   {"type", type},
                                   {"state", state},
                                   {"chunks", chunks(row.value("chunks"))},
                                   {"source", "live"},
                                   {"scan", raw(row.value("type")) == "org.mantis.RawCapture" ||
                                                raw(row.value("type")) == "org.mantis.PointCloud"},
                                   {"mesh", raw(row.value("type")) == "org.mantis.Mesh"},
                                   {"confirmed", confirmed},
                                   {"stale", has && !confirmed}});
        if (!types.contains(type))
            types.push_back(type);
        if (!states.contains(state))
            states.push_back(state);
    }
    types.sort();
    states.sort();
    base_["types"] = types;
    base_["states"] = states;
    base_["artifactsAvailable"] = available;
    base_["snapshotCount"] = available ? QVariant(input.size()) : QVariant{};
    base_["inspectedCount"] = std::min(input.size(), inspected);
    base_["sampled"] = input.size() > inspected;
    // Explicit daemon-wide context; the snapshot supplies no project association.
    for (const auto &[nameKey, property] : {std::pair{"jobs", "jobs"}, std::pair{"events", "diagnostics"}}) {
        QVariantList out;
        const auto values = list(field(property));
        for (qsizetype i = 0; has && i < std::min(values.size(), qsizetype{4}); ++i) {
            if (values[i].metaType() != QMetaType::fromType<QVariantMap>())
                continue;
            const auto v = values[i].toMap();
            out.push_back(QVariantMap{
                {"name", display(v.value(nameKey == QString("jobs") ? "name" : "kind"))},
                {"detail", display(v.value(nameKey == QString("jobs") ? "diagnostics" : "message"), 512, {})},
                {"state", display(v.value("state"), 64, {})}});
        }
        base_[nameKey] = out;
    }
    QVariantList issues;
    const auto errors = list(field("errorDetails"));
    for (qsizetype i = 0; i < std::min(errors.size(), qsizetype{3}); ++i) {
        const auto e = errors[i].toMap();
        issues.push_back(QVariantMap{{"phase", display(e.value("phase"), 64)},
                                     {"code", e.value("code")},
                                     {"component", display(e.value("component"))},
                                     {"message", display(e.value("message"), 512)}});
    }
    base_["issues"] = issues;
    artifacts_ = std::move(next);
    present();
}
void ProjectsModel::present() {
    QVariantList filtered;
    for (const auto &v : artifacts_) {
        const auto row = v.toMap();
        if ((!type_.isEmpty() && row.value("type") != type_) ||
            (!state_.isEmpty() && row.value("state") != state_))
            continue;
        if (!query_.isEmpty() && !row.value("displayId").toString().contains(query_, Qt::CaseInsensitive) &&
            !row.value("type").toString().contains(query_, Qt::CaseInsensitive))
            continue;
        filtered.push_back(row);
    }
    const auto key = sort_ == "Type" ? "type" : sort_ == "State" ? "state" : "id";
    std::sort(filtered.begin(), filtered.end(), [key](const QVariant &a, const QVariant &b) {
        const auto l = a.toMap(), r = b.toMap();
        const auto lc = l.value(key).toString(), rc = r.value(key).toString();
        return lc == rc ? l.value("id").toString() < r.value("id").toString() : lc < rc;
    });
    auto next = base_;
    next["matchingCount"] = filtered.size();
    next["artifacts"] = filtered.mid(0, rendered);
    next["renderLimited"] = filtered.size() > rendered;
    QVariantMap selection;
    for (const auto &v : next.value("artifacts").toList())
        if (v.toMap().value("id") == selected_) {
            selection = v.toMap();
            break;
        }
    if (selection.isEmpty())
        selected_.clear();
    next["selectedId"] = selected_;
    next["selected"] = selection;
    next["summary"] = !base_.value("artifactsAvailable").toBool()
                          ? "Artifact descriptors unavailable"
                          : QString("%1 snapshot entries · %2 matching · showing %3%4")
                                .arg(base_.value("snapshotCount").toLongLong())
                                .arg(filtered.size())
                                .arg(next.value("artifacts").toList().size())
                                .arg(base_.value("sampled").toBool() || filtered.size() > rendered
                                         ? " · limited sample; search covers the first 512 inspected entries"
                                         : "");
    if (next != data_) {
        data_ = std::move(next);
        emit changed();
    }
}
