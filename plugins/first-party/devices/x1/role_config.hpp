#pragma once
// Private first-party configuration plumbing; not a Plugin or Client SDK interface.
#include <filesystem>
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>
namespace x1::roles {
inline constexpr size_t max_bytes = 65536;
struct Error : std::runtime_error {
    bool uncertain;
    explicit Error(std::string message, bool indeterminate = false)
        : std::runtime_error(std::move(message)), uncertain(indeterminate) {}
};
struct Mapping {
    uint64_t generation{};
    bool swapped{};
    friend bool operator==(const Mapping &, const Mapping &) = default;
};
std::string digest(std::string_view);
std::string read_profile(const std::filesystem::path &);
void validate_base(const nlohmann::json &);
// Unset override returns exactly the base profile. Present invalid state never falls back.
nlohmann::json effective(const nlohmann::json &, Mapping);
struct Resolved {
    nlohmann::json profile;
    Mapping mapping;
    std::string base_digest;
};
Resolved resolve(const std::filesystem::path &directory, std::string_view base);
class Store {
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:
    using Checkpoint = std::function<void(std::string_view)>;
    Store(const std::filesystem::path &directory, const std::filesystem::path &base, Checkpoint = {});
    ~Store();
    Store(const Store &) = delete;
    Store &operator=(const Store &) = delete;
    Mapping mapping() const;
    const nlohmann::json &base() const;
    const std::string &base_digest() const;
    // Called only on a Job worker. On uncertain rollback the caller must quarantine admission.
    void apply(Mapping expected, bool desired, const std::function<void(Mapping)> &refresh);
};
} // namespace x1::roles
