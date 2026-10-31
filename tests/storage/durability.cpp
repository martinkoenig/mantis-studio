#include "../../src/artifact-store/segments.hpp"
#include "../unit/projected_storage_values.hpp"
#include "faults.hpp"
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fstream>
#include <future>
#include <iostream>
#include <mantis/artifact_store.hpp>
#include <sys/wait.h>
#include <unistd.h>
using namespace mantis;
using namespace storage_fixture;
namespace st = mantis::artifact::storage_test;
using namespace std::chrono_literals;
#define CHECK(...)                                                                                           \
    do {                                                                                                     \
        if (!(__VA_ARGS__))                                                                                  \
            throw std::runtime_error("Line " + std::to_string(__LINE__) + ": " #__VA_ARGS__);                \
    } while (false)
namespace {
using Clock = std::chrono::steady_clock;
struct Directory {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() / ("mantis-durability-" + Id::random().value);
    Directory() {
        std::filesystem::create_directory(path);
    }
    ~Directory() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};
struct Pipe {
    int fd[2]{-1, -1};
    Pipe() {
        CHECK(::pipe(fd) == 0);
    }
    ~Pipe() {
        for (int value : fd)
            if (value >= 0)
                ::close(value);
    }
    void close(unsigned i) {
        if (fd[i] >= 0)
            ::close(std::exchange(fd[i], -1));
    }
};
std::string bytes(const std::filesystem::path &p) {
    std::ifstream in(p, std::ios::binary);
    CHECK(in.good());
    return {std::istreambuf_iterator<char>(in), {}};
}
void write(const std::filesystem::path &p, std::string_view value, bool append = false) {
    std::ofstream out(p, std::ios::binary | (append ? std::ios::app : std::ios::trunc));
    out.write(value.data(), static_cast<std::streamsize>(value.size()));
    out.close();
    CHECK(out.good());
}
std::string record(unsigned n) {
    std::ostringstream out;
    artifact::segments::append(out, bundle(n));
    return out.str();
}
artifact::Provenance provenance() {
    artifact::Provenance p;
    p.producer = "durability-independent-fixture";
    p.parameters = {{"segment_max_bytes", "4096"}, {"rig_revision", "fixture-7"}};
    return p;
}
template <class F> void rejects(F f) {
    bool rejected = false;
    try {
        f();
    } catch (const Failure &e) {
        CHECK(e.error.code != Status::ok);
        rejected = true;
    } catch (const std::filesystem::filesystem_error &e) {
        CHECK(e.code().value() != 0);
        rejected = true;
    }
    CHECK(rejected);
}
template <class F> void io_failure(F f) {
    bool rejected = false;
    try {
        f();
    } catch (const Failure &e) {
        CHECK(e.error.code == Status::io);
        rejected = true;
    } catch (const std::filesystem::filesystem_error &e) {
        CHECK(e.code().value() != 0);
        rejected = true;
    }
    CHECK(rejected);
}
void exact(artifact::Store &store, const Id &id, unsigned count, bool final_outcome) {
    CHECK(encode(store.capture_header(id)) == encode(header()));
    auto a = store.get(id);
    CHECK(a.state == artifact::ArtifactState::finalized);
    CHECK(a.provenance.producer == provenance().producer &&
          a.provenance.parameters == provenance().parameters);
    auto summary = store.bundle_summary(id);
    CHECK(summary.records == count);
    auto outcome_record = store.run_outcome(id);
    CHECK(bool(outcome_record) == final_outcome);
    if (outcome_record)
        CHECK(encode(data::ProjectedCaptureOutcome{count, *outcome_record}) ==
              encode(data::ProjectedCaptureOutcome{count, outcome()}));
    for (unsigned pass = 0; pass < 2; ++pass) {
        unsigned n = 0;
        store.replay_bundles(id, [&](data::AcquisitionBundle b) {
            CHECK(n < count);
            CHECK(encode(b) == encode(bundle(n++)));
        });
        CHECK(n == count);
    }
    auto before = a.hash;
    auto size = a.bytes;
    auto chunks = a.chunks;
    rejects([&] { store.recover(id); });
    auto after = store.get(id);
    CHECK(after.hash == before && after.bytes == size && after.chunks == chunks && after.state == a.state);
    CHECK(encode(store.capture_header(id)) == encode(header()));
    CHECK(bool(store.run_outcome(id)) == final_outcome);
    unsigned n = 0;
    store.replay_bundles(id, [&](data::AcquisitionBundle b) { CHECK(encode(b) == encode(bundle(n++))); });
    CHECK(n == count);
}
void prefix(artifact::Store &store, const Id &id) {
    store.append_bundle(id, bundle(0));
    store.append_bundle(id, bundle(1));
    CHECK(store.get(id).chunks == 1);
}
data::Published raw_frame() {
    data::Packet image;
    image.type = schema::image;
    memory::BufferBuilder pixels(4);
    image.attributes.push_back(
        {{"org.mantis.pixels", schema::ScalarType::u8, {2, 2}, {2, 1}, ""}, std::move(pixels).publish()});
    data::Packet parent;
    parent.type = schema::frameset;
    parent.frames = {data::publish(std::move(image))};
    return data::publish(std::move(parent));
}
std::string raw_bytes(const data::Packet &packet) {
    std::ostringstream out(std::ios::binary);
    data::write_packet(out, packet);
    return out.str();
}
void raw_first_append_stages() {
    const std::array<const char *, 5> stages{"raw-first-provenance-before", "raw-first-writer-init-before",
                                             "raw-first-segment-open-before",
                                             "raw-first-directory-sync-before", "raw-first-record-before"};
    for (size_t stage = 0; stage < stages.size(); ++stage) {
        Directory dir;
        artifact::Store store(dir.path);
        auto packet = raw_frame();
        const auto id = store.begin({"org.mantis.RawCapture", 2}, {});
        {
            st::Arm fault({dir.path, id.value, stages[stage], st::Action::fail, 0, 0, EIO});
            bool rejected{};
            try {
                store.append(id, *packet);
            } catch (const Failure &failure) {
                rejected = true;
                CHECK(failure.error.code == Status::io);
                CHECK(failure.error.message.find(std::string("Injected ") + stages[stage]) == 0);
            }
            CHECK(rejected && fault.metrics().hits == 1);
        }
        const auto evidence = store.raw_append_diagnostics(id);
        CHECK(evidence.at("raw_first_append_complete") == "0");
        CHECK(evidence.at("raw_first_append_failed_stage") == std::to_string(stage));
        CHECK(std::stoll(evidence.at("raw_first_append_ns")) >= 0);
        CHECK(store.get(id).state == artifact::ArtifactState::open);
        store.abandon(id);
        CHECK(store.get(id).state == artifact::ArtifactState::recoverable);
        const auto healthy = store.begin({"org.mantis.RawCapture", 2}, {});
        Pipe notification, release;
        st::Plan plan{dir.path, healthy.value, stages[stage], st::Action::pause};
        plan.notify = notification.fd[1];
        plan.release = release.fd[0];
        st::Arm held(plan);
        std::future<void> pending;
        struct Release {
            int fd;
            bool opened{};
            void open() {
                if (!opened) {
                    st::release_gate(fd);
                    opened = true;
                }
            }
            ~Release() {
                try {
                    open();
                } catch (...) {
                }
            }
        } guard{release.fd[1]};
        pending = std::async(std::launch::async, [&] { store.append(healthy, *packet); });
        auto event = st::await_event(notification.fd[0], Clock::now() + 10s);
        CHECK(std::string(event.id) == healthy.value && std::string(event.point) == stages[stage]);
        CHECK(pending.wait_for(0s) == std::future_status::timeout);
        guard.open();
        CHECK(pending.wait_for(10s) == std::future_status::ready);
        pending.get();
        const auto resumed = store.raw_append_diagnostics(healthy);
        CHECK(resumed.at("raw_first_append_complete") == "1");
        CHECK(resumed.at("raw_first_append_failed_stage") == "-1");
        CHECK(held.metrics().hits == 1);
        store.finalize(healthy);
        for (unsigned pass = 0; pass < 2; ++pass) {
            unsigned records{};
            store.replay(healthy, [&](data::Published observed) {
                CHECK(raw_bytes(*observed) == raw_bytes(*packet));
                ++records;
            });
            CHECK(records == 1);
        }
        std::cout << "RawCapture2 first-append stage fault/held-request/recovery: " << stages[stage] << '\n';
    }
}
void failures() {
    struct Fault {
        const char *name, *point;
        st::Action action;
        uint64_t bytes{}, skip{};
        bool outcome{};
    };
    const Fault cases[]{
        {"short-enospc", "record-write", st::Action::short_write, 37},
        {"write-eio", "record-write", st::Action::fail},
        {"file-sync", "segment-file-sync", st::Action::syscall_error},
        {"directory-sync", "directory-sync", st::Action::syscall_error, 0, 1},
        {"rename", "unused", st::Action::fail}, // Actual rename fails against our owned directory.
        {"chunk-commit", "chunk-commit-before", st::Action::reject_commit},
        {"outcome-sync", "outcome-file-sync", st::Action::syscall_error, 0, 0, true},
        {"outcome-rename", "outcome-rename-before", st::Action::obstruct_publication, 0, 0, true},
        {"finalize-commit", "finalize-commit-before", st::Action::reject_commit, 0, 0, true},
        {"outcome-after-publish", "outcome-published", st::Action::fail, 0, 0, true},
    };
    for (const auto &scenario : cases) {
        std::cout << "fault " << scenario.name << std::endl;
        Directory dir;
        Id id;
        unsigned expected = 3;
        bool final = false;
        {
            artifact::Store store(dir.path);
            id = store.begin_projected_capture(header(), provenance());
            prefix(store, id);
            auto object = dir.path / "objects" / id.value;
            auto committed = bytes(object / "0.segment");
            std::string_view name = scenario.name;
            st::Plan plan{dir.path, id.value, scenario.point, scenario.action};
            plan.error = name == "short-enospc" ? ENOSPC : EIO;
            plan.bytes = scenario.bytes;
            plan.skip = scenario.skip;
            st::Arm fault(plan);
            if (name == "rename")
                std::filesystem::create_directory(object / "1.segment");
            io_failure([&] {
                store.append_bundle(id, bundle(2));
                store.record_run_outcome(id, {3, outcome()});
                store.finalize(id);
            });
            if (name == "outcome-rename")
                std::filesystem::remove(object / "run.outcome"); // Remove only our publication obstruction.
            CHECK(store.get(id).state == artifact::ArtifactState::recoverable);
            CHECK(bytes(object / "0.segment") == committed);
            if (name == "short-enospc" || name == "write-eio") {
                auto tail = bytes(object / "1.segment.part");
                auto m = fault.metrics();
                CHECK(m.hits == 1 && m.short_results == 1 && m.error == plan.error);
                CHECK(tail.size() == plan.bytes && m.written == plan.bytes);
                CHECK(record(2).substr(0, tail.size()) == tail);
                auto scan = artifact::segments::scan(object / "1.segment.part", 3);
                CHECK(scan.incomplete && scan.records.empty() && scan.corruption.empty());
                expected = 2;
            } else if (name != "rename" && name != "outcome-rename")
                CHECK(fault.metrics().hits == 1);
            if (name == "chunk-commit" || name == "finalize-commit")
                CHECK(fault.metrics().rejected_commits == 1);
            if (name == "chunk-commit")
                CHECK(store.get(id).chunks == 1 && std::filesystem::is_regular_file(object / "1.segment"));
            if (name == "rename")
                std::filesystem::remove(object / "1.segment");
            final = scenario.outcome;
            if (!final)
                CHECK(!std::filesystem::exists(object / "run.outcome"));
        }
        artifact::Store reopened(dir.path);
        CHECK(reopened.get(id).state == artifact::ArtifactState::recoverable);
        reopened.recover(id);
        exact(reopened, id, expected, final);
    }
    // Plan scoping: another artifact in the same Store and another Store both succeed.
    Directory a, b;
    artifact::Store first(a.path), second(b.path);
    auto target = first.begin_projected_capture(header(), provenance());
    auto unrelated = first.begin_projected_capture(header(), provenance());
    st::Arm fault({a.path, target.value, "record-write", st::Action::short_write, 0, 37, ENOSPC});
    auto other = std::async(std::launch::async, [&] {
        auto id = second.begin_projected_capture(header(), provenance());
        prefix(second, id);
        second.record_run_outcome(id, {2, outcome()});
        second.finalize(id);
        exact(second, id, 2, true);
    });
    prefix(first, unrelated);
    first.record_run_outcome(unrelated, {2, outcome()});
    first.finalize(unrelated);
    exact(first, unrelated, 2, true);
    io_failure([&] { first.append_bundle(target, bundle(0)); });
    other.get();
    CHECK(fault.metrics().hits == 1);
    // Header sync failure reports a row that cannot be recovered without a published header.
    Directory init;
    st::Arm failed({init.path, {}, "header-file-sync", st::Action::syscall_error, 0, 0, EIO});
    {
        artifact::Store store(init.path);
        io_failure([&] { store.begin_projected_capture(header(), provenance()); });
        CHECK(store.list().size() == 1);
        auto id = store.list()[0].id;
        CHECK(store.get(id).state == artifact::ArtifactState::recoverable);
        CHECK(!std::filesystem::exists(init.path / "objects" / id.value / "run.header"));
        rejects([&] { store.recover(id); });
    }
}
struct Child {
    pid_t pid{-1};
    ~Child() {
        if (pid > 0) {
            ::kill(pid, SIGKILL);
            int status;
            while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
            }
        }
    }
    void kill() {
        CHECK(pid > 0);
        CHECK(::kill(pid, SIGKILL) == 0);
        int status;
        pid_t n;
        do {
            n = ::waitpid(pid, &status, 0);
        } while (n < 0 && errno == EINTR);
        CHECK(n == pid);
        pid = -1;
        CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL);
    }
};
struct Crash {
    const char *point;
    unsigned count;
    bool published_header, outcome, finalized;
    uint64_t skip{};
};
void crashes(const char *executable) {
    for (const auto &c : std::array<Crash, 13>{{{"artifact-row", 0, false, false, false},
                                                {"header-synced", 0, false, false, false},
                                                {"header-published", 0, true, false, false},
                                                {"record-written", 2, true, false, false, 1},
                                                {"record-write", 2, true, false, false},
                                                {"segment-synced", 2, true, false, false},
                                                {"segment-published", 2, true, false, false},
                                                {"chunk-commit-before", 2, true, false, false},
                                                {"chunk-committed", 2, true, false, false},
                                                {"outcome-written", 4, true, true, false},
                                                {"outcome-published", 4, true, true, false},
                                                {"finalize-commit-before", 4, true, true, false},
                                                {"finalize-committed", 4, true, true, true}}}) {
        std::cout << "crash " << c.point << std::endl;
        Directory dir;
        Pipe notification, release;
        Child child;
        child.pid = ::fork();
        CHECK(child.pid >= 0);
        if (child.pid == 0) {
            notification.close(0);
            release.close(1);
            auto n = std::to_string(notification.fd[1]), r = std::to_string(release.fd[0]),
                 skip = std::to_string(c.skip);
            ::execl(executable, executable, "--child", c.point, dir.path.c_str(), n.c_str(), r.c_str(),
                    skip.c_str(), nullptr);
            ::_exit(127);
        }
        notification.close(1);
        release.close(0);
        auto event = st::await_event(notification.fd[0], Clock::now() + 10s);
        CHECK(std::string(event.point) == c.point && *event.id);
        Id id{event.id};
        child.kill();
        artifact::Store store(dir.path);
        CHECK(store.list().size() == 1);
        auto object = dir.path / "objects" / id.value;
        CHECK(store.get(id).state ==
              (c.finalized ? artifact::ArtifactState::finalized : artifact::ArtifactState::recoverable));
        if (std::string_view(c.point) == "chunk-commit-before")
            CHECK(store.get(id).chunks == 0); // Uncommitted SQLite transaction rolled back.
        if (std::string_view(c.point) == "chunk-committed")
            CHECK(store.get(id).chunks == 1);
        if (!c.published_header) {
            CHECK(!std::filesystem::exists(object / "run.header"));
            if (std::string(c.point) == "header-synced")
                CHECK(bytes(object / "run.header.part") == encode(header()));
            rejects([&] { store.recover(id); });
            CHECK(store.get(id).state == artifact::ArtifactState::recoverable);
            continue;
        }
        CHECK(bytes(object / "run.header") == encode(header()));
        if (!c.finalized)
            store.recover(id);
        exact(store, id, c.count, c.outcome);
    }
}
int child(int argc, char **argv) {
    CHECK(argc == 7);
    st::Plan p{argv[3], {}, argv[2], st::Action::pause};
    p.notify = std::stoi(argv[4]);
    p.release = std::stoi(argv[5]);
    p.skip = std::stoull(argv[6]);
    if (p.point == "record-write") {
        artifact::Store store(p.root);
        auto id = store.begin_projected_capture(header(), provenance());
        prefix(store, id);
        p.id = id.value;
        p.action = st::Action::short_pause;
        p.bytes = 37;
        st::Arm fault(p);
        store.append_bundle(id, bundle(2));
        throw std::runtime_error("Partial write checkpoint never paused");
    }
    st::Arm fault(p);
    artifact::Store store(p.root);
    auto id = store.begin_projected_capture(header(), provenance());
    for (unsigned n = 0; n < 4; ++n)
        store.append_bundle(id, bundle(n));
    store.record_run_outcome(id, {4, outcome()});
    store.finalize(id);
    throw std::runtime_error("Crash checkpoint never reached: " + p.point);
}
void retry() {
    std::cout << "recovery cancellation/retry/corruption" << std::endl;
    Directory dir;
    Id id;
    {
        artifact::Store store(dir.path);
        id = store.begin_projected_capture(header(), provenance());
        prefix(store, id);
        store.append_bundle(id, bundle(2));
        store.abandon(id);
    }
    auto part = dir.path / "objects" / id.value / "1.segment.part";
    write(part, record(3).substr(0, 37), true);
    auto before = bytes(part);
    artifact::Store store(dir.path);
    CancellationToken token;
    Pipe notification, release;
    {
        st::Plan p{dir.path, id.value, "recovery-scanned", st::Action::pause};
        p.notify = notification.fd[1];
        p.release = release.fd[0];
        st::Arm gate(p);
        auto pending = std::async(std::launch::async, [&] {
            try {
                store.recover(id, token);
                return Status::ok;
            } catch (const Failure &e) {
                return e.error.code;
            }
        });
        // Always release a gated worker even if an assertion/notification fails.
        struct Release {
            int fd;
            ~Release() {
                try {
                    st::release_gate(fd);
                } catch (...) {
                }
            }
        } guard{release.fd[1]};
        auto event = st::await_event(notification.fd[0], Clock::now() + 10s);
        CHECK(std::string(event.id) == id.value);
        token.cancel();
        st::release_gate(release.fd[1]);
        CHECK(pending.wait_for(2s) == std::future_status::ready && pending.get() == Status::cancelled);
        CHECK(store.get(id).state == artifact::ArtifactState::recoverable && bytes(part) == before);
    }
    store.recover(id);
    exact(store, id, 3, false);
    CHECK(bytes(dir.path / "objects" / id.value / "1.segment") == record(2));
    Directory bad;
    Id corrupted;
    {
        artifact::Store s(bad.path);
        corrupted = s.begin_projected_capture(header(), provenance());
        prefix(s, corrupted);
        s.append_bundle(corrupted, bundle(2));
        s.abandon(corrupted);
    }
    auto tail = bad.path / "objects" / corrupted.value / "1.segment.part";
    auto corrupt = record(3);
    corrupt[16] ^= 1;
    write(tail, corrupt, true);
    auto retained = bytes(tail);
    auto sealed = bad.path / "objects" / corrupted.value / "0.segment";
    const auto committed = bytes(sealed);
    auto scan = artifact::segments::scan(sealed, 3);
    CHECK(scan.records.size() == 2 && !scan.incomplete && scan.corruption.empty());
    for (size_t n = 0; n < 2; ++n)
        CHECK(encode(artifact::segments::bundle(scan, n)) == encode(bundle(static_cast<unsigned>(n))));
    artifact::Store s(bad.path);
    for (unsigned attempt = 0; attempt < 2; ++attempt) {
        bool rejected = false;
        try {
            s.recover(corrupted);
        } catch (const Failure &e) {
            CHECK(e.error.code == Status::corrupt);
            rejected = true;
        }
        CHECK(rejected && s.get(corrupted).chunks == 1 && bytes(sealed) == committed);
        CHECK(s.get(corrupted).state == artifact::ArtifactState::recoverable && bytes(tail) == retained);
    }
}
} // namespace
int main(int argc, char **argv) {
    try {
        ::signal(SIGPIPE, SIG_IGN);
        if (argc > 1)
            return child(argc, argv);
        CHECK(record(0).size() < 4096 && record(0).size() + record(1).size() > 4096);
        raw_first_append_stages();
        failures();
        crashes(std::filesystem::absolute(argv[0]).c_str());
        retry();
        std::cout << "RawCapture3 storage faults, 13 synchronized SIGKILL windows and recovery retry PASS\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
