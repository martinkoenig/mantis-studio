#include "faults.hpp"
#include <cerrno>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <mantis/artifact_store.hpp>
#include <mantis/data_io.hpp>
#include <mantis/platform.hpp>
#include <nlohmann/json.hpp>
#include <sqlite3.h>
#include <sstream>
#include <unistd.h>
using namespace mantis;
namespace st = artifact::storage_test;
using Json = nlohmann::json;
#define CHECK(...)                                                                                           \
    do {                                                                                                     \
        if (!(__VA_ARGS__))                                                                                  \
            throw std::runtime_error("Line " + std::to_string(__LINE__) + ": " #__VA_ARGS__);                \
    } while (false)
namespace {
std::string read(const std::filesystem::path &p) {
    std::ifstream in(p, std::ios::binary);
    CHECK(in.good());
    return {std::istreambuf_iterator<char>(in), {}};
}
void write(const std::filesystem::path &p, std::string_view bytes) {
    std::ofstream out(p, std::ios::binary);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    out.close();
    CHECK(out.good());
    platform::durable_file(p);
    platform::durable_directory(p.parent_path());
}
std::string digest(std::string_view bytes) {
    uint64_t value = 14695981039346656037ULL;
    for (unsigned char byte : bytes) {
        value ^= byte;
        value *= 1099511628211ULL;
    }
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(16) << value;
    return out.str();
}
data::Published packet(uint64_t n) {
    data::Packet p;
    p.type = schema::image;
    p.header.sequence.value = n;
    p.header.timestamp = {static_cast<int64_t>(n * 123), {{"journal-clock"}, "fixture"}};
    p.header.metadata = {{"identity", "independent-prefix-tail"}};
    memory::BufferBuilder bytes(4);
    for (size_t i = 0; i < 4; ++i)
        bytes.writable()[i] = static_cast<std::byte>(n * 17 + i);
    p.attributes = {
        {{"org.mantis.pixels", schema::ScalarType::u8, {4}, {1}, "intensity"}, std::move(bytes).publish()}};
    return data::publish(std::move(p));
}
std::string encode(const data::Packet &p) {
    std::ostringstream out(std::ios::binary);
    data::write_packet(out, p);
    return out.str();
}
template <class F> std::string rejects(F &&f, Status status) {
    try {
        f();
    } catch (const Failure &e) {
        CHECK(e.error.code == status);
        return e.error.message;
    }
    throw std::runtime_error("Expected explicit failure was not reported");
}
struct Database {
    sqlite3 *db{};
    explicit Database(const std::filesystem::path &root) {
        CHECK(sqlite3_open((root / "project.sqlite").c_str(), &db) == SQLITE_OK);
    }
    ~Database() {
        sqlite3_close(db);
    }
    void exec(const std::string &sql) {
        CHECK(sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK);
    }
    std::vector<std::vector<std::string>> rows(const std::string &sql) {
        sqlite3_stmt *statement{};
        CHECK(sqlite3_prepare_v2(db, sql.c_str(), -1, &statement, nullptr) == SQLITE_OK);
        struct Finalize {
            sqlite3_stmt *s;
            ~Finalize() {
                sqlite3_finalize(s);
            }
        } cleanup{statement};
        std::vector<std::vector<std::string>> result;
        int rc{};
        while ((rc = sqlite3_step(statement)) == SQLITE_ROW) {
            std::vector<std::string> row;
            for (int i = 0; i < sqlite3_column_count(statement); ++i)
                row.emplace_back(reinterpret_cast<const char *>(sqlite3_column_text(statement, i)));
            result.push_back(std::move(row));
        }
        CHECK(rc == SQLITE_DONE);
        return result;
    }
};
struct Fixture {
    std::filesystem::path root = std::filesystem::temp_directory_path() / ("journal-" + Id::random().value);
    Id id;
    const std::array<std::string, 2> expected{encode(*packet(0)), encode(*packet(1))};
    std::filesystem::path prefix, tail, journal, invalid;
    std::string journal_bytes;
    explicit Fixture(bool pending = true) {
        artifact::Store store(root);
        id = store.begin({"org.mantis.RawCapture", 1}, {});
        store.append(id, *packet(0));
        prefix = root / "objects" / id.value / "0.packet";
        tail = prefix.parent_path() / "1.packet";
        journal = root / "journal" / (id.value + "_1.json");
        invalid = journal.string() + ".invalid";
        if (pending) {
            st::Arm fault({root, id.value, "chunk-commit-before", st::Action::reject_commit});
            CHECK(rejects([&] { store.append(id, *packet(1)); }, Status::io).find("constraint") !=
                  std::string::npos);
            CHECK(fault.metrics().hits == 1 && fault.metrics().rejected_commits == 1);
            journal_bytes = read(journal);
            CHECK(read(tail) == expected[1]);
            rejects([&] { store.append(id, *packet(1)); }, Status::busy);
        }
        store.abandon(id);
        prefix_only();
    }
    ~Fixture() {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }
    void prefix_only() const {
        CHECK(read(prefix) == expected[0]);
        Database db(root);
        CHECK(
            db.rows("SELECT artifact,idx,path,hash,bytes FROM chunks ORDER BY idx") ==
            std::vector<std::vector<std::string>>{{id.value, "0", "objects/" + id.value + "/0.packet",
                                                   digest(expected[0]), std::to_string(expected[0].size())}});
        CHECK(
            db.rows("SELECT id,chunks,bytes,state FROM artifacts") ==
            std::vector<std::vector<std::string>>{{id.value, "1", std::to_string(expected[0].size()), "3"}});
    }
    void retained() const {
        prefix_only();
        CHECK(read(journal) == journal_bytes);
        CHECK(!std::filesystem::exists(invalid));
    }
    void exact(artifact::Store &store) const {
        const auto a = store.get(id);
        CHECK(a.id == id && a.state == artifact::ArtifactState::finalized && a.chunks == 2);
        CHECK(a.bytes == expected[0].size() + expected[1].size());
        CHECK(a.hash.hex == digest(digest(expected[0]) + digest(expected[1])));
        CHECK(read(prefix) == expected[0] && read(tail) == expected[1]);
        Database db(root);
        CHECK(
            db.rows("SELECT artifact,idx,path,hash,bytes FROM chunks ORDER BY idx") ==
            std::vector<std::vector<std::string>>{{id.value, "0", "objects/" + id.value + "/0.packet",
                                                   digest(expected[0]), std::to_string(expected[0].size())},
                                                  {id.value, "1", "objects/" + id.value + "/1.packet",
                                                   digest(expected[1]), std::to_string(expected[1].size())}});
        for (unsigned pass = 0; pass < 2; ++pass) {
            size_t n{};
            store.replay(id, [&](data::Published value) {
                CHECK(n < expected.size());
                CHECK(encode(*value) == expected[n++]);
            });
            CHECK(n == 2);
        }
        CHECK(!std::filesystem::exists(journal) && !std::filesystem::exists(invalid));
        rejects([&] { store.recover(id); }, Status::invalid_argument);
        CHECK(store.get(id).hash == a.hash && store.get(id).bytes == a.bytes);
    }
    void finish() const {
        {
            artifact::Store store(root);
            CHECK(store.get(id).chunks == 2 && store.get(id).state == artifact::ArtifactState::recoverable);
            store.recover(id);
            exact(store);
        }
        artifact::Store reopened(root);
        exact(reopened);
    }
};
void rollback() {
    Fixture f;
    // Decisive original-bug oracle: actual replay reaches the real SQLite
    // transaction, commit-hook rejection rolls it back, and opening MUST fail.
    for (unsigned restart = 0; restart < 2; ++restart) {
        st::Arm fault({f.root, f.id.value, "chunk-commit-before", st::Action::reject_commit});
        bool failed{};
        try {
            artifact::Store open(f.root);
        } catch (const Failure &error) {
            CHECK(error.error.code == Status::io &&
                  error.error.message.find("constraint") != std::string::npos);
            failed = true;
        }
        CHECK(fault.metrics().hits == 1 && fault.metrics().rejected_commits == 1);
        CHECK(!std::filesystem::exists(f.invalid)); // Original broad catch fails this oracle.
        CHECK(failed);
        f.retained();
        CHECK(read(f.tail) == f.expected[1]);
    }
    f.finish();
}
void same_store() {
    Fixture f(false);
    artifact::Store store(f.root);
    // Manufacture the same complete durable tail after opening, so recovery
    // and ordinary finalization must not rely solely on constructor replay.
    write(f.tail, f.expected[1]);
    f.journal_bytes = Json{{"id", f.id.value},
                           {"index", 1},
                           {"path", "objects/" + f.id.value + "/1.packet"},
                           {"hash", digest(f.expected[1])},
                           {"bytes", f.expected[1].size()}}
                          .dump();
    write(f.journal, f.journal_bytes);
    for (bool recovery : {false, true}) {
        st::Arm fault({f.root, f.id.value, "chunk-commit-before", st::Action::reject_commit});
        rejects([&] { recovery ? store.recover(f.id) : store.finalize(f.id); }, Status::io);
        CHECK(fault.metrics().rejected_commits == 1);
        f.retained();
    }
    store.recover(f.id);
    f.exact(store);
}
void committed_before_cleanup() {
    Fixture f;
    {
        st::Arm fault({f.root, f.id.value, "chunk-committed", st::Action::fail, 0, 0, EIO});
        rejects([&] { artifact::Store open(f.root); }, Status::io);
        CHECK(fault.metrics().hits == 1);
    }
    CHECK(read(f.prefix) == f.expected[0] && read(f.tail) == f.expected[1]);
    CHECK(read(f.journal) == f.journal_bytes && !std::filesystem::exists(f.invalid));
    Database db(f.root);
    CHECK(db.rows("SELECT chunks,bytes,state FROM artifacts") ==
          std::vector<std::vector<std::string>>{
              {"2", std::to_string(f.expected[0].size() + f.expected[1].size()), "3"}});
    st::Arm no_second_commit({f.root, f.id.value, "chunk-commit-before", st::Action::reject_commit});
    f.finish();
    CHECK(no_second_commit.metrics().hits == 0);
}
void operational() {
    for (int error : {EIO, ENOSPC}) {
        Fixture f;
        {
            // The real platform fsync is intercepted for this artifact only.
            st::Arm fault({f.root, f.id.value, "directory-sync", st::Action::syscall_error, 0, 0, error});
            rejects([&] { artifact::Store open(f.root); }, Status::io);
            CHECK(fault.metrics().hits == 1 && fault.metrics().error == error);
        }
        f.retained();
        f.finish();
    }
    Fixture f;
    {
        Database lock(f.root);
        lock.exec("BEGIN IMMEDIATE");
        st::Trace trace(f.root, f.id.value);
        // A real competing SQLite writer produces SQLITE_BUSY after the
        // existing bounded busy timeout, not an exception in a mock wrapper.
        CHECK(rejects([&] { artifact::Store open(f.root); }, Status::io).find("locked") != std::string::npos);
        CHECK(std::any_of(trace.samples.begin(), trace.samples.begin() + trace.count, [](const auto &sample) {
            return std::string_view(sample.point) == "journal-validate-before";
        })); // BUSY came from actual replay, rather than earlier project setup.
        lock.exec("ROLLBACK");
    }
    f.retained();
    f.finish();
}
void interrupted_publication() {
    for (const auto *point : {"journal-rename-before", "journal-renamed"}) {
        Fixture f;
        const auto part = std::filesystem::path(f.tail.string() + ".part");
        std::filesystem::rename(f.tail, part);
        platform::durable_directory(part.parent_path());
        {
            st::Arm fault({f.root, f.id.value, point, st::Action::fail, 0, 0, EIO});
            rejects([&] { artifact::Store open(f.root); }, Status::io);
            CHECK(fault.metrics().hits == 1);
        }
        f.retained();
        CHECK(read(std::filesystem::exists(part) ? part : f.tail) == f.expected[1]);
        f.finish();
    }
}
void corrupt_evidence() {
    for (const std::string mode : {"json", "hash", "bytes", "packet", "missing", "part", "traversal",
                                   "identity", "expected-path", "negative-index", "symlink"}) {
        Fixture f;
        auto j = Json::parse(f.journal_bytes);
        const auto sentinel = f.root / "sentinel.packet";
        write(sentinel, f.expected[1]);
        if (mode == "hash")
            j["hash"] = "0000000000000000";
        if (mode == "bytes")
            j["bytes"] = f.expected[1].size() + 1;
        if (mode == "packet") {
            write(f.tail, "complete but invalid packet");
            j["hash"] = digest(read(f.tail));
            j["bytes"] = read(f.tail).size();
        }
        if (mode == "missing")
            std::filesystem::remove(f.tail);
        if (mode == "part") {
            std::filesystem::rename(f.tail, f.tail.string() + ".part");
            std::filesystem::resize_file(f.tail.string() + ".part", f.expected[1].size() / 2);
        }
        if (mode == "traversal")
            j["path"] = "../../sentinel.packet";
        if (mode == "identity")
            j["id"] = "../outside";
        if (mode == "expected-path")
            j["path"] = "objects/" + f.id.value + "/0.packet";
        if (mode == "negative-index")
            j["index"] = -1;
        if (mode == "symlink") {
            std::filesystem::remove(f.tail);
            std::filesystem::create_symlink(sentinel, f.tail);
        }
        const auto evidence = mode == "json" ? std::string("{") : j.dump();
        write(f.journal, evidence);
        for (unsigned restart = 0; restart < 2; ++restart) {
            artifact::Store store(f.root);
            f.prefix_only();
            CHECK(!std::filesystem::exists(f.journal) && read(f.invalid) == evidence);
            rejects([&] { store.recover(f.id); }, Status::corrupt);
            rejects([&] { store.finalize(f.id); }, Status::corrupt);
            f.prefix_only();
            CHECK(read(sentinel) == f.expected[1]);
        }
        if (mode == "part")
            CHECK(read(f.tail.string() + ".part") == f.expected[1].substr(0, f.expected[1].size() / 2));
    }
}
void stale_and_conflict() {
    for (bool conflict : {false, true}) {
        Fixture f;
        const auto stale = f.root / "journal" / (f.id.value + "_0.json");
        const auto text = Json{{"id", f.id.value},
                               {"index", 0},
                               {"path", "objects/" + f.id.value + "/0.packet"},
                               {"hash", digest(f.expected[0])},
                               {"bytes", f.expected[0].size()}}
                              .dump();
        write(stale, text);
        if (conflict) {
            Database db(f.root);
            db.exec("BEGIN IMMEDIATE; UPDATE chunks SET hash='0000000000000000' WHERE idx=0; COMMIT");
            const auto before = db.rows("SELECT * FROM chunks ORDER BY idx");
            rejects([&] { artifact::Store open(f.root); }, Status::corrupt);
            CHECK(db.rows("SELECT * FROM chunks ORDER BY idx") == before);
            CHECK(read(stale) == text && !std::filesystem::exists(stale.string() + ".invalid"));
            CHECK(read(f.journal) == f.journal_bytes && read(f.prefix) == f.expected[0]);
            db.exec("BEGIN IMMEDIATE; UPDATE chunks SET hash='" + digest(f.expected[0]) +
                    "' WHERE idx=0; COMMIT");
        }
        f.finish();
        // A stale committed journal is also safe after finalization, with no
        // metadata rewrite, byte loss or extra chunk registration.
        write(stale, text);
        artifact::Store store(f.root);
        f.exact(store);
        CHECK(!std::filesystem::exists(stale));
    }
    Fixture f;
    auto gap = Json::parse(f.journal_bytes);
    gap["index"] = 2;
    gap["path"] = "objects/" + f.id.value + "/2.packet";
    std::filesystem::rename(f.tail, f.tail.parent_path() / "2.packet");
    std::filesystem::remove(f.journal);
    const auto journal = f.root / "journal" / (f.id.value + "_2.json");
    write(journal, gap.dump());
    rejects([&] { artifact::Store open(f.root); }, Status::corrupt);
    f.prefix_only();
    CHECK(read(journal) == gap.dump() && !std::filesystem::exists(journal.string() + ".invalid"));
}
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
};
struct ReleaseIO {
    int fd;
    bool released{};
    void open() {
        if (!released) {
            st::release_gate(fd);
            released = true;
        }
    }
    ~ReleaseIO() {
        try {
            open();
        } catch (...) {
        } // Preserve assertion/worker failure; pipes outlive joins.
    }
};
void committed_cancellation() {
    using namespace std::chrono_literals;
    Fixture f(false);
    artifact::Store store(f.root);
    write(f.tail, f.expected[1]);
    f.journal_bytes = Json{{"id", f.id.value},
                           {"index", 1},
                           {"path", "objects/" + f.id.value + "/1.packet"},
                           {"hash", digest(f.expected[1])},
                           {"bytes", f.expected[1].size()}}
                          .dump();
    write(f.journal, f.journal_bytes);
    CancellationToken token;
    Pipe entered, resume;
    st::Plan plan{f.root, f.id.value, "chunk-committed", st::Action::pause};
    plan.notify = entered.fd[1];
    plan.release = resume.fd[0];
    st::Arm held(plan);
    std::future<artifact::ArtifactDescriptor> recovering;
    ReleaseIO cleanup{resume.fd[1]};
    recovering = std::async(std::launch::async, [&] { return store.recover(f.id, token); });
    const auto event = st::await_event(entered.fd[0], std::chrono::steady_clock::now() + 5s);
    CHECK(std::string_view(event.point) == "chunk-committed");
    token.cancel(); // SQLite already committed: cancellation preserves that exact tail.
    cleanup.open();
    CHECK(recovering.wait_for(5s) == std::future_status::ready);
    rejects([&] { recovering.get(); }, Status::cancelled);
    CHECK(held.metrics().hits == 1);
    CHECK(read(f.journal) == f.journal_bytes && !std::filesystem::exists(f.invalid));
    CHECK(read(f.prefix) == f.expected[0] && read(f.tail) == f.expected[1]);
    CHECK(store.get(f.id).state == artifact::ArtifactState::recoverable && store.get(f.id).chunks == 2);
    st::Arm no_second_commit({f.root, f.id.value, "chunk-commit-before", st::Action::reject_commit});
    store.recover(f.id);
    CHECK(no_second_commit.metrics().hits == 0);
    f.exact(store);
}
void responsive_replay() {
    using namespace std::chrono_literals;
    for (const std::string scenario : {"cancel", "finalize-cancel", "io-error", "metadata-conflict"}) {
        Fixture f(false);
        artifact::Store store(f.root);
        const auto other = store.begin({"org.mantis.RawCapture", 1}, {});
        const auto other_packet = packet(7);
        const auto other_bytes = encode(*other_packet);
        write(f.tail, f.expected[1]);
        f.journal_bytes = Json{{"id", f.id.value},
                               {"index", 1},
                               {"path", "objects/" + f.id.value + "/1.packet"},
                               {"hash", digest(f.expected[1])},
                               {"bytes", f.expected[1].size()}}
                              .dump();
        write(f.journal, f.journal_bytes);
        CancellationToken token;
        Pipe entered, resume;
        const std::string phase = scenario == "io-error" ? "journal-sync-before" : "journal-validate-before";
        st::Plan plan{f.root, f.id.value, phase, st::Action::pause};
        plan.notify = entered.fd[1];
        plan.release = resume.fd[0];
        st::Arm held(plan);
        std::optional<st::Arm> failure;
        if (scenario == "io-error")
            failure.emplace(
                st::Plan{f.root, f.id.value, "directory-sync", st::Action::syscall_error, 0, 0, EIO});
        std::future<artifact::ArtifactDescriptor> recovering, snapshot;
        // Release before either future joins on every assertion/exception path.
        ReleaseIO cleanup{resume.fd[1]};
        recovering = std::async(std::launch::async, [&] {
            return scenario == "finalize-cancel" ? store.finalize(f.id, token) : store.recover(f.id, token);
        });
        const auto event = st::await_event(entered.fd[0], std::chrono::steady_clock::now() + 5s);
        CHECK(std::string_view(event.point) == phase && std::string_view(event.id) == f.id.value);
        snapshot = std::async(std::launch::async, [&] {
            const auto value = store.get(f.id);
            CHECK(store.list().size() == 2);
            CHECK(store.get(other).state == artifact::ArtifactState::open);
            store.append(other, *other_packet);
            CHECK(store.finalize(other).state == artifact::ArtifactState::finalized);
            return value;
        });
        CHECK(snapshot.wait_for(2s) == std::future_status::ready); // Original outer lock fails here.
        const auto visible = snapshot.get();
        CHECK(visible.state == artifact::ArtifactState::recoverable && visible.chunks == 1 &&
              visible.bytes == f.expected[0].size());
        CHECK(recovering.wait_for(0s) == std::future_status::timeout);
        for (auto operation : {0, 1, 2, 3, 4, 5})
            rejects(
                [&] {
                    switch (operation) {
                    case 0:
                        (void)store.recover(f.id);
                        break;
                    case 1:
                        (void)store.finalize(f.id);
                        break;
                    case 2:
                        store.abandon(f.id);
                        break;
                    case 3:
                        store.append(f.id, *packet(2));
                        break;
                    case 4:
                        store.prepare_finalize(f.id);
                        break;
                    case 5:
                        store.initialize_provenance(f.id, {});
                        break;
                    }
                },
                Status::busy);
        if (scenario == "cancel" || scenario == "finalize-cancel")
            token.cancel();
        if (scenario == "metadata-conflict") {
            // Fault a real independent SQLite transaction while file I/O is
            // held. Publication must reject this changed snapshot, not repair it.
            Database db(f.root);
            db.exec("BEGIN IMMEDIATE; UPDATE artifacts SET bytes=bytes+1 WHERE id='" + f.id.value +
                    "'; COMMIT");
        }
        cleanup.open();
        CHECK(recovering.wait_for(5s) == std::future_status::ready);
        rejects([&] { recovering.get(); }, (scenario == "cancel" || scenario == "finalize-cancel")
                                               ? Status::cancelled
                                           : scenario == "io-error" ? Status::io
                                                                    : Status::corrupt);
        CHECK(held.metrics().hits == 1);
        if (failure)
            CHECK(failure->metrics().hits == 1 && failure->metrics().error == EIO);
        failure.reset();
        CHECK(read(f.journal) == f.journal_bytes && !std::filesystem::exists(f.invalid));
        CHECK(read(f.prefix) == f.expected[0] && read(f.tail) == f.expected[1]);
        CHECK(store.get(f.id).state == artifact::ArtifactState::recoverable && store.get(f.id).chunks == 1);
        if (scenario == "metadata-conflict") {
            Database db(f.root);
            CHECK(store.get(f.id).bytes == f.expected[0].size() + 1);
            db.exec("BEGIN IMMEDIATE; UPDATE artifacts SET bytes=" + std::to_string(f.expected[0].size()) +
                    " WHERE id='" + f.id.value + "'; COMMIT");
        }
        const auto recovered = store.recover(f.id); // Proves reservation retirement and fresh ownership.
        CHECK(recovered.state == artifact::ArtifactState::finalized && recovered.chunks == 2 &&
              recovered.bytes == f.expected[0].size() + f.expected[1].size() &&
              recovered.hash.hex == digest(digest(f.expected[0]) + digest(f.expected[1])));
        for (unsigned pass = 0; pass < 2; ++pass) {
            size_t n{};
            store.replay(f.id, [&](data::Published p) { CHECK(n < 2 && encode(*p) == f.expected[n++]); });
            CHECK(n == 2);
        }
        const auto untouched = store.get(other);
        CHECK(untouched.state == artifact::ArtifactState::finalized && untouched.chunks == 1 &&
              untouched.bytes == other_bytes.size() && untouched.hash.hex == digest(digest(other_bytes)));
        CHECK(encode(*store.packet(other)) == other_bytes);
        Database db(f.root);
        CHECK(db.rows("SELECT COUNT(*) FROM chunks")[0][0] == "3");
        CHECK(store.list().size() == 2);
    }
}
} // namespace
int main(int argc, char **argv) {
    try {
        const std::string mode = argc == 2 ? argv[1] : "all";
        if (mode == "all" || mode == "rollback")
            rollback();
        if (mode == "all" || mode == "responsive")
            responsive_replay();
        if (mode == "all")
            committed_cancellation();
        if (mode == "all") {
            same_store();
            committed_before_cleanup();
            operational();
            interrupted_publication();
            corrupt_evidence();
            stale_and_conflict();
        }
        CHECK(mode == "all" || mode == "rollback" || mode == "responsive");
        std::cout << "Schema-1 journal transaction rollback, durable-prefix retry, corruption/conflict "
                     "rejection and exact replays PASS\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
