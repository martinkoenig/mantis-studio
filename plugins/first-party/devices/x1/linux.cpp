#include "backend.hpp"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#include <linux/media.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <set>
#include <stdexcept>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>
namespace x1 {
namespace {
struct Fd {
    int value{-1};
    explicit Fd(const std::string &path) : value(::open(path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC)) {
        if (value < 0) throw std::runtime_error("open " + path + ": " + std::strerror(errno));
    }
    ~Fd() { if (value >= 0) ::close(value); }
    Fd(const Fd &) = delete;
};
int call(int fd, unsigned long request, void *arg) {
    int rc;
    do { rc = ::ioctl(fd, request, arg); } while (rc < 0 && errno == EINTR);
    return rc;
}
void checked(int fd, unsigned long request, void *arg, const char *name) {
    if (call(fd, request, arg) < 0) throw std::runtime_error(std::string(name) + ": " + std::strerror(errno));
}
uint32_t code(const std::string &s) {
    if (s.size() != 4) throw std::runtime_error("fourcc must have four bytes");
    return v4l2_fourcc(s[0], s[1], s[2], s[3]);
}
std::string fourcc(uint32_t n) {
    std::string out(4, ' ');
    for (unsigned i = 0; i < 4; ++i) out[i] = static_cast<char>((n >> (i * 8)) & 255u);
    return out;
}
v4l2_buf_type capture_type(int fd) {
    v4l2_capability caps{};
    checked(fd, VIDIOC_QUERYCAP, &caps, "QUERYCAP");
    auto flags = (caps.capabilities & V4L2_CAP_DEVICE_CAPS) ? caps.device_caps : caps.capabilities;
    if (!(flags & V4L2_CAP_STREAMING)) throw std::runtime_error("V4L2 streaming unavailable");
    if (flags & V4L2_CAP_VIDEO_CAPTURE_MPLANE) return V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (flags & V4L2_CAP_VIDEO_CAPTURE) return V4L2_BUF_TYPE_VIDEO_CAPTURE;
    throw std::runtime_error("Not a capture node");
}
std::vector<std::string> formats(int fd, v4l2_buf_type type) {
    std::vector<std::string> out;
    for (uint32_t i = 0; i < 256; ++i) {
        v4l2_fmtdesc f{}; f.index = i; f.type = type;
        if (call(fd, VIDIOC_ENUM_FMT, &f) < 0) {
            if (errno == EINVAL) break;
            throw std::runtime_error("ENUM_FMT failed");
        }
        if (!(f.flags & V4L2_FMT_FLAG_COMPRESSED)) out.push_back(fourcc(f.pixelformat));
    }
    return out;
}
class LinuxCamera final : public Camera {
    Fd fd_;
    v4l2_buf_type type_;
    struct Mapping { void *address; size_t size; };
    std::vector<Mapping> mappings_;
    uint32_t width_{}, height_{}, stride_{}, size_{};
    std::string format_;
    bool streaming_{};
    Metadata controls_;
    void cleanup() noexcept {
        stop();
        for (auto &m : mappings_) ::munmap(m.address, m.size);
        mappings_.clear();
    }
    void buffer(v4l2_buffer &b, v4l2_plane &plane, uint32_t index) const {
        b = {}; plane = {}; b.type = type_; b.memory = V4L2_MEMORY_MMAP; b.index = index;
        if (type_ == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) { b.m.planes = &plane; b.length = 1; }
    }
  public:
    LinuxCamera(const CameraInfo &info, const Mode &mode) : fd_(info.video), type_(capture_type(fd_.value)) {
        try {
            auto available = formats(fd_.value, type_);
            if (std::find(available.begin(), available.end(), mode.fourcc) == available.end())
                throw std::runtime_error("Requested RAW8 fourcc unavailable");
            // Query enumerated modes where the driver provides them. S_FMT below is
            // authoritative for MC-centric drivers without ENUM_FRAMESIZES.
            bool has_sizes = false, size_matches = false;
            for (uint32_t i = 0; i < 256; ++i) {
                v4l2_frmsizeenum f{}; f.index = i; f.pixel_format = code(mode.fourcc);
                if (call(fd_.value, VIDIOC_ENUM_FRAMESIZES, &f) < 0) {
                    if (errno == EINVAL || errno == ENOTTY) break;
                    throw std::runtime_error("ENUM_FRAMESIZES failed");
                }
                has_sizes = true;
                if (f.type == V4L2_FRMSIZE_TYPE_DISCRETE)
                    size_matches |= f.discrete.width == mode.width && f.discrete.height == mode.height;
                else {
                    const auto &s = f.stepwise;
                    size_matches |= mode.width >= s.min_width && mode.width <= s.max_width &&
                        mode.height >= s.min_height && mode.height <= s.max_height &&
                        (mode.width - s.min_width) % std::max(1u, s.step_width) == 0 &&
                        (mode.height - s.min_height) % std::max(1u, s.step_height) == 0;
                    break;
                }
            }
            if (has_sizes && !size_matches) throw std::runtime_error("Requested camera resolution unavailable");
            v4l2_format f{}; f.type = type_;
            if (type_ == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
                f.fmt.pix_mp.width = mode.width; f.fmt.pix_mp.height = mode.height;
                f.fmt.pix_mp.pixelformat = code(mode.fourcc); f.fmt.pix_mp.field = V4L2_FIELD_NONE;
            } else {
                f.fmt.pix.width = mode.width; f.fmt.pix.height = mode.height;
                f.fmt.pix.pixelformat = code(mode.fourcc); f.fmt.pix.field = V4L2_FIELD_NONE;
            }
            checked(fd_.value, VIDIOC_S_FMT, &f, "S_FMT (configure media links/subdevice formats first)");
            uint32_t pixel;
            if (type_ == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
                if (f.fmt.pix_mp.num_planes != 1) throw std::runtime_error("RAW8 requires one plane");
                width_ = f.fmt.pix_mp.width; height_ = f.fmt.pix_mp.height; pixel = f.fmt.pix_mp.pixelformat;
                stride_ = f.fmt.pix_mp.plane_fmt[0].bytesperline; size_ = f.fmt.pix_mp.plane_fmt[0].sizeimage;
            } else {
                width_ = f.fmt.pix.width; height_ = f.fmt.pix.height; pixel = f.fmt.pix.pixelformat;
                stride_ = f.fmt.pix.bytesperline; size_ = f.fmt.pix.sizeimage;
            }
            format_ = fourcc(pixel);
            if (width_ != mode.width || height_ != mode.height || format_ != mode.fourcc ||
                stride_ < width_ || uint64_t(stride_) * height_ > size_ || size_ > 128 * 1024 * 1024)
                throw std::runtime_error("Driver negotiated an incompatible RAW8 layout");
            bool has_intervals = false, interval_matches = false;
            for (uint32_t i = 0; i < 256; ++i) {
                v4l2_frmivalenum interval{}; interval.index = i; interval.pixel_format = pixel;
                interval.width = width_; interval.height = height_;
                if (call(fd_.value, VIDIOC_ENUM_FRAMEINTERVALS, &interval) < 0) {
                    if (errno == EINVAL || errno == ENOTTY) break;
                    throw std::runtime_error("ENUM_FRAMEINTERVALS failed");
                }
                has_intervals = true;
                if (interval.type == V4L2_FRMIVAL_TYPE_DISCRETE) {
                    interval_matches |= uint64_t(interval.discrete.numerator) * mode.fps == interval.discrete.denominator;
                } else {
                    const auto &range = interval.stepwise;
                    if (!range.min.denominator || !range.max.denominator) throw std::runtime_error("Invalid driver interval range");
                    double requested = 1.0 / mode.fps;
                    interval_matches |= requested >= double(range.min.numerator) / range.min.denominator &&
                        requested <= double(range.max.numerator) / range.max.denominator;
                    break;
                }
            }
            if (has_intervals && !interval_matches) throw std::runtime_error("Requested camera FPS unavailable");
            controls_["interval_enumeration"] = has_intervals ? "supported" : "unavailable";
            v4l2_streamparm parm{}; parm.type = type_;
            if (call(fd_.value, VIDIOC_G_PARM, &parm) == 0 && (parm.parm.capture.capability & V4L2_CAP_TIMEPERFRAME)) {
                parm.parm.capture.timeperframe = {1, mode.fps};
                checked(fd_.value, VIDIOC_S_PARM, &parm, "S_PARM");
                controls_["driver_interval_numerator"] = std::to_string(parm.parm.capture.timeperframe.numerator);
                controls_["driver_interval_denominator"] = std::to_string(parm.parm.capture.timeperframe.denominator);
            }
            // Controls are a start-time snapshot, never fabricated per-exposure values.
            for (auto [id, name] : std::array<std::pair<uint32_t, const char *>, 2>{{
                    {V4L2_CID_EXPOSURE, "initial_exposure_control"}, {V4L2_CID_ANALOGUE_GAIN, "initial_analogue_gain_control"}}}) {
                v4l2_control control{}; control.id = id;
                if (call(fd_.value, VIDIOC_G_CTRL, &control) == 0) controls_[name] = std::to_string(control.value);
            }
            v4l2_requestbuffers request{}; request.type = type_; request.memory = V4L2_MEMORY_MMAP; request.count = 8;
            checked(fd_.value, VIDIOC_REQBUFS, &request, "REQBUFS");
            if (request.count < 2 || request.count > 64) throw std::runtime_error("Invalid V4L2 buffer count");
            for (uint32_t i = 0; i < request.count; ++i) {
                v4l2_buffer b{}; v4l2_plane plane{}; buffer(b, plane, i);
                checked(fd_.value, VIDIOC_QUERYBUF, &b, "QUERYBUF");
                auto length = type_ == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE ? plane.length : b.length;
                auto offset = type_ == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE ? plane.m.mem_offset : b.m.offset;
                if (length < size_) throw std::runtime_error("Driver buffer shorter than sizeimage");
                void *address = ::mmap(nullptr, length, PROT_READ | PROT_WRITE, MAP_SHARED, fd_.value, offset);
                if (address == MAP_FAILED) throw std::runtime_error("V4L2 mmap failed");
                mappings_.push_back({address, length});
                checked(fd_.value, VIDIOC_QBUF, &b, "initial QBUF");
            }
        } catch (...) { cleanup(); throw; }
    }
    ~LinuxCamera() override { cleanup(); }
    void start() override { checked(fd_.value, VIDIOC_STREAMON, &type_, "STREAMON"); streaming_ = true; }
    void stop() noexcept override { if (streaming_) { call(fd_.value, VIDIOC_STREAMOFF, &type_); streaming_ = false; } }
    bool next(uint32_t timeout, const std::function<void(const FrameView &)> &emit) override {
        pollfd descriptor{fd_.value, POLLIN, 0};
        int rc;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout);
        auto remaining = timeout;
        for (;;) {
            rc = ::poll(&descriptor, 1, static_cast<int>(remaining));
            if (rc >= 0 || errno != EINTR) break;
            auto now = std::chrono::steady_clock::now();
            if (now >= deadline) return false;
            remaining = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
        }
        if (rc < 0 || (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL))) throw std::runtime_error("Camera poll failure/disconnect");
        if (!rc) return false;
        v4l2_buffer b{}; v4l2_plane plane{}; buffer(b, plane, 0);
        if (call(fd_.value, VIDIOC_DQBUF, &b) < 0) {
            if (errno == EAGAIN) return false;
            throw std::runtime_error("DQBUF: " + std::string(std::strerror(errno)));
        }
        const auto received = monotonic_ns();
        try {
            if (b.index >= mappings_.size() || (b.flags & V4L2_BUF_FLAG_ERROR)) throw std::runtime_error("Invalid/error V4L2 frame");
            auto used = type_ == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE ? plane.bytesused : b.bytesused;
            auto offset = type_ == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE ? plane.data_offset : 0u;
            const auto &mapping = mappings_[b.index];
            if (used > mapping.size || offset > used || uint64_t(stride_) * height_ > used - offset)
                throw std::runtime_error("Truncated V4L2 payload");
            auto clock = (b.flags & V4L2_BUF_FLAG_TIMESTAMP_MASK) == V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC
                ? "linux.monotonic" : "linux.v4l2.unknown";
            FrameView view{{static_cast<const std::byte *>(mapping.address) + offset, used - offset},
                b.sequence, width_, height_, stride_, size_, b.flags,
                int64_t(b.timestamp.tv_sec) * 1000000000 + int64_t(b.timestamp.tv_usec) * 1000,
                received, clock, format_, controls_};
            emit(view); // The only acquisition copy occurs here, before driver reuse.
        } catch (...) { call(fd_.value, VIDIOC_QBUF, &b); throw; }
        checked(fd_.value, VIDIOC_QBUF, &b, "QBUF");
        return true;
    }
};
class LinuxBackend final : public Backend {
  public:
    std::vector<CameraInfo> discover() override {
        std::map<std::pair<unsigned, unsigned>, std::string> nodes;
        std::vector<std::filesystem::path> media;
        for (const auto &entry : std::filesystem::directory_iterator("/dev")) {
            auto name = entry.path().filename().string();
            if (name.starts_with("media")) media.push_back(entry.path());
            if (name.starts_with("video")) {
                struct stat st{};
                if (::stat(entry.path().c_str(), &st) == 0 && S_ISCHR(st.st_mode))
                    nodes[{major(st.st_rdev), minor(st.st_rdev)}] = entry.path().string();
            }
        }
        std::vector<CameraInfo> out;
        for (const auto &path : media) {
            Fd fd(path.string());
            media_device_info device{}; checked(fd.value, MEDIA_IOC_DEVICE_INFO, &device, "MEDIA_DEVICE_INFO");
            std::map<uint32_t, media_entity_desc> entities;
            uint32_t id = MEDIA_ENT_ID_FLAG_NEXT;
            for (;;) {
                media_entity_desc e{}; e.id = id;
                if (call(fd.value, MEDIA_IOC_ENUM_ENTITIES, &e) < 0) {
                    if (errno == EINVAL) break;
                    throw std::runtime_error("MEDIA_ENUM_ENTITIES failed");
                }
                entities[e.id] = e; id = e.id | MEDIA_ENT_ID_FLAG_NEXT;
            }
            std::map<uint32_t, std::set<uint32_t>> edges;
            for (const auto &[key, e] : entities) {
                std::vector<media_pad_desc> pads(e.pads);
                std::vector<media_link_desc> links(e.links);
                media_links_enum enumeration{}; enumeration.entity = key;
                enumeration.pads = pads.data(); enumeration.links = links.data();
                checked(fd.value, MEDIA_IOC_ENUM_LINKS, &enumeration, "MEDIA_ENUM_LINKS");
                for (const auto &link : links)
                    if (link.flags & MEDIA_LNK_FL_ENABLED) edges[link.source.entity].insert(link.sink.entity);
            }
            for (const auto &[key, sensor] : entities) {
                std::string name(sensor.name);
                if (name.find("ov9281") == std::string::npos) continue;
                std::vector<uint32_t> pending{key}; std::set<uint32_t> visited;
                while (!pending.empty()) {
                    auto current = pending.back(); pending.pop_back();
                    if (!visited.insert(current).second) continue;
                    auto node = entities.find(current);
                    if (node == entities.end()) throw std::runtime_error("Broken media topology");
                    auto video = nodes.find({node->second.dev.major, node->second.dev.minor});
                    if (video != nodes.end()) {
                        Fd capture(video->second);
                        auto type = capture_type(capture.value);
                        out.push_back({name, device.bus_info, video->second, formats(capture.value, type)});
                    }
                    for (auto sink : edges[current]) pending.push_back(sink);
                }
            }
        }
        return out;
    }
    std::unique_ptr<Camera> open(const CameraInfo &camera, const Mode &mode) override {
        return std::make_unique<LinuxCamera>(camera, mode);
    }
};
}
std::unique_ptr<Backend> linux_backend() { return std::make_unique<LinuxBackend>(); }
} // namespace x1
