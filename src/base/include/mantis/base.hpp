#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
namespace mantis {
enum class Status {
    ok,
    invalid_argument,
    not_found,
    incompatible,
    cancelled,
    io,
    busy,
    plugin_failed,
    unsupported,
    corrupt
};
struct Error {
    Status code;
    std::string message;
    std::string component;
};
template <class T> using Result = std::expected<T, Error>;
class Failure : public std::runtime_error {
  public:
    Error error;
    explicit Failure(Error e) : std::runtime_error(e.message), error(std::move(e)) {}
};
[[noreturn]] inline void fail(Status code, std::string message, std::string component = {}) {
    throw Failure({code, std::move(message), std::move(component)});
}
struct Id {
    std::string value;
    auto operator<=>(const Id &) const = default;
    static Id random();
};
struct SemanticVersion {
    uint32_t major{}, minor{}, patch{};
    auto operator<=>(const SemanticVersion &) const = default;
};
struct Hash {
    std::string algorithm;
    std::string hex;
    auto operator<=>(const Hash &) const = default;
};
Hash content_hash(std::span<const std::byte> bytes);
class CancellationToken {
    std::shared_ptr<std::atomic_bool> flag_ = std::make_shared<std::atomic_bool>(false);

  public:
    bool cancelled() const noexcept {
        return flag_->load();
    }
    void cancel() const noexcept {
        flag_->store(true);
    }
    void check() const {
        if (cancelled())
            fail(Status::cancelled, "Operation cancelled");
    }
};
struct LogRecord {
    std::string level, component, message;
};
using LogSink = std::function<void(const LogRecord &)>;
inline constexpr SemanticVersion application_version{0, 2, 0};
inline constexpr std::string_view build_version = "Mantis Studio Acquisition Foundation 0.2.0-dev";
} // namespace mantis
