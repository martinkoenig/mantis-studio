#pragma once
#include <QStringList>
#include <QVariantList>

namespace studio_capture {
inline constexpr qsizetype descriptorLimit = 256, identityLimit = 4096;
inline bool capable(const QVariant &value) {
    if (value.metaType() != QMetaType::fromType<QStringList>())
        return false;
    const auto capabilities = value.toStringList();
    if (capabilities.size() > 64)
        return false;
    return capabilities.contains("org.mantis.camera.image-stream.v1") ||
           capabilities.contains("org.mantis.camera.frameset-stream.v1");
}
// Only an exact, unique admitted top-level descriptor may authorize a capture.
// Reject an incomplete oversized inventory rather than guessing uniqueness.
inline bool uniqueDevice(const QVariantList &devices, const QString &id) {
    if (id.isEmpty() || id.size() > identityLimit || devices.size() > descriptorLimit)
        return false;
    int matches = 0;
    bool supported = false;
    for (const auto &value : devices) {
        if (value.metaType() != QMetaType::fromType<QVariantMap>())
            continue;
        const auto row = value.toMap();
        const auto identity = row.value("id");
        if (identity.metaType() == QMetaType::fromType<QString>() && identity.toString() == id) {
            ++matches;
            supported = capable(row.value("capabilities"));
        }
    }
    return matches == 1 && supported;
}
} // namespace studio_capture
