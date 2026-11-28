#include "product_paths.hpp"
#include <stdexcept>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
namespace mantis::app {
std::filesystem::path executable_path() {
#if defined(__linux__)
    return std::filesystem::canonical("/proc/self/exe");
#elif defined(_WIN32)
    // Windows maximum extended path length, including a terminating null.
    std::vector<wchar_t> buffer(32768);
    auto count = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!count || count >= buffer.size())
        throw std::runtime_error("Cannot resolve executable image path");
    return std::filesystem::canonical(std::filesystem::path(buffer.data(), buffer.data() + count));
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> buffer(size);
    if (!size || _NSGetExecutablePath(buffer.data(), &size))
        throw std::runtime_error("Cannot resolve executable image path");
    return std::filesystem::canonical(buffer.data());
#else
    throw std::runtime_error("Executable image path unavailable on this platform");
#endif
}
} // namespace mantis::app
