// Executable-local linker seam: ordinary mantisd/platform code has no gate.
#include "gate.hpp"
#include <atomic>
#include <filesystem>
#include <iostream>
#include <sys/mman.h>
#include <vector>
namespace {
int gate = -1;
std::string objects;
std::atomic<bool> held{};
} // namespace
extern "C" void *__real_mmap(void *, size_t, int, int, int, off_t);
extern "C" void *__wrap_mmap(void *address, size_t size, int prot, int flags, int fd, off_t offset) {
    char name[4096]{};
    const auto length = ::readlink(("/proc/self/fd/" + std::to_string(fd)).c_str(), name, sizeof(name) - 1);
    const std::string_view path(name, length > 0 ? static_cast<size_t>(length) : 0);
    // Capture writes use .packet.part. Only an explicitly armed, finalized
    // packet read may pause. object_path releases its SQLite mutex before mapping.
    if (gate >= 0 && path.starts_with(objects) && path.ends_with(".packet") && !held.load()) {
        char command{};
        if (::recv(gate, &command, 1, MSG_DONTWAIT) == 1) {
            if (command != 'A' || held.exchange(true)) {
                errno = EIO;
                return MAP_FAILED;
            }
            try {
                std::cout << "REPLAY_GATE entered finalized packet read" << std::endl;
                qualification::send(gate, 'R');
                if (qualification::receive(gate) != 'G')
                    throw std::runtime_error("Invalid gate release");
                std::cout << "REPLAY_GATE released" << std::endl;
            } catch (const std::exception &e) {
                std::cerr << e.what() << std::endl;
                errno = EIO;
                return MAP_FAILED;
            }
        }
    }
    return __real_mmap(address, size, prot, flags, fd, offset);
}
#define main mantis_daemon_entry
#include "../../../apps/mantisd/main.cpp"
#undef main
int main(int argc, char **argv) {
    // Gate FD is supplied by our Python harness, never from deployed configuration.
    if (argc < 3 || std::string_view(argv[1]) != "--test-replay-gate")
        return 2;
    gate = std::stoi(argv[2]);
    for (int i = 3; i + 1 < argc; ++i)
        if (std::string_view(argv[i]) == "--project")
            objects = (std::filesystem::absolute(argv[i + 1]) / "objects").string() + "/";
    if (objects.empty())
        return 2;
    std::vector<char *> arguments{argv[0]};
    for (int i = 3; i < argc; ++i)
        arguments.push_back(argv[i]);
    arguments.push_back(nullptr);
    const auto result = mantis_daemon_entry(static_cast<int>(arguments.size() - 1), arguments.data());
    ::close(gate);
    return result;
}
