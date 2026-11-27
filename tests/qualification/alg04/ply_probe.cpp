// Test-only Linux link wrappers. The stock daemon and PLY DSO are unchanged.
#include <cstdio>
#include <cstdlib>
#include <mantis/l1_ray_plane.hpp>
#include <mantis/plugin_runtime.hpp>
#include <poll.h>
#include <unistd.h>

namespace {
using namespace mantis;
struct Work {
    size_t aligned_count{}, aligned_bytes{}, bulk_copies{}, bulk_copy_bytes{};
    const void *last_backing{};
};
thread_local Work validation, projection;
thread_local Work *work{};
thread_local spatial::CoordinateFrame frame;
thread_local calibration::Reference calibration_ref;
thread_local bool awaiting_sync{};
constexpr size_t sensor_backing = 256 * 12;

int channel() {
    const auto *fd = std::getenv("MANTIS_PLY_PROBE_FD");
    return fd ? std::atoi(fd) : -1;
}

void copy_bytes(size_t count) {
    if (work && count >= sensor_backing) {
        ++work->bulk_copies;
        work->bulk_copy_bytes += count;
    }
}
} // namespace

extern "C" void *__real__ZnwmSt11align_val_t(size_t, std::align_val_t);
extern "C" void *__wrap__ZnwmSt11align_val_t(size_t size, std::align_val_t alignment) {
    auto *p = __real__ZnwmSt11align_val_t(size, alignment);
    if (work) {
        ++work->aligned_count;
        work->aligned_bytes += size;
        work->last_backing = p;
    }
    return p;
}
extern "C" void *__real_memcpy(void *, const void *, size_t);
extern "C" void *__real_memmove(void *, const void *, size_t);
extern "C" void *__wrap_memcpy(void *out, const void *in, size_t n) {
    copy_bytes(n);
    return __real_memcpy(out, in, n);
}
extern "C" void *__wrap_memmove(void *out, const void *in, size_t n) {
    copy_bytes(n);
    return __real_memmove(out, in, n);
}

Result<data::Published> real_load(const artifact::Store &, const Id &,
                                  const CancellationToken &) asm("__real_" PLY_LOAD);
Result<data::Published> observed_load(const artifact::Store &, const Id &,
                                      const CancellationToken &) asm("__wrap_" PLY_LOAD);
Result<data::Published> observed_load(const artifact::Store &store, const Id &id,
                                      const CancellationToken &t) {
    if (channel() < 0)
        return real_load(store, id, t);
    validation = {};
    projection = {};
    work = &validation;
    struct Reset {
        ~Reset() {
            work = nullptr;
        }
    };
    auto load = [&] {
        const Reset reset;
        return real_load(store, id, t);
    };
    auto result = load();
    if (result) {
        const auto &packet = **result;
        frame = packet.header.frame;
        calibration_ref = packet.header.calibration;
        const auto &first = packet.attributes.at(0).buffer;
        size_t columns{}, payload_bytes{};
        for (const auto &attr : packet.attributes) {
            columns += attr.buffer.identity() == first.identity();
            payload_bytes += attr.buffer.size();
        }
        dprintf(channel(),
                "validation shared_columns=%zu total_columns=%zu mapped_bytes=%zu payload_bytes=%zu "
                "aligned_allocations=%zu aligned_bytes=%zu bulk_copies=%zu bulk_copy_bytes=%zu\n",
                columns, packet.attributes.size(), first.backing_size(), payload_bytes,
                validation.aligned_count, validation.aligned_bytes, validation.bulk_copies,
                validation.bulk_copy_bytes);
        work = &projection;
    }
    return result;
}

void real_export(plugins::Registry *, const std::string &, const data::Packet &,
                 const std::filesystem::path &, const CancellationToken &) asm("__real_" PLY_EXPORT);
void observed_export(plugins::Registry *, const std::string &, const data::Packet &,
                     const std::filesystem::path &, const CancellationToken &) asm("__wrap_" PLY_EXPORT);
void observed_export(plugins::Registry *registry, const std::string &id, const data::Packet &packet,
                     const std::filesystem::path &path, const CancellationToken &t) {
    if (work != &projection) {
        real_export(registry, id, packet, path, t);
        return;
    }
    struct Reset {
        ~Reset() {
            work = nullptr;
        }
    } reset;
    const auto &pos = packet.attributes.at(0);
    const auto &desc = pos.descriptor;
    const auto view = pos.buffer.map_read(t);
    const bool envelope_ok = id == "org.mantis.ply" && packet.type == schema::points &&
                             packet.frames.empty() && packet.attributes.size() == 1 &&
                             desc.name == "org.mantis.position" && desc.scalar == schema::ScalarType::f32 &&
                             desc.shape.size() == 2 && desc.shape.at(1) == 3 && desc.stride.size() == 2 &&
                             desc.stride.at(0) == 12 && desc.stride.at(1) == 4 && desc.unit == "mm";
    const bool frame_ok = packet.header.frame.id == frame.id && packet.header.frame.name == frame.name;
    const auto &c = packet.header.calibration;
    const bool calibration_ok = c.id == calibration_ref.id &&
                                c.schema_version == calibration_ref.schema_version &&
                                c.revision == calibration_ref.revision;
    const bool same_address = view && view->data() == projection.last_backing;
    real_export(registry, id, packet, path, t);
    dprintf(channel(),
            "projection vertices=%llu backing_bytes=%zu payload_bytes=%zu "
            "aligned_allocations=%zu aligned_bytes=%zu bulk_copies=%zu bulk_copy_bytes=%zu "
            "envelope_ok=%d frame_ok=%d calibration_ok=%d same_address=%d\n",
            static_cast<unsigned long long>(desc.shape.at(0)), pos.buffer.backing_size(), pos.buffer.size(),
            projection.aligned_count, projection.aligned_bytes, projection.bulk_copies,
            projection.bulk_copy_bytes, envelope_ok, frame_ok, calibration_ok, same_address);
    awaiting_sync = true;
}

void real_sync(const std::filesystem::path &) asm("__real_" PLY_SYNC);
void observed_sync(const std::filesystem::path &) asm("__wrap_" PLY_SYNC);
void observed_sync(const std::filesystem::path &path) {
    real_sync(path);
    if (!awaiting_sync)
        return;
    awaiting_sync = false;
    const auto fd = channel();
    pollfd gate{fd, POLLIN, 0};
    char resume{};
    if (path.extension() != ".part" || dprintf(fd, "part_synced\n") < 0 || poll(&gate, 1, 15000) != 1 ||
        read(fd, &resume, 1) != 1 || resume != 'G')
        fail(Status::io, "Test export publication gate not released");
}
