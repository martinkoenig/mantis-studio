#include "media.hpp"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#include <linux/media.h>
#include <linux/media-bus-format.h>
#include <linux/v4l2-subdev.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <set>
#include <stdexcept>
#include <system_error>
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
        if (value < 0) throw std::system_error(errno, std::generic_category(), "open " + path);
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
    if (call(fd, request, arg) < 0) throw std::system_error(errno, std::generic_category(), std::string(name) + " (errno " + std::to_string(errno) + ")");
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
            throw std::system_error(errno, std::generic_category(), "ENUM_FMT");
        }
        if (!(f.flags & V4L2_FMT_FLAG_COMPRESSED)) out.push_back(fourcc(f.pixelformat));
    }
    return out;
}
class NativeMedia final : public MediaIo {
  public:
    Graph graph(const std::string &path) override {
        Fd fd(path);
        media_device_info device{}; checked(fd.value, MEDIA_IOC_DEVICE_INFO, &device, "MEDIA_IOC_DEVICE_INFO");
        Graph out{path, device.bus_info, {}, {}};
        std::map<std::pair<unsigned, unsigned>, std::string> nodes;
        for (const auto &entry : std::filesystem::directory_iterator("/dev")) {
            auto name = entry.path().filename().string();
            if (!name.starts_with("video") && !name.starts_with("v4l-subdev")) continue;
            struct stat st{};
            if (::stat(entry.path().c_str(), &st) == 0 && S_ISCHR(st.st_mode))
                nodes[{major(st.st_rdev), minor(st.st_rdev)}] = entry.path().string();
        }
        std::set<std::pair<Endpoint, Endpoint>> seen;
        uint32_t id = MEDIA_ENT_ID_FLAG_NEXT;
        for (uint32_t count = 0; ; ++count) {
            if (count >= 4096) throw std::runtime_error(path + " media entity enumeration exceeds bound");
            media_entity_desc e{}; e.id = id;
            if (call(fd.value, MEDIA_IOC_ENUM_ENTITIES, &e) < 0) {
                if (errno == EINVAL) break;
                throw std::system_error(errno, std::generic_category(), path + " MEDIA_IOC_ENUM_ENTITIES");
            }
            id = e.id | MEDIA_ENT_ID_FLAG_NEXT;
            Entity entity{e.id, e.name, {}, false, {}};
            auto node = nodes.find({e.dev.major, e.dev.minor});
            if (node != nodes.end()) {
                entity.node = node->second;
                entity.capture = std::filesystem::path(entity.node).filename().string().starts_with("video");
            }
            std::vector<media_pad_desc> pads(e.pads);
            std::vector<media_link_desc> links(e.links);
            media_links_enum enumeration{}; enumeration.entity = e.id;
            enumeration.pads = pads.data(); enumeration.links = links.data();
            checked(fd.value, MEDIA_IOC_ENUM_LINKS, &enumeration, "MEDIA_IOC_ENUM_LINKS");
            for (const auto &pad : pads) entity.pads.push_back({pad.index, bool(pad.flags & MEDIA_PAD_FL_SOURCE), bool(pad.flags & MEDIA_PAD_FL_SINK)});
            out.entities.push_back(std::move(entity));
            for (const auto &l : links) {
                if ((l.flags & MEDIA_LNK_FL_LINK_TYPE) != MEDIA_LNK_FL_DATA_LINK) continue;
                Endpoint source{l.source.entity, l.source.index}, sink{l.sink.entity, l.sink.index};
                if (seen.insert({source, sink}).second)
                    out.links.push_back({source, sink, bool(l.flags & MEDIA_LNK_FL_ENABLED), bool(l.flags & MEDIA_LNK_FL_IMMUTABLE)});
            }
        }
        return out;
    }
    void link(const std::string &media, const Link &link, bool enabled) override {
        Fd fd(media);
        // Re-read the kernel descriptor to retain its immutable/dynamic/type flags.
        media_entity_desc entity{}; entity.id = link.source.entity;
        checked(fd.value, MEDIA_IOC_ENUM_ENTITIES, &entity, "MEDIA_IOC_ENUM_ENTITIES (link source)");
        std::vector<media_pad_desc> pads(entity.pads); std::vector<media_link_desc> links(entity.links);
        media_links_enum enumeration{}; enumeration.entity = entity.id; enumeration.pads = pads.data(); enumeration.links = links.data();
        checked(fd.value, MEDIA_IOC_ENUM_LINKS, &enumeration, "MEDIA_IOC_ENUM_LINKS (setup)");
        auto it = std::find_if(links.begin(), links.end(), [&](const auto &l) {
            return l.source.entity == link.source.entity && l.source.index == link.source.pad &&
                l.sink.entity == link.sink.entity && l.sink.index == link.sink.pad;
        });
        if (it == links.end()) throw std::runtime_error("Media link disappeared before setup");
        it->flags = enabled ? it->flags | MEDIA_LNK_FL_ENABLED : it->flags & ~MEDIA_LNK_FL_ENABLED;
        checked(fd.value, MEDIA_IOC_SETUP_LINK, &*it, "MEDIA_IOC_SETUP_LINK");
    }
    uint32_t format_code(const std::string &name) override {
        if (name == "Y10_1X10") return MEDIA_BUS_FMT_Y10_1X10;
        if (name == "Y8_1X8") return MEDIA_BUS_FMT_Y8_1X8;
        throw std::runtime_error("Unsupported media-bus code " + name);
    }
    std::vector<uint32_t> codes(const Entity &entity, uint32_t pad) override {
        Fd fd(entity.node); std::vector<uint32_t> result;
        for (uint32_t i = 0; i < 256; ++i) {
            v4l2_subdev_mbus_code_enum code{}; code.pad = pad; code.index = i; code.which = V4L2_SUBDEV_FORMAT_ACTIVE;
            if (call(fd.value, VIDIOC_SUBDEV_ENUM_MBUS_CODE, &code) < 0) {
                if (errno == EINVAL || errno == ENOTTY) return result; // S_FMT/read-back remains authoritative
                throw std::system_error(errno, std::generic_category(), entity.node + " SUBDEV_ENUM_MBUS_CODE");
            }
            result.push_back(code.code);
        }
        throw std::runtime_error(entity.node + " media-bus enumeration exceeds bound");
    }
    static PadFormat unpack(const v4l2_mbus_framefmt &f) {
        return {f.code, f.width, f.height, f.field, f.colorspace, f.ycbcr_enc, f.quantization, f.xfer_func, f.flags};
    }
    PadFormat get_format(const Entity &entity, uint32_t pad) override {
        Fd fd(entity.node); v4l2_subdev_format f{}; f.which = V4L2_SUBDEV_FORMAT_ACTIVE; f.pad = pad;
        checked(fd.value, VIDIOC_SUBDEV_G_FMT, &f, "VIDIOC_SUBDEV_G_FMT"); return unpack(f.format);
    }
    PadFormat set_format(const Entity &entity, uint32_t pad, const PadFormat &requested) override {
        Fd fd(entity.node); v4l2_subdev_format f{}; f.which = V4L2_SUBDEV_FORMAT_ACTIVE; f.pad = pad;
        f.format.code = requested.code; f.format.width = requested.width; f.format.height = requested.height;
        f.format.field = requested.field; f.format.colorspace = requested.colorspace; f.format.ycbcr_enc = static_cast<decltype(f.format.ycbcr_enc)>(requested.ycbcr);
        f.format.quantization = static_cast<decltype(f.format.quantization)>(requested.quantization); f.format.xfer_func = static_cast<decltype(f.format.xfer_func)>(requested.transfer); f.format.flags = static_cast<decltype(f.format.flags)>(requested.flags);
        checked(fd.value, VIDIOC_SUBDEV_S_FMT, &f, "VIDIOC_SUBDEV_S_FMT"); return unpack(f.format);
    }
    int32_t get_vblank(const Entity &entity) override {
        Fd fd(entity.node); v4l2_control control{}; control.id = V4L2_CID_VBLANK;
        checked(fd.value, VIDIOC_G_CTRL, &control, "VIDIOC_G_CTRL VBLANK"); return control.value;
    }
    int32_t set_vblank(const Entity &entity, int32_t value) override {
        Fd fd(entity.node); v4l2_queryctrl query{}; query.id = V4L2_CID_VBLANK;
        checked(fd.value, VIDIOC_QUERYCTRL, &query, "VIDIOC_QUERYCTRL VBLANK");
        if ((query.flags & (V4L2_CTRL_FLAG_DISABLED | V4L2_CTRL_FLAG_READ_ONLY | V4L2_CTRL_FLAG_INACTIVE | V4L2_CTRL_FLAG_GRABBED)) ||
            value < query.minimum || value > query.maximum || (int64_t(value) - query.minimum) % std::max(1, query.step))
            throw std::runtime_error("VBLANK unavailable, busy or outside driver range/step");
        v4l2_control control{}; control.id = V4L2_CID_VBLANK; control.value = value;
        checked(fd.value, VIDIOC_S_CTRL, &control, "VIDIOC_S_CTRL VBLANK"); return control.value;
    }
    Metadata timing(const Entity &entity) override {
        Fd fd(entity.node); Metadata result;
        for (auto [id, name] : std::array<std::pair<uint32_t, const char *>, 2>{{
                {V4L2_CID_EXPOSURE, "initial_sensor_exposure_control"}, {V4L2_CID_ANALOGUE_GAIN, "initial_sensor_analogue_gain_control"}}}) {
            v4l2_control control{}; control.id = id;
            if (call(fd.value, VIDIOC_G_CTRL, &control) == 0) result[name] = std::to_string(control.value);
            else if (errno == EINVAL || errno == ENOTTY) result[name] = "unavailable";
            else throw std::system_error(errno, std::generic_category(), entity.node + " G_CTRL " + name);
        }
        v4l2_subdev_frame_interval interval{};
        if (call(fd.value, VIDIOC_SUBDEV_G_FRAME_INTERVAL, &interval) < 0) {
            if (errno == EINVAL || errno == ENOTTY) result["sensor_driver_interval"] = "unavailable";
            else throw std::system_error(errno, std::generic_category(), entity.node + " SUBDEV_G_FRAME_INTERVAL");
        } else {
            result["sensor_driver_interval_numerator"] = std::to_string(interval.interval.numerator);
            result["sensor_driver_interval_denominator"] = std::to_string(interval.interval.denominator);
        }
        return result;
    }
};
class LinuxCamera final : public Camera {
    std::string context_;
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
    LinuxCamera(const CameraInfo &info, const Mode &mode) try : context_(camera_context(info)), fd_(info.video), type_(capture_type(fd_.value)) {
        try {
            auto available = formats(fd_.value, type_);
            if (std::find(available.begin(), available.end(), mode.fourcc) == available.end())
                throw std::runtime_error("Requested native fourcc unavailable");
            // Query enumerated modes where the driver provides them. S_FMT below is
            // authoritative for MC-centric drivers without ENUM_FRAMESIZES.
            bool has_sizes = false, size_matches = false;
            for (uint32_t i = 0; i < 256; ++i) {
                v4l2_frmsizeenum f{}; f.index = i; f.pixel_format = code(mode.fourcc);
                if (call(fd_.value, VIDIOC_ENUM_FRAMESIZES, &f) < 0) {
                    if (errno == EINVAL || errno == ENOTTY) break;
                    throw std::system_error(errno, std::generic_category(), "ENUM_FRAMESIZES");
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
                if (f.fmt.pix_mp.num_planes != 1) throw std::runtime_error("Native raw capture requires one plane");
                width_ = f.fmt.pix_mp.width; height_ = f.fmt.pix_mp.height; pixel = f.fmt.pix_mp.pixelformat;
                stride_ = f.fmt.pix_mp.plane_fmt[0].bytesperline; size_ = f.fmt.pix_mp.plane_fmt[0].sizeimage;
            } else {
                width_ = f.fmt.pix.width; height_ = f.fmt.pix.height; pixel = f.fmt.pix.pixelformat;
                stride_ = f.fmt.pix.bytesperline; size_ = f.fmt.pix.sizeimage;
            }
            format_ = fourcc(pixel);
            if (width_ != mode.width || height_ != mode.height || format_ != mode.fourcc ||
                stride_ < (mode.fourcc == "Y10P" ? width_ / 4 * 5 : width_) || uint64_t(stride_) * height_ > size_ || size_ > 128 * 1024 * 1024)
                throw std::runtime_error("Driver negotiated an incompatible native layout");
            bool has_intervals = false, interval_matches = false;
            for (uint32_t i = 0; i < 256; ++i) {
                v4l2_frmivalenum interval{}; interval.index = i; interval.pixel_format = pixel;
                interval.width = width_; interval.height = height_;
                if (call(fd_.value, VIDIOC_ENUM_FRAMEINTERVALS, &interval) < 0) {
                    if (errno == EINVAL || errno == ENOTTY) break;
                    throw std::system_error(errno, std::generic_category(), "ENUM_FRAMEINTERVALS");
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
            if (!mode.sensor_timing_configured && has_intervals && !interval_matches) throw std::runtime_error("Requested camera FPS unavailable");
            controls_["interval_enumeration"] = has_intervals ? "supported" : "unavailable";
            controls_["driver_interval"] = "unavailable";
            v4l2_streamparm parm{}; parm.type = type_;
            const auto parm_result = call(fd_.value, VIDIOC_G_PARM, &parm);
            if (parm_result < 0 && errno != EINVAL && errno != ENOTTY) throw std::system_error(errno, std::generic_category(), "G_PARM");
            if (parm_result == 0 && (parm.parm.capture.capability & V4L2_CAP_TIMEPERFRAME)) {
                controls_["driver_interval"] = "reported; not measured receive FPS";
                if (!mode.sensor_timing_configured) {
                    parm.parm.capture.timeperframe = {1, mode.fps};
                    checked(fd_.value, VIDIOC_S_PARM, &parm, "S_PARM");
                }
                controls_["driver_interval_numerator"] = std::to_string(parm.parm.capture.timeperframe.numerator);
                controls_["driver_interval_denominator"] = std::to_string(parm.parm.capture.timeperframe.denominator);
            }
            // Controls are a start-time snapshot, never fabricated per-exposure values.
            for (auto [id, name] : std::array<std::pair<uint32_t, const char *>, 2>{{
                    {V4L2_CID_EXPOSURE, "initial_exposure_control"}, {V4L2_CID_ANALOGUE_GAIN, "initial_analogue_gain_control"}}}) {
                v4l2_control control{}; control.id = id;
                if (call(fd_.value, VIDIOC_G_CTRL, &control) == 0) controls_[name] = std::to_string(control.value);
                else if (errno == EINVAL || errno == ENOTTY) controls_[name] = "unavailable";
                else throw std::system_error(errno, std::generic_category(), std::string("G_CTRL ") + name);
            }
            v4l2_requestbuffers request{}; request.type = type_; request.memory = V4L2_MEMORY_MMAP; request.count = 8;
            checked(fd_.value, VIDIOC_REQBUFS, &request, "REQBUFS");
            if (request.count < 2 || request.count > 64) throw std::runtime_error("Invalid V4L2 buffer count");
            mappings_.reserve(request.count); // allocation cannot fail after a successful mmap below
            for (uint32_t i = 0; i < request.count; ++i) {
                v4l2_buffer b{}; v4l2_plane plane{}; buffer(b, plane, i);
                checked(fd_.value, VIDIOC_QUERYBUF, &b, "QUERYBUF");
                auto length = type_ == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE ? plane.length : b.length;
                auto offset = type_ == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE ? plane.m.mem_offset : b.m.offset;
                if (length < size_) throw std::runtime_error("Driver buffer shorter than sizeimage");
                void *address = ::mmap(nullptr, length, PROT_READ | PROT_WRITE, MAP_SHARED, fd_.value, offset);
                if (address == MAP_FAILED) throw std::system_error(errno, std::generic_category(), "MMAP");
                mappings_.push_back({address, length});
                checked(fd_.value, VIDIOC_QBUF, &b, "initial QBUF");
            }
        } catch (...) { cleanup(); throw; }
    }
    catch (const std::exception &e) { throw std::runtime_error(camera_context(info) + " configure: " + e.what()); }
    ~LinuxCamera() override { cleanup(); }
    void start() override {
        try { checked(fd_.value, VIDIOC_STREAMON, &type_, "STREAMON"); streaming_ = true; }
        catch (const std::exception &e) { throw std::runtime_error(context_ + " " + e.what()); }
    }
    Metadata diagnostics() const override { return controls_; }
    void stop() noexcept override {
        if (!streaming_) return;
        if (call(fd_.value, VIDIOC_STREAMOFF, &type_) < 0) {
            const auto error = errno;
            try { controls_["streamoff_error"] = context_ + " STREAMOFF: " + std::strerror(error) + " (errno " + std::to_string(error) + ")"; } catch (...) {}
        }
        streaming_ = false;
    }
    bool next(uint32_t timeout, const std::function<void(const FrameView &)> &emit) override try {
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
        if (rc < 0) throw std::system_error(errno, std::generic_category(), "poll");
        if (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) throw std::runtime_error("Camera poll failure/disconnect; revents=" + std::to_string(descriptor.revents));
        if (!rc) return false;
        v4l2_buffer b{}; v4l2_plane plane{}; buffer(b, plane, 0);
        if (call(fd_.value, VIDIOC_DQBUF, &b) < 0) {
            if (errno == EAGAIN) return false;
            throw std::system_error(errno, std::generic_category(), "DQBUF (errno " + std::to_string(errno) + ")");
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
    } catch (const std::exception &e) { throw std::runtime_error(context_ + " " + e.what()); }
};
class LinuxBackend final : public Backend {
    NativeMedia media_;
    std::vector<CameraInfo> scan(const Profile *profile) {
        std::vector<CameraInfo> out;
        for (const auto &entry : std::filesystem::directory_iterator("/dev")) {
            if (!entry.path().filename().string().starts_with("media")) continue;
            auto graph = media_.graph(entry.path().string());
            for (const auto &sensor : graph.entities) {
                if (sensor.name.find("ov9281") == std::string::npos) continue;
                std::vector<std::vector<std::string>> routes;
                std::string role;
                if (profile && profile->format_version == 2) {
                    for (size_t i = 0; i < 2; ++i) if (sensor.name == profile->sensors[i] &&
                        (profile->buses[i].empty() || profile->buses[i] == graph.bus)) {
                        routes.push_back(profile->routes[i]); role = i ? "RIGHT" : "LEFT";
                    }
                    if (routes.empty()) continue;
                } else {
                    routes = possible_routes(graph, sensor, true);
                    if (routes.empty()) routes = possible_routes(graph, sensor, false);
                    if (routes.size() > 1) throw std::runtime_error(sensor.name + " ambiguous capture topology; configure profile v2 explicit route");
                }
                for (const auto &names : routes) {
                    auto route = select_route(graph, names, role);
                    const auto &node = route.entities.back().node;
                    if (node.empty()) throw std::runtime_error(role + " " + sensor.name + " capture entity has no resolved node");
                    try {
                        Fd capture(node); auto type = capture_type(capture.value);
                        out.push_back({sensor.name, graph.bus, node, formats(capture.value, type), role, graph.path, names});
                    } catch (const std::exception &e) { throw std::runtime_error(role + " " + sensor.name + " (" + node + ") discovery: " + e.what()); }
                }
            }
        }
        return out;
    }
  public:
    std::vector<CameraInfo> discover() override { return scan(nullptr); }
    std::vector<CameraInfo> discover(const Profile &p) override { return scan(&p); }
    std::unique_ptr<Setup> prepare(const Profile &p, const std::array<CameraInfo, 2> &cameras) override {
        return configure_media(media_, p, cameras);
    }
    std::unique_ptr<Camera> open(const CameraInfo &camera, const Mode &mode) override {
        return std::make_unique<LinuxCamera>(camera, mode);
    }
};
}
std::unique_ptr<Backend> linux_backend() { return std::make_unique<LinuxBackend>(); }
} // namespace x1
