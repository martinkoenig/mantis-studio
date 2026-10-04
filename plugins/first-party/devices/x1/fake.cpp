#include "media.hpp"
#include <algorithm>
#include <system_error>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
namespace x1 {
namespace {
class FakeCamera final : public Camera {
    Mode mode_;
    std::string scenario_;
    std::string context_;
    bool right_{}, running_{};
    Metadata diagnostics_;
    uint32_t sequence_{};
    std::chrono::steady_clock::time_point due_;
    std::mutex mutex_;
    std::condition_variable ready_;
  public:
    FakeCamera(Mode mode, std::string scenario, bool right, std::string context)
        : mode_(std::move(mode)), scenario_(std::move(scenario)), context_(std::move(context)), right_(right) {}
    void start() override {
        if (scenario_ == (right_ ? "streamon-right" : "streamon-left"))
            throw std::runtime_error(context_ + " STREAMON failed: Broken pipe (errno 32)");
        running_ = true; sequence_ = 0; due_ = std::chrono::steady_clock::now(); }
    void stop() noexcept override {
        if (running_ && scenario_ == (right_ ? "streamoff-right" : "streamoff-left")) {
            try { diagnostics_["streamoff_error"] = context_ + " STREAMOFF failed: Input/output error (errno 5)"; } catch (...) {}
        }
        running_ = false; ready_.notify_all();
    }
    Metadata diagnostics() const override { return diagnostics_; }
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
        uint32_t stride = mode_.fourcc == "Y10P" ? mode_.width / 4 * 5 : mode_.width;
        std::vector<std::byte> pixels(size_t(stride) * mode_.height);
        for (size_t i = 0; i < pixels.size(); ++i)
            pixels[i] = static_cast<std::byte>((i + native * 7u + (right_ ? 97u : 0u)) & 255u);
        FrameView view{pixels, native, mode_.width, mode_.height, stride,
                       static_cast<uint32_t>(pixels.size()), 0, timestamp,
                       timestamp + 1000 + (right_ ? 100 : 0), "org.mantis.fake.monotonic", mode_.fourcc, {}};
        emit(view);
        ++sequence_;
        due_ += std::chrono::nanoseconds(1000000000 / mode_.fps);
        return true;
    }
};
class FakeMedia final : public MediaIo {
    Graph graph_;
    std::map<Endpoint, PadFormat> formats_;
    std::map<uint32_t, int32_t> vblank_;
    std::string scenario_;
    bool injected_{};
  public:
    FakeMedia(const Profile &p, const std::array<CameraInfo, 2> &cameras, std::string scenario) : scenario_(std::move(scenario)) {
        graph_.path = "fixture-media"; graph_.bus = cameras[0].bus;
        for (size_t i = 0; i < 2; ++i) {
            const auto &names = p.routes[i];
            uint32_t previous{};
            for (size_t j = 0; j < names.size(); ++j) {
                auto id = static_cast<uint32_t>(i * 32 + j + 1);
                bool last = j + 1 == names.size();
                Entity e{id, names[j], last ? cameras[i].video : "fixture-subdev-" + std::to_string(id), last, {}};
                if (j) e.pads.push_back({0, false, true});
                if (!last) e.pads.push_back({j ? 1u : 0u, true, false});
                for (const auto &pad : e.pads) if (!last) formats_[{id, pad.index}] = {10, 320, 240, 0, 0, 0, 0, 0, 0};
                if (!j) vblank_[id] = 20;
                graph_.entities.push_back(e);
                if (j) graph_.links.push_back({{previous, j == 1 ? 0u : 1u}, {id, 0}, j == 1, j == 1});
                previous = id;
            }
        }
        // Unrelated RGB route must survive configuration, rollback and shutdown.
        graph_.entities.push_back({100, "fixture-rgb", "rgb-subdev", false, {{0, true, false}}});
        graph_.entities.push_back({101, "fixture-rgb-video", "rgb-video", true, {{0, false, true}}});
        graph_.links.push_back({{100, 0}, {101, 0}, true, false});
        if (scenario_ == "conflict" || scenario_ == "immutable-conflict")
            graph_.links.push_back({{100, 0}, {3, 0}, true, scenario_ == "immutable-conflict"});
        if (scenario_ == "source-conflict" || scenario_ == "fanout") graph_.links.push_back({{2, 1}, {101, 0}, true, false});
        if (scenario_ == "missing-route") graph_.entities.erase(graph_.entities.begin() + 1);
        if (scenario_ == "ambiguous-route") { auto e = graph_.entities[1]; e.id = 200; graph_.entities.push_back(e); }
    }
    Graph graph(const std::string &) override { return graph_; }
    void link(const std::string &, const Link &l, bool enabled) override {
        auto it = std::find_if(graph_.links.begin(), graph_.links.end(), [&](const Link &v) { return v.source == l.source && v.sink == l.sink; });
        if (it == graph_.links.end()) throw std::runtime_error("Fake missing link");
        if (it->immutable && it->enabled != enabled) throw std::runtime_error("Immutable link");
        if (enabled && scenario_ == "source-conflict" && std::any_of(graph_.links.begin(), graph_.links.end(), [&](const Link &v) {
                return v.enabled && v.source == l.source && v.sink != l.sink; }))
            throw std::system_error(EBUSY, std::generic_category(), "MEDIA_IOC_SETUP_LINK");
        if (enabled && !injected_ && scenario_ == "link-readback-mismatch") { injected_ = true; return; }
        it->enabled = enabled;
    }
    uint32_t format_code(const std::string &name) override { return name == "Y10_1X10" ? 10 : 8; }
    std::vector<uint32_t> codes(const Entity &, uint32_t) override { return scenario_ == "unsupported-mbus" ? std::vector<uint32_t>{8} : std::vector<uint32_t>{8, 10}; }
    PadFormat get_format(const Entity &e, uint32_t pad) override {
        auto f = formats_.at({e.id, pad});
        if (!injected_ && scenario_ == "pad-readback-failure" && f.width != 320) {
            injected_ = true; throw std::system_error(EIO, std::generic_category(), "SUBDEV_G_FMT");
        }
        return f;
    }
    PadFormat set_format(const Entity &e, uint32_t pad, const PadFormat &f) override {
        if (scenario_ == "rollback-failure" && ((!injected_ && e.id >= 32) || (injected_ && f.width == 320 && e.id < 32))) {
            injected_ = true; throw std::system_error(EIO, std::generic_category(), "SUBDEV_S_FMT (rollback fixture)");
        }
        if (!injected_ && ((scenario_ == "setup-left" && e.id < 32) || (scenario_ == "setup-right" && e.id >= 32))) {
            injected_ = true; throw std::system_error(EIO, std::generic_category(), "SUBDEV_S_FMT");
        }
        auto actual = f;
        if (!injected_ && scenario_ == "format-readback-mismatch") { ++actual.width; injected_ = true; }
        formats_[{e.id, pad}] = actual; return actual;
    }
    int32_t get_vblank(const Entity &e) override { return vblank_.at(e.id); }
    int32_t set_vblank(const Entity &e, int32_t value) override {
        if (!injected_ && scenario_ == "vblank-readback-mismatch") { ++value; injected_ = true; }
        if (!injected_ && scenario_ == "late-route-mismatch" && e.id >= 32 && value == 196) {
            graph_.links[1].enabled = false; injected_ = true;
        }
        vblank_[e.id] = value; return value;
    }
    Metadata timing(const Entity &) override { return {{"sensor_driver_interval", "unavailable"}}; }
};
class FakeBackend final : public Backend {
    std::string scenario_;
    std::unique_ptr<MediaIo> media_;
  public:
    explicit FakeBackend(std::string scenario) : scenario_(std::move(scenario)) {}
    std::vector<CameraInfo> discover() override {
        std::vector<CameraInfo> out{{"ov9281 18-0060", "fixture", "/dev/video42", {"GREY", "Y10P"}},
                                    {"ov9281 20-0060", "fixture", "/dev/video7", {"GREY", "Y10P"}}};
        if (scenario_ == "renumber") std::swap(out[0].video, out[1].video);
        if (scenario_ == "missing") out.pop_back();
        if (scenario_ == "ambiguous") { auto c = out.front(); c.video = "/dev/video99"; out.push_back(c); }
        if (scenario_ == "unsupported") out[0].formats = {"Y10P"};
        if (scenario_ == "y10p-unsupported") out[0].formats = {"GREY"};
        return out;
    }
    std::vector<CameraInfo> discover(const Profile &p) override {
        auto out = discover();
        if (p.format_version == 2) {
            for (auto &c : out) for (size_t i = 0; i < 2; ++i) if (c.sensor == p.sensors[i]) {
                c.media = "fixture-media"; c.route = p.routes[i]; c.role = i ? "RIGHT" : "LEFT";
            }
            auto cameras = assign(p, out);
            media_ = fake_media(p, cameras, scenario_);
            auto g = media_->graph("fixture-media");
            for (size_t i = 0; i < 2; ++i) (void)select_route(g, p.routes[i], cameras[i].role);
        }
        return out;
    }
    std::unique_ptr<Setup> prepare(const Profile &p, const std::array<CameraInfo, 2> &cameras) override {
        if (p.format_version == 1) return {};
        media_ = fake_media(p, cameras, scenario_);
        return configure_media(*media_, p, cameras);
    }
    std::unique_ptr<Camera> open(const CameraInfo &info, const Mode &mode) override {
        auto selected = mode;
        if (scenario_ == "stream-mismatch" && info.sensor == "ov9281 20-0060") ++selected.width;
        return std::make_unique<FakeCamera>(selected, scenario_, info.sensor == "ov9281 20-0060", camera_context(info));
    }
};
}
std::unique_ptr<MediaIo> fake_media(const Profile &p, const std::array<CameraInfo, 2> &cameras, const std::string &scenario) {
    return std::make_unique<FakeMedia>(p, cameras, scenario);
}
std::unique_ptr<Backend> fake_backend(const std::string &scenario) { return std::make_unique<FakeBackend>(scenario); }
} // namespace x1
