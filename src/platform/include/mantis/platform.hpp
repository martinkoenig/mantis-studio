#pragma once
#include <chrono>
#include <filesystem>
#include <mantis/base.hpp>
#include <vector>
namespace mantis::platform {
class Library {
    void *handle_{};

  public:
    explicit Library(const std::filesystem::path &);
    ~Library();
    Library(const Library &) = delete;
    Library &operator=(const Library &) = delete;
    void *symbol(const char *) const;
};
class Socket {
    intptr_t fd_{-1};

  public:
    Socket() = default;
    explicit Socket(intptr_t fd) : fd_(fd) {}
    ~Socket();
    Socket(const Socket &) = delete;
    Socket &operator=(const Socket &) = delete;
    Socket(Socket &&) noexcept;
    Socket &operator=(Socket &&) noexcept;
    static Socket connect(uint16_t port);
    static Socket listen(uint16_t port);
    Socket accept() const;
    void send(std::span<const std::byte>) const;
    void receive(std::span<std::byte>) const;
};
struct Mapping {
    std::shared_ptr<const void> owner;
    const std::byte *data{};
    size_t size{};
};
Mapping map_read(const std::filesystem::path &);
void durable_file(const std::filesystem::path &);
void durable_directory(const std::filesystem::path &);
int run_process(const std::filesystem::path &executable, const std::vector<std::string> &args,
                const CancellationToken &cancel = {},
                std::chrono::seconds timeout = std::chrono::seconds(30));
class FileLock {
    intptr_t handle_{-1};

  public:
    explicit FileLock(const std::filesystem::path &);
    ~FileLock();
    FileLock(const FileLock &) = delete;
    FileLock &operator=(const FileLock &) = delete;
};
} // namespace mantis::platform
