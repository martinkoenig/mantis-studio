#include "recorder-diagnostics.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fstream>
#include <nlohmann/json.hpp>
#include <unistd.h>

namespace mantis::recorder_diagnostics {
namespace {
constexpr size_t capacity = 8192, sessions = 8, text_limit = 96;
using Text = std::array<char, text_limit>;
struct Key {
    uint64_t sequence{};
    int64_t sensor{}, received{};
    Text clock{};
};
struct Event {
    const char *stage{};
    int64_t ns{}, value{-1};
    Key key;
    Text source{}, artifact{};
};
struct Lane {
    std::array<Event, capacity> events;
    size_t count{}, dropped{};
    bool invalid{};
};
thread_local Trace *current{};
thread_local unsigned lane{};
thread_local Key key;
thread_local Text artifact;
std::mutex registry_mutex;
std::atomic<Boundary> hook{};
uint64_t next_generation{};
std::array<std::unique_ptr<Trace>, sessions> registry;
bool copy(Text &out, std::string_view value) noexcept {
    out.fill(0);
    if (value.size() >= out.size() || value.find('\0') != std::string_view::npos)
        return false;
    if (!value.empty())
        std::memcpy(out.data(), value.data(), value.size());
    return true;
}
int64_t now() noexcept {
    return time::MonotonicTimestamp::now().nanoseconds;
}
} // namespace
struct Trace {
    const void *owner{};
    std::array<Lane, 3> lanes;
    std::filesystem::path marker, output;
    nlohmann::json header;
};
namespace {
nlohmann::json key_json(const Key &k) {
    return {k.sequence, k.sensor, k.received, k.clock.data()};
}
void serialize(Trace &t) noexcept {
    try {
        nlohmann::json out = t.header;
        out["events"] = nlohmann::json::array();
        size_t dropped{};
        bool invalid{};
        for (size_t i = 0; i < t.lanes.size(); ++i) {
            const auto &l = t.lanes[i];
            dropped += l.dropped;
            invalid = invalid || l.invalid;
            for (size_t j = 0; j < l.count; ++j) {
                const auto &e = l.events[j];
                out["events"].push_back({{"lane", i},
                                         {"ordinal", j},
                                         {"stage", e.stage},
                                         {"ns", e.ns},
                                         {"value", e.value},
                                         {"key", key_json(e.key)},
                                         {"source", e.source.data()},
                                         {"artifact", e.artifact.data()}});
            }
        }
        out["dropped"] = dropped;
        out["invalid_identity"] = invalid;
        out["retired"] = true;
        // Only a fully serialized, closed file is published. The marker remains
        // even after success: crash/missing final output can never look complete.
        auto part = t.output;
        part += ".part";
        std::ofstream stream(part, std::ios::binary | std::ios::out | std::ios::noreplace);
        stream << out.dump() << '\n';
        stream.flush();
        if (!stream)
            throw std::runtime_error("diagnostic serialization failed");
        stream.close();
        if (!stream)
            throw std::runtime_error("diagnostic close failed");
        std::filesystem::rename(part, t.output);
    } catch (...) {
        std::fprintf(stderr, "recorder-observer: INCOMPLETE serialization; capture error unchanged\n");
    }
}
} // namespace
Trace *open(const void *owner, const device::CaptureDescriptor &descriptor) noexcept {
    const auto *directory = std::getenv("MANTIS_RECORDER_OBSERVE_DIR");
    if (!directory)
        return nullptr;
    try {
        std::lock_guard guard(registry_mutex);
        auto slot = std::find_if(registry.begin(), registry.end(), [](const auto &p) { return !p; });
        if (slot == registry.end() || next_generation >= sessions)
            throw std::runtime_error("diagnostic session capacity");
        auto t = std::make_unique<Trace>();
        t->owner = owner;
        auto generation = ++next_generation;
        auto stem = std::to_string(getpid()) + "-" + std::to_string(generation);
        t->marker = std::filesystem::path(directory) / (stem + ".started");
        t->output = std::filesystem::path(directory) / (stem + ".json");
        auto part = t->output;
        part += ".part";
        if (std::filesystem::exists(t->marker) || std::filesystem::exists(t->output) ||
            std::filesystem::exists(part))
            throw std::runtime_error("diagnostic output identity already retained");
        std::vector<std::string> sources;
        for (const auto &id : descriptor.devices) {
            if (sources.size() == 32 || id.value.size() >= text_limit)
                throw std::runtime_error("diagnostic source identity capacity");
            sources.push_back(id.value);
        }
        if (descriptor.id.value.size() >= text_limit)
            throw std::runtime_error("diagnostic capture identity capacity");
        t->header = {{"version", 1},
                     {"capture", descriptor.id.value},
                     {"sources", sources},
                     {"pid", getpid()},
                     {"generation", generation},
                     {"opened_ns", now()},
                     {"clock", "process-steady-ns"},
                     {"build", MANTIS_RECORDER_BUILD_ID},
                     {"capacity_per_lane", capacity},
                     {"record_bytes", sizeof(Event)},
                     {"retained_record_bytes_per_capture", sizeof(Lane) * 3},
                     {"queue_capacity", 32},
                     {"push_ms", 50},
                     {"startup_s", 5},
                     {"unpaced_ms", 33},
                     {"retired", false},
                     {"durability", "callback counts are not durable acknowledgements"}};
        std::ofstream marker(t->marker, std::ios::binary | std::ios::out | std::ios::noreplace);
        marker << t->header.dump() << '\n';
        marker.close();
        if (!marker)
            throw std::runtime_error("diagnostic marker failed");
        *slot = std::move(t);
        return slot->get();
    } catch (...) {
        std::fprintf(stderr, "recorder-observer: UNAVAILABLE initialization; capture error unchanged\n");
        return nullptr;
    }
}
Worker::Worker(Trace *trace, unsigned value) noexcept : previous_(current), lane_(lane) {
    current = trace;
    lane = value;
    key = {};
    artifact = {};
    event("worker.begin");
}
Worker::~Worker() {
    event("worker.end");
    current = previous_;
    lane = lane_;
}
void event(const char *stage, const data::Packet *packet, int64_t value,
           std::string_view source_id) noexcept {
    if (!current)
        return;
    auto &l = current->lanes[lane];
    if (packet) {
        key.sequence = packet->header.sequence.value;
        key.sensor = packet->header.timestamp.nanoseconds;
        key.received = packet->header.received.nanoseconds;
        l.invalid = !copy(key.clock, packet->header.timestamp.domain.id.value) || l.invalid;
    }
    if (l.count == capacity) {
        if (l.dropped != SIZE_MAX)
            ++l.dropped;
        return;
    }
    auto &e = l.events[l.count++];
    e.stage = stage;
    e.ns = now();
    e.value = value;
    e.key = key;
    e.artifact = artifact;
    l.invalid = !copy(e.source, source_id) || l.invalid;
}
void source(const data::Packet &packet, const device::ImageStream &stream) noexcept {
    if (!current)
        return;
    try {
        // Keep virtual argument evaluation inside the exception boundary too.
        event("source.frame", &packet, -1, stream.descriptor().id.value);
    } catch (...) {
        current->lanes[lane].invalid = true;
    }
}
std::optional<data::Published> pop(pipeline::BoundedQueue<data::Published> &queue) {
    if (!current)
        return queue.pop();
    event("pop.begin", nullptr, static_cast<int64_t>(queue.metrics().occupancy));
    auto frame = queue.try_pop();
    if (frame) {
        event("pop.ready", frame->get());
    } else {
        event("pop.empty");
        frame = queue.pop();
        event(frame ? "pop.woke" : "pop.closed", frame ? frame->get() : nullptr);
    }
    return frame;
}
void cleanup(const void *owner, bool done) noexcept {
    try {
        std::unique_ptr<Trace> retired;
        {
            std::lock_guard guard(registry_mutex);
            auto slot = std::find_if(registry.begin(), registry.end(),
                                     [&](const auto &p) { return p && p->owner == owner; });
            if (slot == registry.end())
                return;
            auto *previous = current;
            auto previous_lane = lane;
            current = slot->get();
            lane = 2;
            event(done ? "cleanup.end" : "cleanup.begin");
            current = previous;
            lane = previous_lane;
            if (done)
                retired = std::move(*slot);
        }
        // stop() has joined both workers before done. No observation under
        // Session/Store/capture locks, and no callback can outlive these buffers.
        if (retired)
            serialize(*retired);
    } catch (...) {
        std::fprintf(stderr, "recorder-observer: INCOMPLETE retirement; capture error unchanged\n");
    }
}
void raw_stage(size_t index, bool end) noexcept {
    constexpr std::array<const char *, 6> begins{"raw.provenance.begin", "raw.init.begin",  "raw.open.begin",
                                                 "raw.directory.begin",  "raw.write.begin", "raw.seal.begin"};
    constexpr std::array<const char *, 6> ends{"raw.provenance.end", "raw.init.end",  "raw.open.end",
                                               "raw.directory.end",  "raw.write.end", "raw.seal.end"};
    if (index < begins.size())
        event(end ? ends[index] : begins[index]);
}
StoreSpan::StoreSpan(std::string_view id) noexcept : exceptions_(std::uncaught_exceptions()) {
    if (current && !copy(artifact, id))
        current->lanes[lane].invalid = true;
    event("store.begin");
}
StoreSpan::~StoreSpan() {
    event(std::uncaught_exceptions() == exceptions_ ? "store.end" : "store.failed");
    artifact = {};
}
void boundary_hook(Boundary value) noexcept {
    hook.store(value);
}
void boundary(std::mutex &mutex) noexcept {
    if (auto callback = hook.load()) {
        try {
            callback(mutex);
        } catch (...) {
            if (current)
                current->lanes[lane].invalid = true;
        }
    }
}
} // namespace mantis::recorder_diagnostics
