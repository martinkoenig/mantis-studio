#include "alg03_ply.hpp"

#include <bit>
#include <cstring>
#include <limits>
#include <mantis/l1_ray_plane.hpp>
#include <nlohmann/json.hpp>
#include <utility>

namespace mantis::services::detail {
namespace {
constexpr size_t axes = 3, packed_stride = axes * sizeof(uint32_t);
constexpr size_t source_stride = axes * sizeof(uint64_t);

std::span<const std::byte> view(const data::Attribute &attribute, const CancellationToken &token) {
    auto bytes = attribute.buffer.map_read(token);
    if (!bytes)
        throw Failure(bytes.error());
    return *bytes;
}

bool binary32_supported() {
    // ALG03's validated Tensor encodes binary64 little-endian; the native PLY
    // exporter reads each packed position as a host float via memcpy.
    if constexpr (sizeof(float) == sizeof(uint32_t)) {
        const float one = 1.0f;
        uint32_t bits{};
        std::memcpy(&bits, &one, sizeof(bits));
        return std::endian::native == std::endian::little && std::numeric_limits<float>::is_iec559 &&
               std::numeric_limits<float>::digits == 24 && std::numeric_limits<float>::max_exponent == 128 &&
               bits == 0x3f800000u;
    }
    return false;
}
} // namespace

uint32_t binary32_rne_bits(uint64_t bits) {
    const auto sign = static_cast<uint32_t>(bits >> 32) & 0x80000000u;
    const auto magnitude = bits & 0x7fffffffffffffffull;
    if (magnitude >= 0x7ff0000000000000ull)
        fail(Status::corrupt, "ALG03 export: nonfinite position");
    if (magnitude > 0x47efffffe0000000ull) // Largest finite binary32, exactly widened.
        fail(Status::incompatible, "ALG03 export: position outside binary32 range");
    const int exponent = static_cast<int>(magnitude >> 52) - 1023;
    // Below half the least binary32 subnormal (also covers binary64 zero and
    // subnormals). Preserve the sign when rounding to zero.
    if (exponent < -150)
        return sign;
    const bool normal = exponent >= -126;
    const int shift = normal ? 29 : -exponent - 97;
    const auto significand = (magnitude & 0x000fffffffffffffull) | (uint64_t{1} << 52);
    auto rounded = static_cast<uint32_t>(significand >> shift);
    const auto half = uint64_t{1} << (shift - 1);
    const auto remainder = significand & ((half << 1) - 1);
    if (remainder > half || (remainder == half && (rounded & 1)))
        ++rounded;
    // Addition carries into the next exponent at a power of two, including
    // the transition from the largest subnormal to the smallest normal.
    const auto biased = normal ? static_cast<uint32_t>(exponent + 126) << 23 : 0;
    return sign | (biased + rounded);
}

data::Published alg03_ply_input(const artifact::Store &store, const Id &source,
                                const CancellationToken &token) {
    auto result = alg03::load_result(store, source, token);
    if (!result)
        throw Failure(result.error());
    token.check();
    const auto &tensor = **result;
    // Only extract fields from the already typed-validated v1 table. This is
    // not a generic Tensor adapter or a second schema/provenance validator.
    const auto [rows, accepted] = [&] {
        const auto bytes = view(tensor.attributes.at(0), token);
        const auto *first = reinterpret_cast<const char *>(bytes.data());
        const auto manifest = nlohmann::json::parse(first, first + bytes.size());
        const auto &counts = manifest.at("payload");
        const auto n = counts.at("sample_count").get<size_t>();
        const auto a = counts.at("accepted_count").get<size_t>();
        token.check();
        if (!a) {
            if (counts.at("outcome") == "empty_input")
                fail(Status::invalid_argument, "ALG03 export: empty_input (no source rows)");
            fail(Status::invalid_argument, "ALG03 export: all_rejected (no accepted rows)");
        }
        return std::pair{n, a};
    }();
    if (!binary32_supported())
        fail(Status::unsupported, "ALG03 export requires little-endian IEEE 754 binary32");
    if (rows > alg03::max_samples || accepted > rows)
        fail(Status::corrupt, "ALG03 export: invalid row extent");
    const auto mask = view(tensor.attributes.at(4), token); // accepted u8[N]
    const auto xyz = view(tensor.attributes.at(6), token);  // position f64[N,3]
    if (mask.size() != rows || xyz.size() != rows * source_stride)
        fail(Status::corrupt, "ALG03 export: invalid column extent");

    token.check();
    memory::BufferBuilder packed(accepted * packed_stride);
    auto destination = packed.writable();
    size_t written = 0;
    for (size_t row = 0; row < rows; ++row) {
        token.check();
        if (mask[row] == std::byte{0})
            continue;
        if (written == accepted)
            fail(Status::corrupt, "ALG03 export: accepted count mismatch");
        for (size_t axis = 0; axis < axes; ++axis) {
            uint64_t bits;
            std::memcpy(&bits, xyz.data() + row * source_stride + axis * sizeof(bits), sizeof(bits));
            const auto rounded = binary32_rne_bits(bits);
            std::memcpy(destination.data() + written * packed_stride + axis * sizeof(rounded), &rounded,
                        sizeof(rounded));
        }
        ++written;
    }
    if (written != accepted)
        fail(Status::corrupt, "ALG03 export: accepted count mismatch");
    token.check();
    data::Packet cloud;
    cloud.type = schema::points;
    cloud.header = tensor.header;
    cloud.attributes.push_back({{"org.mantis.position",
                                 schema::ScalarType::f32,
                                 {accepted, axes},
                                 {packed_stride, sizeof(uint32_t)},
                                 "mm"},
                                std::move(packed).publish()});
    return data::publish(std::move(cloud));
}
} // namespace mantis::services::detail
