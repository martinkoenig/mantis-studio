#include "faults.hpp"
#include "../../src/artifact-store/persistence-test.hpp"
#include <array>
#include <cerrno>
#include <climits>
#include <cstring>
#include <mantis/base.hpp>
#include <mutex>
#include <optional>
#include <poll.h>
#include <sqlite3.h>
#include <sys/stat.h>
#include <unistd.h>
namespace mantis::artifact::storage_test {
namespace {
struct Slot {
    Plan plan;
    Metrics metrics;
    bool consumed{};
    uint64_t remaining{};
};
std::mutex mutex;
std::array<std::optional<Slot>, 8> slots;
using Clock = std::chrono::steady_clock;
void io(bool ok, const char *text) {
    if (!ok)
        fail(Status::io, text, "storage-fault");
}
void ready(int fd, short interest, Clock::time_point deadline) {
    for (;;) {
        auto ms = std::chrono::ceil<std::chrono::milliseconds>(deadline - Clock::now()).count();
        if (ms <= 0)
            fail(Status::io, "Storage checkpoint pipe deadline", "storage-fault");
        pollfd p{fd, interest, 0};
        auto n = ::poll(&p, 1, static_cast<int>(std::min<int64_t>(ms, INT_MAX)));
        if (n < 0 && errno == EINTR)
            continue;
        io(n > 0 && (p.revents & interest), "Storage checkpoint pipe unavailable");
        return;
    }
}
void transfer(int fd, void *bytes, size_t size, bool write, Clock::time_point deadline) {
    auto p = static_cast<char *>(bytes);
    while (size) {
        ready(fd, write ? POLLOUT : POLLIN, deadline);
        auto n = write ? ::write(fd, p, size) : ::read(fd, p, size);
        if (n < 0 && errno == EINTR)
            continue;
        io(n > 0, "Storage checkpoint pipe closed");
        p += n;
        size -= static_cast<size_t>(n);
    }
}
void gate(const Plan &p, std::string_view id) {
    Event event;
    io(p.point.size() < sizeof event.point && id.size() < sizeof event.id, "Storage checkpoint text bound");
    std::memcpy(event.point, p.point.data(), p.point.size());
    std::memcpy(event.id, id.data(), id.size());
    auto deadline = Clock::now() + std::chrono::seconds(10);
    transfer(p.notify, &event, sizeof event, true, deadline);
    char release{};
    transfer(p.release, &release, 1, false, deadline);
    io(release == 1, "Invalid storage checkpoint release");
}
std::optional<Plan> hit(std::string_view point, const std::filesystem::path &root, std::string_view id) {
    std::lock_guard lock(mutex);
    for (auto &s : slots)
        if (s && !s->consumed && s->plan.point == point && s->plan.root == root &&
            (s->plan.id.empty() || s->plan.id == id)) {
            if (s->plan.skip) {
                --s->plan.skip;
                return {};
            }
            s->consumed = true;
            ++s->metrics.hits;
            s->metrics.error = s->plan.error;
            return s->plan;
        }
    return {};
}
struct Allowance {
    size_t bytes;
    bool failed{};
    std::optional<Plan> pause;
};
Allowance allowance(const std::filesystem::path &path, size_t count) {
    auto name = path.filename().string();
    auto point = name.ends_with(".segment.part") ? "record-write"
                 : name == "run.outcome.part"    ? "outcome-write"
                                                 : "header-write";
    auto id = path.parent_path().filename().string();
    auto root = path.parent_path().parent_path().parent_path();
    std::lock_guard lock(mutex);
    for (auto &s : slots)
        if (s && !s->consumed && s->plan.point == point && s->plan.root == root &&
            (s->plan.id.empty() || s->plan.id == id)) {
            auto bytes = static_cast<size_t>(std::min<uint64_t>(s->remaining, count));
            s->remaining -= bytes;
            s->metrics.written += bytes;
            if (bytes < count) {
                s->consumed = true;
                ++s->metrics.hits;
                ++s->metrics.short_results;
                s->metrics.error = s->plan.error;
                errno = s->plan.error;
                return {bytes, true,
                        s->plan.action == Action::short_pause ? std::optional<Plan>(s->plan) : std::nullopt};
            }
            return {bytes, false, {}};
        }
    return {count, false, {}};
}
} // namespace
Arm::Arm(Plan p) {
    p.root = std::filesystem::absolute(p.root).lexically_normal();
    io(p.root.string().size() <= 4096 && p.id.size() <= 256 && p.point.size() < 64, "Storage plan bound");
    std::lock_guard lock(mutex);
    for (size_t i = 0; i < slots.size(); ++i)
        if (!slots[i]) {
            auto n = p.bytes;
            slots[i] = Slot{std::move(p), {}, false, n};
            slot_ = i;
            return;
        }
    fail(Status::busy, "Storage test plan capacity", "storage-fault");
}
Arm::~Arm() {
    std::lock_guard lock(mutex);
    slots[slot_].reset();
}
Metrics Arm::metrics() const {
    std::lock_guard lock(mutex);
    return slots[slot_]->metrics;
}
Event await_event(int fd, Clock::time_point deadline) {
    Event event;
    transfer(fd, &event, sizeof event, false, deadline);
    return event;
}
void release_gate(int fd) {
    char value = 1;
    transfer(fd, &value, 1, true, Clock::now() + std::chrono::seconds(10));
}
void checkpoint(std::string_view point, const std::filesystem::path &root, std::string_view id) {
    if (auto p = hit(point, root, id)) {
        if (p->action == Action::pause)
            gate(*p, id);
        else if (p->action == Action::obstruct_publication && point == "outcome-rename-before")
            std::filesystem::create_directory(root / "objects" / std::string(id) / "run.outcome");
        else
            fail(Status::io, "Injected " + std::string(point) + ": " + std::strerror(p->error),
                 "storage-fault");
    }
}
CommitGuard::CommitGuard(std::string_view point, const std::filesystem::path &root, std::string_view id,
                         sqlite3 *db) {
    auto p = hit(point, root, id);
    if (!p)
        return;
    if (p->action == Action::pause) {
        gate(*p, id);
        return;
    }
    io(p->action == Action::reject_commit, "Invalid commit injection");
    db_ = db;
    sqlite3_commit_hook(
        db_,
        [](void *ctx) {
            auto &self = *static_cast<CommitGuard *>(ctx);
            self.rejected_ = true;
            return 1;
        },
        this);
    // This is the real SQLite commit mechanism: returning nonzero rolls the
    // current transaction back. The guard is removed before outer error cleanup.
    std::lock_guard lock(mutex);
    for (size_t i = 0; i < slots.size(); ++i)
        if (slots[i] && slots[i]->consumed && slots[i]->plan.root == root && slots[i]->plan.point == point &&
            (slots[i]->plan.id.empty() || slots[i]->plan.id == id))
            slot_ = i;
}
CommitGuard::~CommitGuard() {
    if (db_)
        sqlite3_commit_hook(db_, nullptr, nullptr);
    if (rejected_) {
        std::lock_guard lock(mutex);
        if (slot_ < slots.size() && slots[slot_])
            ++slots[slot_]->metrics.rejected_commits;
    }
}
class Output::Buffer final : public std::streambuf {
    std::filebuf file_;
    std::filesystem::path path_;
    bool failed_{};

