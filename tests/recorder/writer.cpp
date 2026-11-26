#include "../../src/artifact-store/segments.hpp"
#include "../../src/device-runtime/recorder-diagnostics.hpp"
#include <fstream>
#include <future>
#include <iostream>
#include <mantis/artifact_store.hpp>
#include <mantis/data_io.hpp>
#include <nlohmann/json.hpp>
#include <sqlite3.h>
#include <unistd.h>
using namespace mantis;
using namespace std::chrono_literals;
#define CHECK(...)                                                                                           \
    do {                                                                                                     \
        if (!(__VA_ARGS__))                                                                                  \
            throw std::runtime_error("Line " + std::to_string(__LINE__) + ": " #__VA_ARGS__);                \
    } while (false)
namespace {
struct Gate {
    std::promise<void> promise;
    std::shared_future<void> future = promise.get_future().share();
    std::atomic_bool opened{};
    void open() {
        if (!opened.exchange(true))
            promise.set_value();
    }
    void wait() {
        CHECK(future.wait_for(5s) == std::future_status::ready);
    }
};
struct Control {
    Gate produce, ended, finish, boundary_entered, boundary_release, primary;
    std::atomic_uint callbacks{};
    std::atomic_int64_t entered_ns{}, released_ns{};
    bool hold{}, noisy{}, descriptor_fault{};
    std::string terminal;
    void release() {
        produce.open();
        finish.open();
        boundary_release.open();
    }
};
Control *control{};
void boundary(std::mutex &mutex) {
    auto &c = *control;
    if (c.callbacks != 2)
        return;
    if (c.noisy)
        throw std::runtime_error("observer secondary failure");
    if (!c.hold)
        return;
    // This is the actual Session accounting mutex, not a look-alike test lock.
    std::unique_lock lock(mutex);
    c.entered_ns = time::MonotonicTimestamp::now().nanoseconds;
    c.boundary_entered.open();
    c.boundary_release.wait();
}
class Source : public device::ImageStream {
    device::Descriptor descriptor_;
    Control &c_;
    uint64_t calls_{};
    unsigned count_;
    bool composite_;

  public:
    unsigned stops{};
    explicit Source(Control &c, unsigned count, bool composite)
        : c_(c), count_(count), composite_(composite) {
        descriptor_.id = {"controlled-camera"};
        descriptor_.name = "controlled software image source";
    }
    const device::Descriptor &descriptor() const override {
        if (c_.descriptor_fault)
            throw std::runtime_error("observer descriptor failure");
        return descriptor_;
    }
    bool source_paced() const override {
        return true;
    }
    Result<void> start() override {
        return {};
    }
    Result<void> stop() override {
        ++stops;
        if (c_.noisy)
            throw std::runtime_error("cleanup secondary failure");
        return {};
    }
    Result<data::Published> next() override {
        if (calls_ == 1)
            c_.produce.wait();
        if (calls_ == count_) {
            c_.ended.open();
            c_.finish.wait();
            if (c_.terminal == "source-throw")
                throw std::runtime_error("source primary");
            if (c_.terminal == "source-error")
                return std::unexpected(Error{Status::io, "source primary", "fixture"});
            return data::Published{};
        }
        if (calls_ > count_)
            return data::Published{};
        data::Packet p;
        p.type = schema::image;
        p.header.sequence.value = calls_++;
        p.header.timestamp.domain.id = {"controlled-device-clock"};
        p.header.timestamp.nanoseconds = static_cast<int64_t>(p.header.sequence.value * 33000000);
        p.header.received.nanoseconds = time::MonotonicTimestamp::now().nanoseconds;
        memory::BufferBuilder bytes(16);
        for (size_t i = 0; i < bytes.writable().size(); ++i)
            bytes.writable()[i] = std::byte(p.header.sequence.value + i);
        p.attributes = {
            {{"org.mantis.pixels", schema::ScalarType::u8, {4, 4}, {4, 1}, ""}, std::move(bytes).publish()}};
        if (composite_) {
            p.header.metadata["fourcc"] = "GREY";
            data::Packet parent;
            parent.type = schema::frameset;
            parent.header = p.header;
            parent.frames.push_back(data::publish(std::move(p)));
            return data::publish(std::move(parent));
        }
        return data::publish(std::move(p));
    }
};
std::string replay(const std::shared_ptr<artifact::Store> &store, const Id &raw, uint64_t count) {
    artifact::CaptureReader reader(store, raw);
    std::string bytes;
    for (uint64_t i = 0; i < count; ++i) {
        auto packet = reader.next();
        CHECK(packet && packet->header.sequence.value == i);
        const auto &image = packet->type == schema::frameset ? *packet->frames.at(0) : *packet;
        const auto mapped = image.attributes.at(0).buffer.map_read();
        CHECK(mapped);
        const auto view = *mapped;
        CHECK(view.size() == 16);
        for (size_t j = 0; j < view.size(); ++j) {
            CHECK(view[j] == std::byte(i + j));
            bytes.push_back(static_cast<char>(view[j]));
        }
    }
    CHECK(!reader.next());
    return bytes;
}
} // namespace
int run_case(const std::string &mode, const std::filesystem::path &root) {
    try {
        std::filesystem::create_directories(root);
        if (mode == "init-sibling") {
            // Real sequential Sessions: only the second observer loses its
            // output directory. Neither the source nor Store operation fails.
            const auto *directory = std::getenv("MANTIS_RECORDER_OBSERVE_DIR");
            CHECK(directory);
            const std::string original(directory);
            CHECK(run_case("positive", root / "first") == 0);
            const auto missing = (std::filesystem::path(original) / "missing").string();
            CHECK(setenv("MANTIS_RECORDER_OBSERVE_DIR", missing.c_str(), 1) == 0);
            const auto result = run_case("init-failed", root / "second");
            CHECK(setenv("MANTIS_RECORDER_OBSERVE_DIR", original.c_str(), 1) == 0);
            CHECK(result == 0);
            return 0;
        }
        if (mode == "overflow" || mode == "crash") {
            auto *trace =
                recorder_diagnostics::open(&root, {{"observer-control"}, {{"controlled-camera"}}, {}});
            CHECK(trace);
            if (mode == "crash")
                std::_Exit(27);
            {
                recorder_diagnostics::Worker worker(trace, 0);
                for (unsigned i = 0; i < 20000; ++i)
                    recorder_diagnostics::event("overflow.control");
            }
            recorder_diagnostics::cleanup(&root, false);
            recorder_diagnostics::cleanup(&root, true);
            return 0;
        }
        Control c;
        if (mode == "collision") {
            const auto *directory = std::getenv("MANTIS_RECORDER_OBSERVE_DIR");
            CHECK(directory);
            const auto marker = std::filesystem::path(directory) / (std::to_string(getpid()) + "-1.started");
            std::ofstream prior(marker);
            prior << "retained prior observation\n";
            prior.close();
            CHECK(prior);
        }
        c.hold = mode == "boundary";
        c.noisy = mode == "storage" || mode == "writer";
        c.descriptor_fault = mode == "source-observer";
        if (mode == "source-throw" || mode == "source-error")
            c.terminal = mode;
        control = &c;
        recorder_diagnostics::boundary_hook(boundary);
        const bool queue = mode == "queue";
        const bool segmented = mode == "segmented" || mode == "segmented-unsealed";
        const auto count = queue ? 40 : mode == "segmented" ? 24 : mode == "segmented-unsealed" ? 4 : 8;
        Source source(c, count, segmented);
        auto store = std::make_shared<artifact::Store>(root / "Fixture.mantis");
        artifact::Provenance provenance;
        if (segmented)
            provenance.parameters["segment_max_bytes"] = "4096";
        const auto raw = store->begin({"org.mantis.RawCapture", segmented ? 2u : 1u}, provenance);
        if (mode == "storage") {
            sqlite3 *db{};
            CHECK(sqlite3_open((root / "Fixture.mantis" / "project.sqlite").c_str(), &db) == SQLITE_OK);
            const auto result =
                sqlite3_exec(db,
                             "CREATE TRIGGER recorder_fault BEFORE INSERT ON chunks WHEN NEW.idx=3 BEGIN "
                             "SELECT RAISE(FAIL,'writer storage primary'); END",
                             nullptr, nullptr, nullptr);
            sqlite3_close(db);
            CHECK(result == SQLITE_OK);
        }
        Gate writer_entered, writer_release;
        std::vector<data::Published> accepted;
        std::unique_ptr<device::Session> session;
        // Release every gate before Session destruction joins, including assertions.
        struct Release {
            Control &c;
            Gate &writer;
            ~Release() {
                c.release();
                writer.open();
                recorder_diagnostics::boundary_hook(nullptr);
            }
        } release{c, writer_release};
        session = std::make_unique<device::Session>(
            device::CaptureDescriptor{{"controlled-" + mode}, {{"controlled-camera"}}, {}},
            std::vector<device::ImageStream *>{&source},
            [&](data::Published p) {
                if (queue && p->header.sequence.value == 1) {
                    writer_entered.open();
                    writer_release.wait();
                }
                if (mode == "writer" && p->header.sequence.value == 3)
                    throw std::runtime_error("writer primary");
                store->append(raw, *p);
                accepted.push_back(p);
                ++c.callbacks;
            },
            [&](const LogRecord &record) {
                if (record.component == "capture")
                    c.primary.open();
                if (c.noisy)
                    throw std::runtime_error("logger secondary failure");
            });
        if (mode == "serialization") {
            const auto *directory = std::getenv("MANTIS_RECORDER_OBSERVE_DIR");
            CHECK(directory);
            unsigned markers{};
            for (const auto &entry : std::filesystem::directory_iterator(directory)) {
                if (entry.path().extension() == ".started") {
                    auto blocked = entry.path();
                    blocked.replace_extension(".json.part");
                    std::filesystem::create_directory(blocked);
                    ++markers;
                }
            }
            CHECK(markers == 1);
        }
        c.produce.open();
        if (c.hold) {
            c.boundary_entered.wait();
            c.released_ns = time::MonotonicTimestamp::now().nanoseconds;
            c.boundary_release.open();
        }
        if (queue) {
            writer_entered.wait();
            c.primary.wait();
            writer_release.open();
        } else if (c.noisy)
            c.primary.wait();
        else
            c.ended.wait();
        c.finish.open();
        session->stop();
        auto d = session->diagnostics();
        const auto produced = session->produced(), callbacks = session->committed();
        const auto admitted = session->metrics().pushed;
        CHECK(source.stops == 1);
        CHECK(callbacks == accepted.size());
        CHECK(d.at("recorder_writer_running") == "0" && d.at("recorder_producer_running") == "0");
        if (queue) {
            CHECK(session->error() == "LOSSLESS writer queue saturated; capture failed explicitly");
            CHECK(produced == 35 && callbacks == 34 && session->saturation() == 1);
        } else if (c.noisy) {
            CHECK(session->error().find(mode == "storage" ? "writer storage primary" : "writer primary") !=
                  std::string::npos);
            CHECK(callbacks == 3 && d.at("recorder_secondary_error") == "cleanup secondary failure");
        } else if (!c.terminal.empty()) {
            CHECK(session->error() == "source primary" && produced == 8 && callbacks == 8);
        } else {
            CHECK(session->error().empty() && produced == static_cast<unsigned>(count) &&
                  callbacks == produced);
        }
        nlohmann::json segment_oracle;
        if (segmented) {
            // Independent Store oracle BEFORE finalize: only the actual indexed
            // segment files are scanned. No callback count implies durability.
            const auto descriptor = store->get(raw);
            segment_oracle = {
                {"capture", "controlled-" + mode},
                {"artifact", raw.value},
                {"schema", 2},
                {"segment_max_bytes", std::stoull(descriptor.provenance.parameters.at("segment_max_bytes"))},
                {"segments", nlohmann::json::array()}};
            for (uint64_t index = 0; index < descriptor.chunks; ++index) {
                const auto path = store->object_path(raw, index);
                const auto scan = artifact::segments::scan(path);
                CHECK(scan.corruption.empty() && !scan.incomplete && !scan.records.empty());
                CHECK(scan.complete_bytes == std::filesystem::file_size(path));
                const auto last = artifact::segments::packet(scan, scan.records.size() - 1);
                const auto &h = last->header;
                segment_oracle["segments"].push_back(
                    {{"artifact", raw.value},
                     {"index", index},
                     {"records", scan.records.size()},
                     {"bytes", scan.complete_bytes},
                     {"last_key",
                      {h.sequence.value, h.timestamp.nanoseconds, h.received.nanoseconds,
                       h.timestamp.domain.id.value}}});
            }
            store->finalize(raw);
            CHECK(store->record_count(raw) == callbacks);
        } else
            CHECK(store->get(raw).chunks == callbacks);
        const auto primary = session->error();
        if (mode == "collision") {
            const auto marker = std::filesystem::path(std::getenv("MANTIS_RECORDER_OBSERVE_DIR")) /
                                (std::to_string(getpid()) + "-1.started");
            std::ifstream prior(marker);
            std::string line;
            std::getline(prior, line);
            CHECK(line == "retained prior observation");
        }
        session.reset();
        store.reset();
        // Failed schema-1 operation can leave a complete, unacknowledged journal
        // tail. Clear the transactional fault before reopening; report it separately.
        if (mode == "storage") {
            sqlite3 *db{};
            CHECK(sqlite3_open((root / "Fixture.mantis" / "project.sqlite").c_str(), &db) == SQLITE_OK);
            CHECK(sqlite3_exec(db, "DROP TRIGGER recorder_fault", nullptr, nullptr, nullptr) == SQLITE_OK);
            sqlite3_close(db);
        }
        store = std::make_shared<artifact::Store>(root / "Fixture.mantis");
        const auto recovered_count = segmented ? store->record_count(raw) : store->get(raw).chunks;
        CHECK(recovered_count == callbacks + (mode == "storage" ? 1 : 0));
        if (!segmented)
            store->recover(raw);
        const auto first = replay(store, raw, recovered_count);
        CHECK(replay(store, raw, recovered_count) == first);
        nlohmann::json out{{"mode", mode},
                           {"pid", getpid()},
                           {"capture", "controlled-" + mode},
                           {"segment_oracle", segment_oracle},
                           {"produced", produced},
                           {"callbacks", callbacks},
                           {"admitted", admitted},
                           {"schema1_acknowledged", segmented ? 0 : callbacks},
                           {"recovered", recovered_count},
                           {"two_replays", true},
                           {"primary", primary},
                           {"gate_entered_ns", c.entered_ns.load()},
                           {"gate_release_ns", c.released_ns.load()},
                           {"old_append_ns", d.at("recorder_max_append_ns")}};
        std::cout << out.dump() << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "writer diagnostic regression: " << e.what() << '\n';
        return 1;
    }
}
int main(int argc, char **argv) {
    if (argc != 3)
        return 1;
    return run_case(argv[1], argv[2]);
}
