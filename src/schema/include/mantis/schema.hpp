#pragma once
#include <algorithm>
#include <mantis/base.hpp>
#include <vector>
namespace mantis::schema {
enum class ScalarType : uint32_t { u8 = 1, i64 = 2, f32 = 3, f64 = 4, u32 = 5 };
inline size_t scalar_size(ScalarType t) {
    switch (t) {
    case ScalarType::u8:
        return 1;
    case ScalarType::f32:
    case ScalarType::u32:
        return 4;
    case ScalarType::i64:
    case ScalarType::f64:
        return 8;
    }
    fail(Status::invalid_argument, "Invalid scalar type");
}
struct DataTypeId {
    std::string name;
    uint32_t version{1};
    auto operator<=>(const DataTypeId &) const = default;
};
inline const DataTypeId image{"org.mantis.ImageFrame", 1}, points{"org.mantis.PointCloud", 1},
    mesh{"org.mantis.Mesh", 1}, tensor{"org.mantis.Tensor", 1}, frameset{"org.mantis.FrameSet", 1};
struct AttributeDescriptor {
    std::string name;
    ScalarType scalar{ScalarType::u8};
    std::vector<uint64_t> shape, stride;
    std::string unit;
};
inline Result<void> validate(const AttributeDescriptor &a, size_t bytes) {
    if (a.name.find('.') == std::string::npos || a.shape.empty() || a.shape.size() != a.stride.size() ||
        a.shape.size() > 4)
        return std::unexpected(Error{Status::invalid_argument,
                                     "Attribute requires namespaced name, shape and byte strides", "schema"});
    uint64_t end = scalar_size(a.scalar);
    for (size_t i = 0; i < a.shape.size(); ++i) {
        if (a.shape[i] == 0)
            return std::unexpected(Error{Status::invalid_argument, "Empty attribute dimension", "schema"});
        if (a.shape[i] - 1 > (UINT64_MAX - end) / std::max<uint64_t>(a.stride[i], 1))
            return std::unexpected(Error{Status::invalid_argument, "Attribute shape overflow", "schema"});
        end += (a.shape[i] - 1) * a.stride[i];
    }
    if (end > bytes)
        return std::unexpected(Error{Status::invalid_argument, "Attribute exceeds backing buffer", "schema"});
    return {};
}
} // namespace mantis::schema
