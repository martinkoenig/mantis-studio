#include <iomanip>
#include <mantis/base.hpp>
#include <mutex>
#include <random>
#include <sstream>
namespace mantis {
Id Id::random() {
    static std::mutex mutex;
    static std::random_device rd;
    std::lock_guard lock(mutex);
    std::array<unsigned char, 16> b{};
    for (auto &v : b)
        v = static_cast<unsigned char>(rd());
    b[6] = static_cast<unsigned char>((b[6] & 15) | 64);
    b[8] = static_cast<unsigned char>((b[8] & 63) | 128);
    std::ostringstream s;
    s << std::hex << std::setfill('0');
    for (size_t i = 0; i < b.size(); ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10)
            s << '-';
        s << std::setw(2) << static_cast<unsigned>(b[i]);
    }
    return {s.str()};
}
// Non-cryptographic integrity checksum. The algorithm tag prevents confusing it with a security hash.
Hash content_hash(std::span<const std::byte> bytes) {
    uint64_t h = 14695981039346656037ULL;
    for (auto b : bytes) {
        h ^= std::to_integer<uint8_t>(b);
        h *= 1099511628211ULL;
    }
    std::ostringstream s;
    s << std::hex << std::setfill('0') << std::setw(16) << h;
    return {"fnv1a64", s.str()};
}
} // namespace mantis
