#pragma once
// Private plugin implementation seam. No Linux types cross the public SDK.
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>
namespace x1 {
using Metadata = std::map<std::string, std::string>;
struct Mode {
    uint32_t width{1280}, height{800}, fps{120};
    std::string fourcc{"GREY"};
};
struct CameraInfo {
    std::string sensor, bus, video;
    std::vector<std::string> formats;
};
struct Profile {
    std::array<std::string, 2> sensors;
    std::array<std::string, 2> buses;
    Mode mode;
    bool hardware_sync_configured{};
    uint32_t stall_ms{1000};
    int64_t max_timestamp_delta_ns{4000000};
    std::string calibration_id;
    uint64_t calibration_revision{};
    std::string json;
};
Profile load_profile(const std::string &path);
std::array<CameraInfo, 2> assign(const Profile &, const std::vector<CameraInfo> &);
struct FrameView {
    std::span<const std::byte> bytes;
    uint32_t sequence{}, width{}, height{}, stride{}, buffer_size{}, flags{};
    int64_t timestamp_ns{}, received_ns{};
    std::string clock, fourcc;
    Metadata controls;
};
class Camera {
  public:
    virtual ~Camera() = default;
    virtual void start() = 0;
    virtual void stop() noexcept = 0;
    // Pixel lifetime ends at callback return; production QBUFs after the callback.
    virtual bool next(uint32_t timeout_ms, const std::function<void(const FrameView &)> &) = 0;
};
class Backend {
  public:
    virtual ~Backend() = default;
    virtual std::vector<CameraInfo> discover() = 0;
    virtual std::unique_ptr<Camera> open(const CameraInfo &, const Mode &) = 0;
};
std::unique_ptr<Backend> linux_backend();
// Explicit test fixture only, enabled by MANTIS_X1_FAKE. Never automatic fallback.
std::unique_ptr<Backend> fake_backend(const std::string &scenario = "normal");
int64_t monotonic_ns();
} // namespace x1
