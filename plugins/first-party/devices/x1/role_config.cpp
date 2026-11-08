#include "role_config.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <exception>
#include <fstream>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <system_error>
#ifdef __linux__
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
namespace x1::roles {
using Json = nlohmann::json;
namespace {
std::string failure_detail(std::exception_ptr failure) {
    try {
        std::rethrow_exception(failure);
    } catch (const std::exception &error) {
        const std::string_view text(error.what());
        size_t length = std::min<size_t>(text.size(), 384);
        while (length && length < text.size() && (static_cast<unsigned char>(text[length]) & 0xc0) == 0x80)
            --length;
        return std::string(text.substr(0, length)) + (length < text.size() ? " [truncated]" : "");
    } catch (...) {
        return "Non-standard exception";
    }
}
Json parse(std::string_view bytes) {
    std::vector<std::set<std::string>> keys;
    return Json::parse(bytes, [&](int, Json::parse_event_t event, Json &value) {
        if (event == Json::parse_event_t::object_start)
            keys.emplace_back();
        else if (event == Json::parse_event_t::key && !keys.back().insert(value.get<std::string>()).second)
            throw Error("Duplicate key in X1 role/base configuration");
        else if (event == Json::parse_event_t::object_end)
            keys.pop_back();
        return true;
    });
}
} // namespace
std::string digest(std::string_view bytes) {
    // Repository content-identity convention. Trust comes from directory ownership, not this checksum.
    uint64_t value = 14695981039346656037ull;
    for (unsigned char c : bytes)
        value = (value ^ c) * 1099511628211ull;
    std::ostringstream out;
    out << "fnv1a64:" << std::hex << std::setfill('0') << std::setw(16) << value;
    return out.str();
}
std::string read_profile(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw Error("Cannot read configured X1 base profile");
    std::string bytes(max_bytes + 1, '\0');
    in.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    bytes.resize(static_cast<size_t>(in.gcount()));
    if (in.bad() || bytes.empty() || bytes.size() > max_bytes)
        throw Error("X1 base profile read failed or exceeds 64 KiB");
    return bytes;
}
void validate_base(const Json &base) {
    if (!base.at("measurement_cameras").is_object() || base.at("measurement_cameras").size() != 2 ||
        base.at("format_version") != 2 || base.at("runtime_setup").at("ownership") != "selected-routes" ||
        !base.value("calibration_id", std::string{}).empty() ||
        base.value("calibration_revision", uint64_t{}) != 0)
        throw Error("Role override requires profile2 selected-routes without a base calibration stamp");
    std::array<std::string, 2> identities;
    size_t i = 0;
    for (const char *role : {"left", "right"}) {
        const auto &c = base.at("measurement_cameras").at(role);
        const auto bus = c.at("bus_identity").get<std::string>();
        const auto sensor = c.at("sensor_identity").get<std::string>();
        const auto route = c.at("route").get<std::vector<std::string>>();
        if (bus.empty() || sensor.empty() || route.size() < 2 || route.size() > 16 || route.front() != sensor)
            throw Error("Role override requires exact bus, sensor and route identities");
        std::set<std::string> unique;
        for (const auto &entity : route)
            if (!unique.insert(entity).second || entity.empty() || entity.find('\0') != std::string::npos ||
                entity.size() > 256)
                throw Error("Invalid X1 route identity");
        if (bus.size() > 256 || sensor.size() > 256 || bus.find('\0') != std::string::npos ||
            sensor.find('\0') != std::string::npos)
            throw Error("Invalid X1 physical identity");
        identities[i++] = Json::array({bus, sensor}).dump();
    }
    if (identities[0] == identities[1])
        throw Error("Role override requires two distinct physical cameras");
}
Json effective(const Json &base, Mapping mapping) {
    auto profile = base;
    if (mapping.swapped)
        std::swap(profile["measurement_cameras"]["left"], profile["measurement_cameras"]["right"]);
    return profile;
}
namespace {
Json document(const Json &base, std::string_view hash, Mapping mapping) {
    return {{"format_version", 1},
            {"plugin_id", "org.mantis.x1"},
            {"base_digest", hash},
            {"pair", base.at("measurement_cameras")},
            {"generation", mapping.generation},
            {"swapped", mapping.swapped}};
}
Mapping decode(const std::optional<std::string> &bytes, const Json &base, std::string_view hash) {
    if (!bytes)
        return {};
    auto j = parse(*bytes);
    if (!j.is_object() || j.size() != 6 || j.at("plugin_id") != "org.mantis.x1" ||
        !j.at("format_version").is_number_unsigned() || j.at("format_version") != 1 ||
        j.at("base_digest") != hash || j.at("pair") != base.at("measurement_cameras") ||
        !j.at("generation").is_number_unsigned() || !j.at("swapped").is_boolean())
        throw Error("Invalid, unsupported or stale X1 role override");
    auto generation = j.at("generation").get<uint64_t>();
    if (!generation)
        throw Error("Persisted role generation must be positive");
    return {generation, j.at("swapped").get<bool>()};
}
#ifdef __linux__
constexpr const char *payload = "x1-role-map.v1.json";
constexpr const char *temporary = ".x1-role-map.v1.tmp";
struct Fd {
    int value{-1};
    explicit Fd(int fd = -1) : value(fd) {}
    ~Fd() {
        if (value >= 0)
            ::close(value);
    }
    Fd(const Fd &) = delete;
    Fd &operator=(const Fd &) = delete;
};
[[noreturn]] void io(const char *phase) {
    throw Error(std::string("X1 role configuration ") + phase + ": " +
                std::generic_category().message(errno));
}
void regular(int fd) {
    struct stat st {};
    if (::fstat(fd, &st))
        io("stat");
    if (!S_ISREG(st.st_mode) || st.st_uid != ::geteuid() || (st.st_mode & 0077) || st.st_nlink != 1)
        throw Error("Unsafe X1 role configuration file ownership/type/permissions");
}
int directory(const std::filesystem::path &path) {
    if (!path.is_absolute() || path.filename().empty())
        throw Error("X1 role state directory must be an explicit absolute provisioned leaf");
    int current = ::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (current < 0)
        io("open root");
    try {
        for (const auto &part : path.relative_path()) {
            if (part == "." || part == "..")
                throw Error("X1 role state directory traversal is forbidden");
            int next = ::openat(current, part.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            if (next < 0)
                io("open directory");
            ::close(current);
            current = next;
            struct stat st {};
            if (::fstat(current, &st))
                io("stat directory");
            const bool root_sticky = st.st_uid == 0 && (st.st_mode & S_ISVTX);
            if ((st.st_uid != 0 && st.st_uid != ::geteuid()) || ((st.st_mode & 0022) && !root_sticky))
                throw Error("Untrusted X1 role state directory ancestry");
        }
        struct stat leaf {};
        if (::fstat(current, &leaf))
            io("stat leaf");
        if (leaf.st_uid != ::geteuid() || (leaf.st_mode & 0077))
            throw Error("X1 role state leaf must be daemon-owned and private");
        return current;
    } catch (...) {
        ::close(current);
        throw;
    }
}
std::optional<std::string> read(int root) {
    Fd fd(::openat(root, payload, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
    if (fd.value < 0) {
        if (errno == ENOENT)
            return {};
        io("open payload");
    }
    regular(fd.value);
    std::string bytes;
    std::array<char, 4096> buffer{};
    while (bytes.size() <= max_bytes) {
        auto count = ::read(fd.value, buffer.data(), buffer.size());
        if (count < 0) {
            if (errno == EINTR)
                continue;
            io("read payload");
        }
        if (!count)
            break;
        bytes.append(buffer.data(), static_cast<size_t>(count));
    }
    if (bytes.empty() || bytes.size() > max_bytes)
        throw Error("X1 override is empty or exceeds 64 KiB");
    return bytes;
}
void replace(int root, const std::optional<std::string> &bytes, const Store::Checkpoint &hook) {
    const auto point = [&](std::string_view phase) {
        if (hook)
            hook(phase);
    };
    if (!bytes) {
        if (::unlinkat(root, payload, 0) && errno != ENOENT)
            io("rollback unlink");
    } else {
        // A stale/foreign temporary is never followed, overwritten or automatically removed.
        Fd fd(::openat(root, temporary, O_CREAT | O_EXCL | O_WRONLY | O_NOFOLLOW | O_CLOEXEC, 0600));
        if (fd.value < 0)
            io("create temporary");
        bool renamed = false;
        try {
            point("write");
            size_t offset = 0;
            while (offset < bytes->size()) {
                auto n =
                    ::write(fd.value, bytes->data() + offset, std::min<size_t>(4096, bytes->size() - offset));
                if (n < 0 && errno == EINTR)
                    continue;
                if (n <= 0)
                    io("write temporary");
                offset += static_cast<size_t>(n);
                point("write-progress");
            }
            point("file-sync");
            if (::fsync(fd.value))
                io("sync temporary");
            point("rename");
            if (::renameat(root, temporary, root, payload))
                io("replace payload");
            renamed = true;
            point("renamed");
        } catch (...) {
            if (!renamed)
                (void)::unlinkat(root, temporary, 0);
            throw;
        }
    }
    point("directory-sync");
    if (::fsync(root))
        io("sync directory");
}
#endif
} // namespace
Resolved resolve(const std::filesystem::path &path, std::string_view bytes) {
    auto base = parse(bytes);
    validate_base(base);
#ifdef __linux__
    Fd root(directory(path));
    auto hash = digest(bytes);
    auto mapping = decode(read(root.value), base, hash);
    return {effective(base, mapping), mapping, hash};
#else
    (void)path;
    throw Error("X1 role overrides are supported only on Linux");
#endif
}
struct Store::Impl {
    std::filesystem::path profile_path;
    std::string profile_bytes, hash;
    Json base;
    Mapping current;
    Checkpoint hook;
#ifdef __linux__
    Fd root, lock;
    Impl(const std::filesystem::path &dir, const std::filesystem::path &profile, Checkpoint checkpoint)
        : profile_path(profile), profile_bytes(read_profile(profile)), hash(digest(profile_bytes)),
          base(parse(profile_bytes)), hook(std::move(checkpoint)), root(directory(dir)),
          lock(::openat(root.value, "x1-role-map.lock",
                        O_CREAT | O_RDWR | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC, 0600)) {
        validate_base(base);
        if (lock.value < 0)
            io("open owner lock");
        regular(lock.value);
        if (::flock(lock.value, LOCK_EX | LOCK_NB))
            throw Error("X1 role configuration already has an authority");
        current = decode(read(root.value), base, hash);
    }
#else
    Impl(const std::filesystem::path &, const std::filesystem::path &, Checkpoint) {
        throw Error("X1 role overrides are supported only on Linux");
    }
#endif
};
Store::Store(const std::filesystem::path &dir, const std::filesystem::path &profile, Checkpoint hook)
    : impl_(std::make_unique<Impl>(dir, profile, std::move(hook))) {}
Store::~Store() = default;
Mapping Store::mapping() const {
    return impl_->current;
}
const Json &Store::base() const {
    return impl_->base;
}
const std::string &Store::base_digest() const {
    return impl_->hash;
}
void Store::apply(Mapping expected, bool desired, const std::function<void(Mapping)> &refresh) {
#ifdef __linux__
    auto &s = *impl_;
    if (read_profile(s.profile_path) != s.profile_bytes)
        throw Error("X1 base profile changed; restart/review required");
    auto previous = read(s.root.value);
    if (decode(previous, s.base, s.hash) != expected || s.current != expected)
        throw Error("Stale X1 role configuration generation");
    if (desired == expected.swapped) {
        refresh(expected);
        return;
    }
    if (expected.generation == std::numeric_limits<uint64_t>::max())
        throw Error("X1 role generation exhausted");
    Mapping next{expected.generation + 1, desired};
    auto bytes = document(s.base, s.hash, next).dump();
    if (bytes.size() > max_bytes)
        throw Error("X1 role configuration exceeds 64 KiB");
    try {
        replace(s.root.value, bytes, s.hook);
        if (decode(read(s.root.value), s.base, s.hash) != next)
            throw Error("X1 role configuration readback mismatch");
        if (s.hook)
            s.hook("refresh");
        refresh(next);
        s.current = next;
    } catch (...) {
        const auto original = std::current_exception();
        try {
            // Restore even after sync/rename uncertainty. Never infer success from an exception phase.
            if (s.hook)
                s.hook("rollback");
            if (read(s.root.value) != previous)
                replace(s.root.value, previous, {});
            if (read(s.root.value) != previous)
                throw Error("X1 role rollback readback mismatch");
            refresh(expected);
        } catch (...) {
            const auto rollback = std::current_exception();
            throw Error(
                "X1 role commit failed: " + failure_detail(original) +
                    "; rollback failed: " + failure_detail(rollback) +
                    "; commit state indeterminate, device admission quarantined until restart/reconciliation",
                true);
        }
        std::rethrow_exception(original);
    }
#else
    (void)expected;
    (void)desired;
    (void)refresh;
    throw Error("X1 role overrides are supported only on Linux");
#endif
}
} // namespace x1::roles
