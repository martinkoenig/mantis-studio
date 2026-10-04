#include <cstring>
#include <cerrno>
#include <mantis/platform.hpp>
#include <thread>
#include <utility>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif
namespace mantis::platform {
namespace {
#ifdef _WIN32
using NativeSocket = SOCKET;
#else
using NativeSocket = int;
#endif
[[noreturn]] void io(const std::string &message) {
    fail(Status::io, message, "platform");
}
#ifdef _WIN32
void net_init() {
    static bool ready = []() {
        WSADATA w;
        return WSAStartup(MAKEWORD(2, 2), &w) == 0;
    }();
    if (!ready)
        io("WSAStartup failed");
}
void close_socket(intptr_t fd) {
    closesocket(static_cast<SOCKET>(fd));
}
#else
void net_init() {}
void close_socket(intptr_t fd) {
    ::close(static_cast<int>(fd));
}
#endif
} // namespace
Library::Library(const std::filesystem::path &p) {
#ifdef _WIN32
    handle_ = LoadLibraryW(p.c_str());
#else
    handle_ = dlopen(p.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
    if (!handle_)
        io("Cannot load plugin: " + p.string());
}
Library::~Library() {
    if (handle_) {
#ifdef _WIN32
        FreeLibrary(static_cast<HMODULE>(handle_));
#else
        dlclose(handle_);
#endif
    }
}
void *Library::symbol(const char *name) const {
#ifdef _WIN32
    auto p = reinterpret_cast<void *>(GetProcAddress(static_cast<HMODULE>(handle_), name));
#else
    auto p = dlsym(handle_, name);
#endif
    if (!p)
        io(std::string("Missing plugin symbol: ") + name);
    return p;
}
Socket::~Socket() {
    if (fd_ != -1)
        close_socket(fd_);
}
Socket::Socket(Socket &&o) noexcept : fd_(std::exchange(o.fd_, -1)) {}
Socket &Socket::operator=(Socket &&o) noexcept {
    if (this != &o) {
        if (fd_ != -1)
            close_socket(fd_);
        fd_ = std::exchange(o.fd_, -1);
    }
    return *this;
}
namespace {
void connection_timeouts(NativeSocket native) {
#ifdef _WIN32
    DWORD timeout = 5000;
    if (setsockopt(native, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&timeout), sizeof(timeout)) != 0 ||
        setsockopt(native, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char *>(&timeout), sizeof(timeout)) != 0)
        io("Control socket timeout configuration failed: WSA " + std::to_string(WSAGetLastError()));
#else
    timeval timeout{5, 0};
    if (setsockopt(native, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0 ||
        setsockopt(native, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) != 0)
        io("Control socket timeout configuration failed: " + std::string(std::strerror(errno)));
#endif
}
Socket open_socket(uint16_t port, bool server) {
    net_init();
    auto native = ::socket(AF_INET, SOCK_STREAM, 0);
    if (static_cast<intptr_t>(native) == -1)
        io("Cannot create control socket");
    Socket socket(static_cast<intptr_t>(native));
#ifdef SO_NOSIGPIPE
    int no_sigpipe = 1;
    setsockopt(native, SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe, sizeof(no_sigpipe));
#endif
#ifndef _WIN32
    const int flags = fcntl(native, F_GETFD);
    if (flags < 0 || fcntl(native, F_SETFD, flags | FD_CLOEXEC) < 0)
        io("Control socket FD_CLOEXEC failed: " + std::string(std::strerror(errno)));
#endif
    // A listener must wait indefinitely. Connection read/write deadlines are
    // applied to client/accepted sockets, never inherited by accept().
    if (!server) connection_timeouts(native);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    if (server) {
        int one = 1;
        setsockopt(native, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&one), sizeof(one));
        if (::bind(native, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0 ||
            ::listen(native, 32) != 0)
            io("Cannot bind loopback endpoint");
    } else if (::connect(native, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0)
        io("Cannot connect to mantisd; check port/token and start the daemon");
    return socket;
}
} // namespace
Socket Socket::connect(uint16_t port) {
    return open_socket(port, false);
}
Socket Socket::listen(uint16_t port) {
    return open_socket(port, true);
}
Socket Socket::accept() const {
    NativeSocket native;
    for (;;) {
        native = ::accept(static_cast<NativeSocket>(fd_), nullptr, nullptr);
        if (static_cast<intptr_t>(native) != -1) break;
#ifdef _WIN32
        const auto error = WSAGetLastError();
        if (error == WSAEINTR) continue;
        if (error != WSAEWOULDBLOCK) io("Accept failed: WSA " + std::to_string(error));
        WSAPOLLFD ready{static_cast<SOCKET>(fd_), POLLRDNORM, 0};
        while (WSAPoll(&ready, 1, -1) < 0) {
            const auto poll_error = WSAGetLastError();
            if (poll_error != WSAEINTR) io("Accept wait failed: WSA " + std::to_string(poll_error));
        }
#else
        const auto error = errno;
        if (error == EINTR) continue;
        if (error != EAGAIN && error != EWOULDBLOCK)
            io("Accept failed: " + std::string(std::strerror(error)) + " (errno " + std::to_string(error) + ")");
        pollfd ready{static_cast<int>(fd_), POLLIN, 0};
        while (::poll(&ready, 1, -1) < 0) {
            if (errno != EINTR) io("Accept wait failed: " + std::string(std::strerror(errno)));
        }
#endif
        if (ready.revents & (POLLERR | POLLHUP | POLLNVAL)) io("Accept wait failed: listener unavailable");
    }
    Socket accepted(static_cast<intptr_t>(native));
#ifndef _WIN32
    if (fcntl(native, F_SETFD, FD_CLOEXEC) < 0) io("Accepted socket FD_CLOEXEC failed: " + std::string(std::strerror(errno)));
#endif
    connection_timeouts(native);
    return accepted;
}
void Socket::send(std::span<const std::byte> bytes) const {
    while (!bytes.empty()) {
#ifdef _WIN32
        auto n = ::send(static_cast<SOCKET>(fd_), reinterpret_cast<const char *>(bytes.data()),
                        static_cast<int>(bytes.size()), 0);
#else
        auto n = ::send(static_cast<int>(fd_), bytes.data(), bytes.size(),
#ifdef MSG_NOSIGNAL
                        MSG_NOSIGNAL
#else
                        0
#endif
        );
#endif
        if (n <= 0)
            io("Control connection write failed");
        bytes = bytes.subspan(static_cast<size_t>(n));
    }
}
void Socket::receive(std::span<std::byte> bytes) const {
    while (!bytes.empty()) {
#ifdef _WIN32
        auto n = ::recv(static_cast<SOCKET>(fd_), reinterpret_cast<char *>(bytes.data()),
                        static_cast<int>(bytes.size()), 0);
#else
        auto n = ::recv(static_cast<int>(fd_), bytes.data(), bytes.size(), 0);
#endif
        if (n <= 0)
            io("Control connection closed or timed out");
        bytes = bytes.subspan(static_cast<size_t>(n));
    }
}
Mapping map_read(const std::filesystem::path &path) {
#ifdef _WIN32
    auto file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        io("Cannot open data object");
    LARGE_INTEGER size;
    GetFileSizeEx(file, &size);
    auto mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    CloseHandle(file);
    if (!mapping)
        io("Cannot map data object");
    auto p = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    CloseHandle(mapping);
    if (!p)
        io("Cannot map data view");
    return {std::shared_ptr<const void>(p, [](const void *v) { UnmapViewOfFile(v); }),
            static_cast<const std::byte *>(p), static_cast<size_t>(size.QuadPart)};
#else
    int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        io("Cannot open data object: " + path.string());
    struct stat st {};
    if (fstat(fd, &st) || st.st_size <= 0) {
        ::close(fd);
        io("Empty or invalid data object");
    }
    auto size = static_cast<size_t>(st.st_size);
    void *p = mmap(nullptr, size, PROT_READ, MAP_SHARED, fd, 0);
    ::close(fd);
    if (p == MAP_FAILED)
        io("mmap failed");
    return {std::shared_ptr<const void>(p, [size](const void *v) { munmap(const_cast<void *>(v), size); }),
            static_cast<const std::byte *>(p), size};
#endif
}
void durable_file(const std::filesystem::path &path) {
#ifdef _WIN32
    auto h = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        io("Cannot sync file");
    auto ok = FlushFileBuffers(h);
    CloseHandle(h);
    if (!ok)
        io("File sync failed");
#else
    int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        io("Cannot sync file");
    int status = fsync(fd);
    close(fd);
    if (status)
        io("File sync failed");
#endif
}
void durable_directory(const std::filesystem::path &path) {
#ifndef _WIN32
    int fd = open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0)
        io("Cannot sync directory");
    int status = fsync(fd);
    close(fd);
    if (status)
        io("Directory sync failed");
#else
    (void)
        path; // NTFS file durability uses FlushFileBuffers; directory power-loss semantics are not certified.
#endif
}
int run_process(const std::filesystem::path &exe, const std::vector<std::string> &args,
                const CancellationToken &cancel, std::chrono::seconds timeout) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
#ifdef _WIN32
    auto quote = [](const std::string &s) {
        std::string r = "\"";
        size_t slashes = 0;
        for (char c : s) {
            if (c == '\\') {
                ++slashes;
                continue;
            }
            r.append(c == '\"' ? slashes * 2 + 1 : slashes, '\\');
            slashes = 0;
            r += c;
        }
        r.append(slashes * 2, '\\');
        return r + '\"';
    };
    std::string cmd = quote(exe.string());
    for (auto &a : args)
        cmd += ' ' + quote(a);
    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION proc{};
    if (!CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                        &startup, &proc))
        io("Cannot start plugin host");
    CloseHandle(proc.hThread);
    while (WaitForSingleObject(proc.hProcess, 10) == WAIT_TIMEOUT) {
        if (cancel.cancelled() || std::chrono::steady_clock::now() > deadline) {
            TerminateProcess(proc.hProcess, 1);
            WaitForSingleObject(proc.hProcess, INFINITE);
            CloseHandle(proc.hProcess);
            if (cancel.cancelled())
                cancel.check();
            io("Plugin host timed out");
        }
    }
    DWORD code;
    GetExitCodeProcess(proc.hProcess, &code);
    CloseHandle(proc.hProcess);
    return static_cast<int>(code);
#else
    std::vector<std::string> storage{exe.string()};
    storage.insert(storage.end(), args.begin(), args.end());
    std::vector<char *> argv;
    for (auto &s : storage)
        argv.push_back(s.data());
    argv.push_back(nullptr);
    pid_t pid{};
    int rc = posix_spawn(&pid, exe.c_str(), nullptr, nullptr, argv.data(), environ);
    if (rc)
        io("Cannot spawn plugin host: " + std::string(strerror(rc)));
    int status{};
    while (true) {
        auto r = waitpid(pid, &status, WNOHANG);
        if (r == pid)
            break;
        if (r < 0 && errno != EINTR)
            io("Cannot reap plugin host");
        if (cancel.cancelled() || std::chrono::steady_clock::now() > deadline) {
            kill(pid, SIGKILL);
            while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
            }
            if (cancel.cancelled())
                cancel.check();
            io("Plugin host timed out");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
#endif
}
FileLock::FileLock(const std::filesystem::path &p) {
#ifdef _WIN32
    auto h = CreateFileW(p.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        fail(Status::busy, "Project already open");
    handle_ = reinterpret_cast<intptr_t>(h);
#else
    int fd = open(p.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    if (fd < 0)
        io("Cannot open project lock");
    if (flock(fd, LOCK_EX | LOCK_NB)) {
        close(fd);
        fail(Status::busy, "Project already open by another runtime");
    }
    handle_ = fd;
#endif
}
FileLock::~FileLock() {
    if (handle_ != -1) {
#ifdef _WIN32
        CloseHandle(reinterpret_cast<HANDLE>(handle_));
#else
        close(static_cast<int>(handle_));
#endif
    }
}
} // namespace mantis::platform
