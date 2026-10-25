#pragma once
#include <mantis/sdk.hpp>
#include <string>
namespace x1_fixture_views {
template <class T> T view() {
    T v{};
    v.struct_size = sizeof(v);
    v.abi_version = 1;
    return v;
}
template <class T> T absent(uint32_t p = MANTIS_PRESENCE_UNAVAILABLE) {
    auto v = view<T>();
    v.presence = p;
    return v;
}
template <class T, class V> T present(const V *p) {
    auto v = absent<T>(MANTIS_PRESENCE_ESTABLISHED);
    v.value = p;
    return v;
}
MantisDataTypeV1 type(const char *name) {
    auto v = view<MantisDataTypeV1>();
    v.name = name;
    v.version = 1;
    return v;
}
// Borrowed reference metadata is owned exactly, including all presence values.
struct Reference {
    MantisProgramReferenceV1 ref{};
    MantisHashV1 hash{}, content_hash{};
    MantisContentReferenceV1 content{};
    std::string id, algorithm, hex, content_id, content_type, content_algorithm, content_hex;
    void copy(const MantisProgramReferenceV1 &v) {
        ref = v;
        id = v.id;
        if (v.hash.value) {
            hash = *v.hash.value;
            algorithm = hash.algorithm;
            hex = hash.hex;
        }
        if (v.content.value) {
            content = *v.content.value;
            content_id = content.id;
            content_type = content.type.name;
            if (content.hash.value) {
                content_hash = *content.hash.value;
                content_algorithm = content_hash.algorithm;
                content_hex = content_hash.hex;
            }
        }
        ref.id = nullptr;
        ref.hash.value = nullptr;
        ref.content.value = nullptr;
        hash.algorithm = hash.hex = nullptr;
        content.id = content.type.name = nullptr;
        content.hash.value = nullptr;
        content_hash.algorithm = content_hash.hex = nullptr;
    }
    MantisProgramReferenceV1 get() {
        auto v = ref;
        v.id = id.c_str();
        if (v.hash.presence == MANTIS_PRESENCE_ESTABLISHED) {
            hash.algorithm = algorithm.c_str();
            hash.hex = hex.c_str();
            v.hash.value = &hash;
        }
        if (v.content.presence == MANTIS_PRESENCE_ESTABLISHED) {
            content.id = content_id.c_str();
            content.type.name = content_type.c_str();
            if (content.hash.presence == MANTIS_PRESENCE_ESTABLISHED) {
                content_hash.algorithm = content_algorithm.c_str();
                content_hash.hex = content_hex.c_str();
                content.hash.value = &content_hash;
            }
            v.content.value = &content;
        }
        return v;
    }
};
} // namespace x1_fixture_views