  protected:
    std::streamsize xsputn(const char *bytes, std::streamsize count) override {
        if (failed_ || count < 0)
            return 0;
        auto allowed = allowance(path_, static_cast<size_t>(count));
        auto n = file_.sputn(bytes, static_cast<std::streamsize>(allowed.bytes));
        if (allowed.failed) {
            failed_ = true;
            io(file_.pubsync() == 0, "Injected partial bytes could not be flushed");
            if (allowed.pause)
                gate(*allowed.pause, path_.parent_path().filename().string());
        }
        return n;
    }
    int_type overflow(int_type value) override {
        if (traits_type::eq_int_type(value, traits_type::eof()))
            return traits_type::not_eof(value);
        auto byte = traits_type::to_char_type(value);
        return xsputn(&byte, 1) == 1 ? value : traits_type::eof();
    }
    int sync() override {
        return file_.pubsync();
    }
    pos_type seekoff(off_type off, std::ios::seekdir dir, std::ios::openmode mode) override {
        return file_.pubseekoff(off, dir, mode);
    }
    pos_type seekpos(pos_type p, std::ios::openmode mode) override {
        return file_.pubseekpos(p, mode);
    }

  public:
    bool open(const std::filesystem::path &p, std::ios::openmode mode) {
        path_ = p;
        failed_ = false;
        return file_.open(p, mode | std::ios::out) != nullptr;
    }
    bool close() {
        return file_.close() != nullptr;
    }
    bool is_open() const {
        return file_.is_open();
    }
};
Output::Output() : std::ostream(nullptr), buffer_(std::make_unique<Buffer>()) {
    rdbuf(buffer_.get());
}
Output::Output(const std::filesystem::path &p, std::ios::openmode mode) : Output() {
    open(p, mode);
}
Output::~Output() = default;
void Output::open(const std::filesystem::path &p, std::ios::openmode mode) {
    if (!buffer_->open(p, mode))
        setstate(std::ios::failbit);
    else
        clear();
}
void Output::close() {
    if (!buffer_->close())
        setstate(std::ios::failbit);
}
bool Output::is_open() const {
    return buffer_->is_open();
}
} // namespace mantis::artifact::storage_test

extern "C" int __real_fsync(int);
extern "C" int __wrap_fsync(int fd) {
    using namespace mantis::artifact::storage_test;
    std::array<char, 64> descriptor{};
    auto count = std::snprintf(descriptor.data(), descriptor.size(), "/proc/self/fd/%d", fd);
    std::array<char, 4097> name{};
    if (count > 0 && static_cast<size_t>(count) < descriptor.size()) {
        auto n = ::readlink(descriptor.data(), name.data(), name.size() - 1);
        if (n > 0 && static_cast<size_t>(n) < name.size() - 1) {
            std::filesystem::path path(std::string(name.data(), static_cast<size_t>(n)));
            struct stat info{};
            if (::fstat(fd, &info) == 0) {
                bool directory = S_ISDIR(info.st_mode);
                auto id = (directory ? path : path.parent_path()).filename().string();
                auto root = directory ? path.parent_path().parent_path()
                                      : path.parent_path().parent_path().parent_path();
                auto filename = path.filename().string();
                auto point = directory                        ? "directory-sync"
                             : filename == "run.header.part"  ? "header-file-sync"
                             : filename == "run.outcome.part" ? "outcome-file-sync"
                                                              : "segment-file-sync";
                if (auto plan = hit(point, root, id)) {
                    if (plan->action == Action::syscall_error) {
                        errno = plan->error;
                        return -1;
                    }
                    mantis::fail(mantis::Status::invalid_argument, "Invalid fsync fault plan");
                }
            }
        }
    }
    return __real_fsync(fd);
}
