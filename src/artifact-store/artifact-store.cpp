#include <fstream>
#include "segments.hpp"
#include <mantis/artifact_store.hpp>
#include <mantis/data_io.hpp>
#include <mantis/platform.hpp>
#include <mutex>
#include <nlohmann/json.hpp>
#include <sqlite3.h>
namespace mantis::artifact {
namespace {
using Json = nlohmann::json;
struct Statement {
    sqlite3_stmt *s{};
    explicit Statement(sqlite3 *db, const char *sql) {
        if (sqlite3_prepare_v2(db, sql, -1, &s, nullptr) != SQLITE_OK)
            fail(Status::io, sqlite3_errmsg(db), "store");
    }
    ~Statement() {
        sqlite3_finalize(s);
    }
    void text(int n, const std::string &value) {
        if (sqlite3_bind_text(s, n, value.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK)
            fail(Status::io, "SQLite bind failed");
    }
    void integer(int n, uint64_t v) {
        sqlite3_bind_int64(s, n, static_cast<sqlite3_int64>(v));
    }
    bool row() {
        int rc = sqlite3_step(s);
        if (rc == SQLITE_ROW)
            return true;
        if (rc == SQLITE_DONE)
            return false;
        fail(Status::io, sqlite3_errmsg(sqlite3_db_handle(s)), "store");
    }
    std::string text(int n) const {
        auto p = sqlite3_column_text(s, n);
        return p ? reinterpret_cast<const char *>(p) : "";
    }
    uint64_t integer(int n) const {
        return static_cast<uint64_t>(sqlite3_column_int64(s, n));
    }
};
void exec(sqlite3 *db, const char *sql) {
    char *error = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &error) != SQLITE_OK) {
        std::string message = error ? error : "SQLite error";
        sqlite3_free(error);
        fail(Status::io, message, "store");
    }
}
struct Transaction {
    sqlite3 *db;
    bool done{};
    explicit Transaction(sqlite3 *d) : db(d) {
        exec(db, "BEGIN IMMEDIATE");
    }
    ~Transaction() {
        if (!done)
            sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
    }
    void commit() {
        exec(db, "COMMIT");
        done = true;
    }
};
std::string provenance_json(const Provenance &p) {
    std::vector<std::string> ids;
    for (auto &id : p.inputs)
        ids.push_back(id.value);
    return Json{{"producer", p.producer},
                {"version", {p.version.major, p.version.minor, p.version.patch}},
                {"inputs", ids},
                {"parameters", p.parameters},
                {"calibration_id", p.calibration.id.value},
                {"calibration_revision", p.calibration.revision}}
        .dump();
}
Provenance provenance_parse(const std::string &s) {
    auto j = Json::parse(s);
    Provenance p;
    p.producer = j.at("producer");
    p.version = {j["version"][0], j["version"][1], j["version"][2]};
    for (auto &id : j.at("inputs"))
        p.inputs.push_back({id});
    p.parameters = j.at("parameters").get<data::Metadata>();
    p.calibration.id = {j.at("calibration_id")};
    p.calibration.revision = j.at("calibration_revision");
    return p;
}
Hash file_hash(const std::filesystem::path &p) {
    auto m = platform::map_read(p);
    return content_hash({m.data, m.size});
}
} // namespace
struct Store::Impl {
    std::filesystem::path root;
    std::unique_ptr<platform::FileLock> lock;
    sqlite3 *db{};
    mutable std::recursive_mutex mutex;
    struct Writer {
        std::ofstream output;
        std::filesystem::path part;
        uint64_t index{}, limit{segments::max_segment_bytes}, records{};
        std::optional<uint64_t> last_sequence;
    };
    std::map<Id, std::unique_ptr<Writer>> writers;
    void seal(const Id &id, Writer &writer) {
        if (!writer.output.is_open()) return;
        writer.output.flush();
        if (!writer.output) fail(Status::io, "RawCapture segment flush failed");
        writer.output.close();
        platform::durable_file(writer.part);
        auto file = writer.part; file.replace_extension();
        auto hash = file_hash(writer.part);
        auto bytes = std::filesystem::file_size(writer.part);
        std::filesystem::rename(writer.part, file);
        platform::durable_directory(file.parent_path());
        commit_chunk(id.value, writer.index, std::filesystem::relative(file, root).generic_string(), hash.hex, bytes);
        ++writer.index; writer.records = 0;
    }
    void append_raw(const Id &id, const data::Packet &packet) {
        auto found = writers.find(id);
        if (found == writers.end()) {
            auto artifact = get(id);
            if (artifact.state != ArtifactState::open) fail(Status::invalid_argument, "Only OPEN captures accept records");
            auto writer = std::make_unique<Writer>(); writer->index = artifact.chunks;
            if (auto limit = artifact.provenance.parameters.find("segment_max_bytes"); limit != artifact.provenance.parameters.end()) {
                writer->limit = std::stoull(limit->second);
                if (writer->limit < 4096 || writer->limit > segments::max_segment_bytes)
                    fail(Status::invalid_argument, "Segment size must be 4 KiB..64 MiB");
            }
            found = writers.emplace(id, std::move(writer)).first;
        }
        auto &writer = *found->second;
        if (packet.type != schema::frameset) fail(Status::incompatible, "RawCapture v2 requires FrameSet");
        if (writer.last_sequence && packet.header.sequence.value != *writer.last_sequence + 1)
            fail(Status::corrupt, "RawCapture FrameSet sequence discontinuity");
        if (!writer.output.is_open()) {
            writer.part = root / "objects" / id.value / (std::to_string(writer.index) + ".segment.part");
            if (std::filesystem::exists(writer.part)) fail(Status::busy, "Existing provisional segment requires recovery");
            writer.output.open(writer.part, std::ios::binary | std::ios::trunc);
            if (!writer.output) fail(Status::io, "Cannot create RawCapture segment");
            platform::durable_directory(writer.part.parent_path());
        }
        segments::append(writer.output, packet);
        writer.last_sequence = packet.header.sequence.value; ++writer.records;
        if (static_cast<uint64_t>(writer.output.tellp()) >= writer.limit || writer.records >= 100000) seal(id, writer);
    }
    void recover_segments(const Id &id) {
        auto a = get(id);
        // Indexed immutable segments must match; never overwrite an integrity failure.
        Statement indexed(db, "SELECT path,hash FROM chunks WHERE artifact=? ORDER BY idx");
        indexed.text(1, id.value);
        while (indexed.row())
            if (file_hash(root / indexed.text(0)).hex != indexed.text(1)) fail(Status::corrupt, "Indexed segment integrity mismatch");
        const auto directory = root / "objects" / id.value;
        uint64_t index = a.chunks;
        for (;;) {
            auto file = directory / (std::to_string(index) + ".segment");
            auto part = file; part += ".part";
            bool partial = false;
            if (!std::filesystem::exists(file)) {
                if (!std::filesystem::exists(part)) break;
                partial = true; file = part;
            } else if (std::filesystem::exists(part)) fail(Status::corrupt, "Conflicting segment and provisional tail");
            auto scanned = segments::scan(file);
            if (!scanned.corruption.empty()) fail(Status::corrupt, scanned.corruption + "; verified prefix remains on disk");
            if (scanned.incomplete && !partial) fail(Status::corrupt, "Closed segment is incomplete");
            if (partial) {
                if (std::filesystem::exists(directory / (std::to_string(index + 1) + ".segment")) ||
                    std::filesystem::exists(directory / (std::to_string(index + 1) + ".segment.part")))
                    fail(Status::corrupt, "Incomplete segment is not the trailing segment");
                if (scanned.records.empty()) {
                    std::filesystem::resize_file(file, 0);
                    platform::durable_file(file);
                    break;
                }
                if (scanned.incomplete) std::filesystem::resize_file(file, scanned.complete_bytes);
                platform::durable_file(file);
                auto closed = file; closed.replace_extension();
                std::filesystem::rename(file, closed); file = closed;
                platform::durable_directory(directory);
            }
            commit_chunk(id.value, index, std::filesystem::relative(file, root).generic_string(), file_hash(file).hex,
                         std::filesystem::file_size(file));
            ++index;
        }
        for (const auto &entry : std::filesystem::directory_iterator(directory)) {
            auto name = entry.path().filename().string();
            if (name.ends_with(".segment") || name.ends_with(".segment.part")) {
                auto number = std::stoull(name);
                if (number > index) fail(Status::corrupt, "Missing segment / index mismatch");
            }
        }
    }
    explicit Impl(std::filesystem::path path) : root(std::filesystem::absolute(path)) {
        std::filesystem::create_directories(root);
        lock = std::make_unique<platform::FileLock>(root / "runtime.lock");
        for (const auto *dir : {"objects", "capture", "cache", "journal"})
            std::filesystem::create_directories(root / dir);
        auto manifest = root / "manifest.json";
        if (std::filesystem::exists(manifest)) {
            std::ifstream in(manifest);
            auto j = Json::parse(in);
            if (j.at("format") != "org.mantis.project" || j.at("version") != 1)
                fail(Status::incompatible, "Unsupported project version");
        } else {
            std::ofstream out(root / "manifest.part");
            out << Json{{"format", "org.mantis.project"}, {"version", 1}, {"id", Id::random().value}}.dump(2);
            out.close();
            platform::durable_file(root / "manifest.part");
            std::filesystem::rename(root / "manifest.part", manifest);
            platform::durable_directory(root);
        }
        if (sqlite3_open((root / "project.sqlite").string().c_str(), &db) != SQLITE_OK) {
            std::string e = db ? sqlite3_errmsg(db) : "SQLite open failed";
            if (db)
                sqlite3_close(db);
            db = nullptr;
            fail(Status::io, e);
        }
        try {
            sqlite3_busy_timeout(db, 5000);
            {
                Statement schema_version(db, "PRAGMA user_version");
                schema_version.row();
                if (schema_version.integer(0) > 1)
                    fail(Status::incompatible, "Unsupported project metadata schema");
            }
            exec(db, "PRAGMA journal_mode=WAL;PRAGMA synchronous=FULL;PRAGMA foreign_keys=ON;");
            exec(db, "CREATE TABLE IF NOT EXISTS artifacts(id TEXT PRIMARY KEY,type TEXT NOT NULL,version "
                     "INTEGER NOT NULL,state INTEGER NOT NULL,provenance TEXT NOT NULL,hash TEXT NOT NULL "
                     "DEFAULT '',bytes INTEGER NOT NULL DEFAULT 0,chunks INTEGER NOT NULL DEFAULT 0);CREATE "
                     "TABLE IF NOT EXISTS chunks(artifact TEXT NOT NULL REFERENCES artifacts(id),idx INTEGER "
                     "NOT NULL,path TEXT NOT NULL,hash TEXT NOT NULL,bytes INTEGER NOT NULL,PRIMARY "
                     "KEY(artifact,idx));PRAGMA user_version=1;");
            replay_journal();
            exec(db, "UPDATE artifacts SET state=3 WHERE state IN (0,1)");
        } catch (...) {
            sqlite3_close(db);
            db = nullptr;
            throw;
        }
    }
    ~Impl() {
        if (db)
            sqlite3_close(db);
    }
    ArtifactDescriptor get(const Id &id) const {
        Statement s(db,
                    "SELECT id,type,version,state,provenance,hash,bytes,chunks FROM artifacts WHERE id=?");
        s.text(1, id.value);
        if (!s.row())
            fail(Status::not_found, "Artifact not found");
        return {{s.text(0)},
                {s.text(1), static_cast<uint32_t>(s.integer(2))},
                static_cast<ArtifactState>(s.integer(3)),
                provenance_parse(s.text(4)),
                {"fnv1a64", s.text(5)},
                s.integer(6),
                s.integer(7)};
    }
    void commit_chunk(const std::string &id, uint64_t index, const std::string &path, const std::string &hash,
                      uint64_t bytes) {
        Transaction txn(db);
        Statement add(db, "INSERT OR IGNORE INTO chunks(artifact,idx,path,hash,bytes) VALUES(?,?,?,?,?)");
        add.text(1, id);
        add.integer(2, index);
        add.text(3, path);
        add.text(4, hash);
        add.integer(5, bytes);
        add.row();
        Statement update(
            db, "UPDATE artifacts SET chunks=(SELECT COUNT(*) FROM chunks WHERE artifact=?),bytes=(SELECT "
                "COALESCE(SUM(bytes),0) FROM chunks WHERE artifact=?) WHERE id=?");
        update.text(1, id);
        update.text(2, id);
        update.text(3, id);
        update.row();
        txn.commit();
    }
    void replay_journal() {
        for (auto &item : std::filesystem::directory_iterator(root / "journal")) {
            if (item.path().extension() != ".json")
                continue;
            try {
                std::ifstream in(item.path());
                auto j = Json::parse(in);
                auto id = j.at("id").get<std::string>();
                auto artifact = get({id});
                if (artifact.state == ArtifactState::finalized) {
                    std::filesystem::remove(item.path());
                    continue;
                }
                auto rel = j.at("path").get<std::string>();
                auto expected =
                    "objects/" + id + "/" + std::to_string(j.at("index").get<uint64_t>()) + ".packet";
                if (rel != expected)
                    fail(Status::corrupt, "Invalid journal path");
                auto file = root / rel;
                auto part = file;
                part += ".part";
                if (!std::filesystem::exists(file) && std::filesystem::exists(part) &&
                    file_hash(part).hex == j.at("hash").get<std::string>()) {
                    std::filesystem::rename(part, file);
                    platform::durable_directory(file.parent_path());
                }
                if (std::filesystem::exists(file) && file_hash(file).hex == j.at("hash").get<std::string>()) {
                    (void)data::read_packet(file);
                    commit_chunk(id, j.at("index"), rel, j.at("hash"), j.at("bytes"));
                } else if (std::filesystem::exists(part))
                    std::filesystem::remove(part);
                std::filesystem::remove(item.path());
            } catch (const std::exception &) {
                auto bad = item.path();
                bad += ".invalid";
                std::filesystem::rename(item.path(), bad);
            }
        }
    }
};
Store::Store(std::filesystem::path root) : impl_(std::make_unique<Impl>(std::move(root))) {}
Store::~Store() = default;
const std::filesystem::path &Store::root() const {
    return impl_->root;
}
ArtifactId Store::begin(ArtifactType type, Provenance provenance) {
    std::lock_guard guard(impl_->mutex);
    Id id = Id::random();
    std::filesystem::create_directory(impl_->root / "objects" / id.value);
    platform::durable_directory(impl_->root / "objects");
    Statement s(impl_->db, "INSERT INTO artifacts(id,type,version,state,provenance) VALUES(?,?,?,?,?)");
    s.text(1, id.value);
    s.text(2, type.name);
    s.integer(3, type.schema_version);
    s.integer(4, 0);
    s.text(5, provenance_json(provenance));
    s.row();
    return id;
}
void Store::append(const Id &id, const data::Packet &packet) {
    std::lock_guard guard(impl_->mutex);
    if (impl_->writers.contains(id)) { impl_->append_raw(id, packet); return; }
    auto a = impl_->get(id);
    if (a.state != ArtifactState::open)
        fail(Status::invalid_argument, "Only OPEN artifacts accept chunks");
    if (a.type.name == "org.mantis.RawCapture" && a.type.schema_version == 2) {
        impl_->append_raw(id, packet); return;
    }
    auto index = a.chunks;
    auto rel = "objects/" + id.value + "/" + std::to_string(index) + ".packet";
    auto file = impl_->root / rel;
    auto part = file;
    part += ".part";
    data::write_packet(part, packet);
    platform::durable_file(part);
    auto hash = file_hash(part);
    auto bytes = std::filesystem::file_size(part);
    auto journal = impl_->root / "journal" / (id.value + "_" + std::to_string(index) + ".json");
    auto journal_part = journal;
    journal_part += ".part";
    {
        std::ofstream out(journal_part);
        out << Json{{"id", id.value}, {"index", index}, {"path", rel}, {"hash", hash.hex}, {"bytes", bytes}}
                   .dump();
        out.flush();
        if (!out)
            fail(Status::io, "Journal write failed");
    }
    platform::durable_file(journal_part);
    std::filesystem::rename(journal_part, journal);
    platform::durable_directory(journal.parent_path());
    std::filesystem::rename(part, file);
    platform::durable_directory(file.parent_path());
    impl_->commit_chunk(id.value, index, rel, hash.hex, bytes);
    std::filesystem::remove(journal);
    platform::durable_directory(journal.parent_path());
}
ArtifactDescriptor Store::finalize(const Id &id) {
    std::lock_guard guard(impl_->mutex);
    auto a = impl_->get(id);
    if (a.state == ArtifactState::finalized)
        fail(Status::invalid_argument, "Finalized artifacts are immutable");
    if (auto writer = impl_->writers.find(id); writer != impl_->writers.end()) {
        impl_->seal(id, *writer->second); impl_->writers.erase(writer); a = impl_->get(id);
    }
    if (a.type.name == "org.mantis.RawCapture" && a.type.schema_version == 2) {
        // Structural and sequence checks supplement segment hashes at finalization.
        std::optional<uint64_t> sequence;
        for (uint64_t i = 0; i < a.chunks; ++i) {
            auto scanned = segments::scan(object_path(id, i));
            if (scanned.incomplete || !scanned.corruption.empty() || scanned.records.empty())
                fail(Status::corrupt, "Cannot finalize invalid RawCapture segment");
            for (size_t n = 0; n < scanned.records.size(); ++n) {
                auto packet = segments::packet(scanned, n);
                if (packet->type != schema::frameset || (sequence && packet->header.sequence.value != *sequence + 1))
                    fail(Status::corrupt, "Invalid RawCapture FrameSet order");
                sequence = packet->header.sequence.value;
            }
        }
    }
    if (!a.chunks)
        fail(Status::invalid_argument, "Cannot finalize an artifact with no committed chunks");
    {
        Statement s(impl_->db, "UPDATE artifacts SET state=1 WHERE id=?");
        s.text(1, id.value);
        s.row();
    }
    Statement chunks(impl_->db, "SELECT idx,path,hash FROM chunks WHERE artifact=? ORDER BY idx");
    chunks.text(1, id.value);
    std::string hashes;
    uint64_t expected = 0;
    while (chunks.row()) {
        if (chunks.integer(0) != expected++)
            fail(Status::corrupt, "Non-contiguous capture chunks");
        auto file = impl_->root / chunks.text(1);
        if (file_hash(file).hex != chunks.text(2))
            fail(Status::corrupt, "Artifact checksum mismatch");
        hashes += chunks.text(2);
    }
    auto hash = content_hash(std::as_bytes(std::span(hashes)));
    Statement s(impl_->db, "UPDATE artifacts SET state=2,hash=? WHERE id=?");
    s.text(1, hash.hex);
    s.text(2, id.value);
    s.row();
    return impl_->get(id);
}
ArtifactDescriptor Store::get(const Id &id) const {
    std::lock_guard guard(impl_->mutex);
    auto result = impl_->get(id);
    if (auto writer = impl_->writers.find(id); writer != impl_->writers.end() && writer->second->output.is_open()) {
        auto position = writer->second->output.tellp();
        if (position >= 0) result.bytes += static_cast<uint64_t>(position);
    }
    return result;
}
std::vector<ArtifactDescriptor> Store::list() const {
    std::lock_guard guard(impl_->mutex);
    Statement s(impl_->db, "SELECT id FROM artifacts ORDER BY rowid");
    std::vector<ArtifactDescriptor> out;
    while (s.row())
        out.push_back(impl_->get({s.text(0)}));
    return out;
}
std::filesystem::path Store::object_path(const Id &id, uint64_t chunk) const {
    std::lock_guard guard(impl_->mutex);
    Statement s(impl_->db, "SELECT path,hash FROM chunks WHERE artifact=? AND idx=?");
    s.text(1, id.value);
    s.integer(2, chunk);
    if (!s.row())
        fail(Status::not_found, "Artifact chunk not found");
    auto path = impl_->root / s.text(0);
    if (file_hash(path).hex != s.text(1))
        fail(Status::corrupt, "Object integrity check failed");
    return path;
}
data::Published Store::packet(const Id &id, uint64_t chunk) const {
    auto artifact = get(id);
    if (artifact.type.name == "org.mantis.RawCapture" && artifact.type.schema_version == 2) {
        for (uint64_t i = 0; i < artifact.chunks; ++i) {
            auto scanned = segments::scan(object_path(id, i));
            if (!scanned.corruption.empty() || scanned.incomplete) fail(Status::corrupt, "Invalid RawCapture segment");
            if (chunk < scanned.records.size()) return segments::packet(scanned, static_cast<size_t>(chunk));
            chunk -= scanned.records.size();
        }
        fail(Status::not_found, "RawCapture record not found");
    }
    return data::read_packet(object_path(id, chunk));
}
ArtifactDescriptor Store::recover(const Id &id) {
    std::lock_guard guard(impl_->mutex);
    const auto a = impl_->get(id);
    if (a.state != ArtifactState::recoverable) fail(Status::invalid_argument, "Artifact is not recoverable");
    if (a.type.name == "org.mantis.RawCapture" && a.type.schema_version == 2) impl_->recover_segments(id);
    return finalize(id);
}
void Store::abandon(const Id &id) {
    std::lock_guard guard(impl_->mutex);
    if (impl_->get(id).state == ArtifactState::finalized) fail(Status::invalid_argument, "Finalized captures are immutable");
    impl_->writers.erase(id);
    Statement update(impl_->db, "UPDATE artifacts SET state=3 WHERE id=?"); update.text(1, id.value); update.row();
}
void Store::replay(const Id &id, const std::function<void(data::Published)> &emit, const CancellationToken &token) const {
    auto a = get(id);
    if (a.state != ArtifactState::finalized || a.type.name != "org.mantis.RawCapture")
        fail(Status::incompatible, "Replay requires finalized RawCapture");
    std::optional<uint64_t> sequence;
    for (uint64_t i = 0; i < a.chunks; ++i) {
        token.check();
        auto path = object_path(id, i);
        if (a.type.schema_version == 1) { emit(data::read_packet(path)); continue; }
        if (a.type.schema_version != 2) fail(Status::incompatible, "Unsupported RawCapture schema");
        auto scanned = segments::scan(path);
        if (scanned.incomplete || !scanned.corruption.empty()) fail(Status::corrupt, "RawCapture integrity failure");
        for (size_t n = 0; n < scanned.records.size(); ++n) {
            token.check(); auto packet = segments::packet(scanned, n);
            if (sequence && packet->header.sequence.value != *sequence + 1) fail(Status::corrupt, "Replay sequence discontinuity");
            sequence = packet->header.sequence.value; emit(std::move(packet));
        }
    }
}
uint64_t Store::record_count(const Id &id) const {
    uint64_t count{};
    replay(id, [&](data::Published) { ++count; }); return count;
}
struct CaptureReader::Impl {
    std::shared_ptr<const Store> store;
    ArtifactDescriptor artifact;
    uint64_t segment{};
    size_t record{};
    std::optional<segments::Scan> scan;
    Impl(std::shared_ptr<const Store> s, Id id) : store(std::move(s)), artifact(store->get(id)) {
        if (artifact.state != ArtifactState::finalized || artifact.type.name != "org.mantis.RawCapture" ||
            (artifact.type.schema_version != 1 && artifact.type.schema_version != 2))
            fail(Status::incompatible, "CaptureReader requires finalized supported RawCapture");
    }
};
CaptureReader::CaptureReader(std::shared_ptr<const Store> s, Id id) : impl_(std::make_unique<Impl>(std::move(s), std::move(id))) {}
CaptureReader::~CaptureReader() = default;
data::Published CaptureReader::next() {
    auto &r = *impl_;
    if (r.artifact.type.schema_version == 1) {
        if (r.segment == r.artifact.chunks) return {};
        return r.store->packet(r.artifact.id, r.segment++);
    }
    while (!r.scan || r.record == r.scan->records.size()) {
        if (r.segment == r.artifact.chunks) return {};
        r.scan = segments::scan(r.store->object_path(r.artifact.id, r.segment++)); r.record = 0;
        if (r.scan->incomplete || !r.scan->corruption.empty()) fail(Status::corrupt, "Replay segment integrity failure");
    }
    return segments::packet(*r.scan, r.record++);
}
} // namespace mantis::artifact
