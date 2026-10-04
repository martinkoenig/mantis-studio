#include "backend.hpp"
#include <condition_variable>
#include <mutex>
#include <stdexcept>
namespace x1 {
namespace {
class FakeCamera final : public Camera {
    Mode mode_;
    std::string scenario_;
    bool right_{}, running_{};
    uint32_t sequence_{};
    std::chrono::steady_clock::time_point due_;
    std::mutex mutex_;
    std::condition_variable ready_;
  public:
    FakeCamera(Mode mode, std::string scenario, bool right)
        : mode_(std::move(mode)), scenario_(std::move(scenario)), right_(right) {}
    void start() override { running_ = true; sequence_ = 0; due_ = std::chrono::steady_clock::now(); }
    void stop() noexcept override { running_ = false; ready_.notify_all(); }
    bool next(uint32_t timeout, const std::function<void(const FrameView &)> &emit) override {
        if (!running_) throw std::runtime_error("Camera stopped");
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout);
        if ((scenario_ == "stall-left" && !right_) || (scenario_ == "stall-right" && right_) ||
            (scenario_ == "stop-right" && right_ && sequence_ >= 4)) {
            std::unique_lock lock(mutex_); ready_.wait_until(lock, deadline); return false;
        }
        if (scenario_ == "eagain" && sequence_ % 2 == 0) {
            // Still waits for the next readiness deadline; does not busy-spin.
            std::unique_lock lock(mutex_);
            if (ready_.wait_until(lock, std::min(deadline, due_)) == std::cv_status::timeout &&
                std::chrono::steady_clock::now() < due_) return false;
        }
        if (std::chrono::steady_clock::now() < due_) {
            std::unique_lock lock(mutex_); ready_.wait_until(lock, std::min(deadline, due_));
            if (std::chrono::steady_clock::now() < due_) return false;
        }
        if (sequence_ == 4 && scenario_ == "disconnect" && right_) throw std::runtime_error("Fake camera disconnected");
        uint32_t native = sequence_;
        if (sequence_ >= 4 && ((scenario_ == "drop-left" && !right_) || (scenario_ == "drop-right" && right_))) ++native;
        if (scenario_ == "mismatch" && right_) ++native;
        if (scenario_ == "repeat" && sequence_ == 4 && right_) --native;
        auto timestamp = int64_t(native) * (1000000000 / mode_.fps);
        if (scenario_ == "timestamp-jump" && sequence_ == 4 && right_) timestamp = -1;
        if (scenario_ == "lag" && right_) timestamp += 100000000;
        std::vector<std::byte> pixels(size_t(mode_.width) * mode_.height);
        for (size_t i = 0; i < pixels.size(); ++i)
            pixels[i] = static_cast<std::byte>((i + native * 7u + (right_ ? 97u : 0u)) & 255u);
        FrameView view{pixels, native, mode_.width, mode_.height, mode_.width,
                       static_cast<uint32_t>(pixels.size()), 0, timestamp,
                       timestamp + 1000 + (right_ ? 100 : 0), "org.mantis.fake.monotonic", mode_.fourcc, {}};
        emit(view);
        ++sequence_;
        due_ += std::chrono::nanoseconds(1000000000 / mode_.fps);
        return true;
    }
};
class FakeBackend final : public Backend {
    std::string scenario_;
  public:
    explicit FakeBackend(std::string scenario) : scenario_(std::move(scenario)) {}
    std::vector<CameraInfo> discover() override {
        std::vector<CameraInfo> out{{"ov9281 18-0060", "fixture", "/dev/video42", {"GREY", "Y10P"}},
                                    {"ov9281 20-0060", "fixture", "/dev/video7", {"GREY", "Y10P"}}};
        if (scenario_ == "renumber") std::swap(out[0].video, out[1].video);
        if (scenario_ == "missing") out.pop_back();
        if (scenario_ == "ambiguous") { auto c = out.front(); c.video = "/dev/video99"; out.push_back(c); }
        if (scenario_ == "unsupported") out[0].formats = {"Y10P"};
        return out;
    }
    std::unique_ptr<Camera> open(const CameraInfo &info, const Mode &mode) override {
        auto selected = mode;
        if (scenario_ == "stream-mismatch" && info.sensor == "ov9281 20-0060") ++selected.width;
        return std::make_unique<FakeCamera>(selected, scenario_, info.sensor == "ov9281 20-0060");
    }
};
}
std::unique_ptr<Backend> fake_backend(const std::string &scenario) { return std::make_unique<FakeBackend>(scenario); }
} // namespace x1
