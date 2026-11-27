#include "../../../src/services/alg03_ply.hpp"
#include <bit>
#include <cfenv>
#include <iostream>

namespace {
using mantis::services::detail::binary32_rne_bits;
struct Expected {
    double input;
    uint32_t bits;
};
constexpr Expected cases[] = {
    {0.0, 0x00000000},
    {-0.0, 0x80000000},
    {0x1p-1074, 0x00000000},
    {-0x1p-150, 0x80000000}, // Halfway to the least subnormal rounds to zero.
    {0x1.0000000000001p-150, 0x00000001},
    {0x1.8p-149, 0x00000002},      // Odd lower subnormal, even upper.
    {0x1.fffffep-127, 0x00800000}, // Subnormal to normal carry.
    {0x1.000000fffffffp0, 0x3f800000},
    {0x1.000001p0, 0x3f800000}, // Even lower, odd upper.
    {0x1.0000010000001p0, 0x3f800001},
    {-0x1.000003p0, 0xbf800002},  // Odd lower magnitude, even upper.
    {0x1.ffffffp0, 0x40000000},   // Carry at exponent boundary.
    {999999.96875, 0x49742400},   // Halfway to 1e6 mm, error exactly 0.03125 mm.
    {0x1.fffffep127, 0x7f7fffff}, // Largest finite float.
};

void refuse(uint64_t bits, mantis::Status expected) {
    try {
        (void)binary32_rne_bits(bits);
    } catch (const mantis::Failure &error) {
        if (error.error.code == expected)
            return;
        throw;
    }
    throw std::runtime_error("Nonfinite or out-of-range value was accepted");
}
} // namespace

int main() {
    try {
        for (int mode : {FE_TONEAREST, FE_UPWARD, FE_DOWNWARD, FE_TOWARDZERO}) {
            if (std::fesetround(mode) || std::feclearexcept(FE_ALL_EXCEPT) ||
                std::feraiseexcept(FE_DIVBYZERO))
                throw std::runtime_error("Could not establish rounding test environment");
            const int flags = std::fetestexcept(FE_ALL_EXCEPT);
            for (const auto &[input, expected] : cases) {
                const auto raw = std::bit_cast<uint64_t>(input);
                const auto got = binary32_rne_bits(raw);
                if (got != expected) {
                    std::cerr << std::hex << "binary64=" << raw << " expected binary32=" << expected
                              << " actual=" << got << '\n';
                    return 1;
                }
            }
            refuse(0x7ff8000000000000ull, mantis::Status::corrupt);
            refuse(0xfff0000000000000ull, mantis::Status::corrupt);
            refuse(0x47efffffe0000001ull, mantis::Status::incompatible);
            if (std::fegetround() != mode || std::fetestexcept(FE_ALL_EXCEPT) != flags)
                throw std::runtime_error("Narrowing changed the ambient FP environment");
        }
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
