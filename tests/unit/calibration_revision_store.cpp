#include "../../src/artifact-store/segments.hpp"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mantis/artifact_store.hpp>
#include <mantis/data_io.hpp>
#include <mantis/platform.hpp>
#include <nlohmann/json.hpp>
#include <set>
#include <sqlite3.h>
#include <sstream>
#include <thread>
using namespace mantis;
using Json = nlohmann::json;
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(#x);                                                                    \
    } while (false)
template <class F> void rejects(F f, Status code) {
    try {
        f();
    } catch (const Failure &e) {
        CHECK(e.error.code == code);
        return;
    }
    throw std::runtime_error("Expected rejection");
}
struct Temp {
    std::filesystem::path root = std::filesystem::temp_directory_path() / Id::random().value;
    ~Temp() {
        std::filesystem::remove_all(root);
    }
};
struct Db {
    sqlite3 *db{};
    explicit Db(const std::filesystem::path &root) {
        CHECK(sqlite3_open((root / "project.sqlite").c_str(), &db) == SQLITE_OK);
    }
    ~Db() {
        sqlite3_close(db);
    }
    void exec(const std::string &sql) {
        char *e{};
        auto rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &e);
        std::string error = e ? e : "";
        sqlite3_free(e);
        if (rc != SQLITE_OK)
            throw std::runtime_error(error);
    }
    Json rows(const std::string &sql) {
        sqlite3_stmt *s{};
        CHECK(sqlite3_prepare_v2(db, sql.c_str(), -1, &s, nullptr) == SQLITE_OK);
        Json out = Json::array();
        while (sqlite3_step(s) == SQLITE_ROW) {
            Json row = Json::array();
            for (int i = 0; i < sqlite3_column_count(s); ++i)
                row.push_back(reinterpret_cast<const char *>(sqlite3_column_text(s, i)));
            out.push_back(row);
        }
        sqlite3_finalize(s);
        return out;
    }
};
std::string bytes(const std::filesystem::path &p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), {}};
}
data::Published packet(uint64_t n = 0) {
    data::Packet p;
    p.type = schema::image;
    p.header.sequence.value = n;
    memory::BufferBuilder b(64);
    std::fill(b.writable().begin(), b.writable().end(), std::byte{42});
    p.attributes = {
        {{"org.mantis.pixels", schema::ScalarType::u8, {8, 8}, {8, 1}, "intensity"}, std::move(b).publish()}};
    return data::publish(std::move(p));
}
void revisions() {
    Temp t;
    artifact::Store store(t.root);
    auto first = store.begin_calibration({"org.mantis.RigCalibration", 1}, {});
    CHECK(first.reference.revision == 1);
    CHECK(first.reference.id != first.artifact_id);
    store.append(first.artifact_id, *packet());
    auto a = store.finalize(first.artifact_id);
    store.activate_calibration({"device"}, a.id);
    auto active = store.active_calibration({"device"});
    CHECK(active && active->reference.id == first.reference.id && active->artifact.hash == a.hash);
    auto failed = store.begin_calibration({"org.mantis.RigCalibration", 1}, {}, first.reference.id);
    store.abandon(failed.artifact_id);
    auto third = store.begin_calibration({"org.mantis.RigCalibration", 1}, {}, first.reference.id);
    CHECK(third.reference.revision == 3);
    CHECK(store.calibration_revision(first.reference.id, 2).artifact_id == failed.artifact_id);
    CHECK(store.active_calibration({"device"})->reference.revision == 1);
    rejects([&] { store.begin_calibration({"org.mantis.CameraCalibration", 1}, {}, first.reference.id); },
            Status::incompatible);
    rejects([&] { store.activate_calibration({"device"}, third.artifact_id); }, Status::incompatible);
    rejects([&] { store.begin_calibration({"org.mantis.RigCalibration", 1}, {}, Id{"unknown"}); },
            Status::not_found);
    std::mutex mutex;
    std::set<uint64_t> allocated;
    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i)
        threads.emplace_back([&] {
            auto r = store.begin_calibration({"org.mantis.RigCalibration", 1}, {}, first.reference.id);
            std::lock_guard guard(mutex);
            CHECK(allocated.insert(r.reference.revision).second);
        });
    for (auto &thread : threads)
        thread.join();
    CHECK(allocated == std::set<uint64_t>({4, 5, 6, 7, 8, 9, 10, 11}));
    rejects([&] { store.append(a.id, *packet()); }, Status::invalid_argument);
    rejects([&] { store.finalize(a.id); }, Status::invalid_argument);
    CHECK(store.get(a.id).hash == a.hash);
    CHECK(store.artifact_revision(a.id).reference.revision == 1);
    store.clear_active_calibration({"device"});
    CHECK(!store.active_calibration({"device"}));
    CHECK(store.get(a.id).hash == a.hash);
    Db db(t.root);
    bool rejected = false;
    try {
        db.exec("UPDATE calibration_revisions SET revision=99");
    } catch (...) {
        rejected = true;
    }
    CHECK(rejected);
}
void provenance() {
    Temp t;
    Id id;
    {
        artifact::Store store(t.root);
        artifact::Provenance p;
        p.calibration = {{"logical"}, 7, 3};
        id = store.begin({"fixture", 1}, p);
        store.append(id, *packet());
        store.finalize(id);
        CHECK(store.get(id).provenance.calibration.schema_version == 7);
    }
    {
        Db db(t.root);
        auto j = Json::parse(db.rows("SELECT provenance FROM artifacts")[0][0].get<std::string>());
        CHECK(j.at("calibration_schema_version") == 7);
        j.erase("calibration_schema_version");
        sqlite3_stmt *s{};
        CHECK(sqlite3_prepare_v2(db.db, "UPDATE artifacts SET provenance=?", -1, &s, nullptr) == SQLITE_OK);
        auto value = j.dump();
        sqlite3_bind_text(s, 1, value.c_str(), -1, SQLITE_TRANSIENT);
        CHECK(sqlite3_step(s) == SQLITE_DONE);
        sqlite3_finalize(s);
    }
    {
        artifact::Store store(t.root);
        CHECK(store.get(id).provenance.calibration.schema_version == 1);
        CHECK(store.get(id).provenance.calibration.revision == 3);
    }
}
void migration() {
    Temp t;
    std::filesystem::create_directories(t.root / "objects");
    std::ofstream(t.root / "manifest.json")
        << "{\"format\":\"org.mantis.project\",\"version\":1,\"id\":\"historical-project\"}";
    // Build the actual old schema and old rows without invoking the new Store.
    Db db(t.root);
    db.exec("CREATE TABLE artifacts(id TEXT PRIMARY KEY,type TEXT NOT NULL,version INTEGER NOT NULL,state "
            "INTEGER NOT NULL,provenance TEXT NOT NULL,hash TEXT NOT NULL DEFAULT '',bytes INTEGER NOT NULL "
            "DEFAULT 0,chunks INTEGER NOT NULL DEFAULT 0);CREATE TABLE chunks(artifact TEXT NOT NULL "
            "REFERENCES artifacts(id),idx INTEGER NOT NULL,path TEXT NOT NULL,hash TEXT NOT NULL,bytes "
            "INTEGER NOT NULL,PRIMARY KEY(artifact,idx));PRAGMA user_version=1;");
    Json prov = {{"producer", "old"},
                 {"version", {0, 1, 0}},
                 {"inputs", Json::array()},
                 {"parameters", Json::object()},
                 {"calibration_id", "historical"},
                 {"calibration_revision", 4}};
    for (auto name : {"normal", "raw"}) {
        std::filesystem::create_directory(t.root / "objects" / name);
        db.exec("INSERT INTO artifacts VALUES('" + std::string(name) + "','" +
                (std::string(name) == "raw" ? "org.mantis.RawCapture" : "fixture") + "'," +
                (std::string(name) == "raw" ? "2" : "1") + ",2,'" + prov.dump() +
                "','preserved-final-hash',0,0)");
        for (int i = 0; i < (std::string(name) == "raw" ? 2 : 1); ++i) {
            auto rel = "objects/" + std::string(name) + "/" + std::to_string(i) +
                       (std::string(name) == "raw" ? ".segment" : ".packet");
            if (std::string(name) == "raw") {
                std::ofstream f(t.root / rel, std::ios::binary);
                for (uint64_t j = 0; j < 3; ++j) {
                    data::Packet fs;
                    fs.type = schema::frameset;
                    fs.header.sequence.value = uint64_t(i) * 3 + j;
                    fs.frames = {packet(j)};
                    artifact::segments::append(f, fs);
                }
            } else
                data::write_packet(t.root / rel, *packet());
            auto mapping = platform::map_read(t.root / rel);
            auto hash = content_hash({mapping.data, mapping.size});
            db.exec("INSERT INTO chunks VALUES('" + std::string(name) + "'," + std::to_string(i) + ",'" +
                    rel + "','" + hash.hex + "'," + std::to_string(mapping.size) + ")");
        }
        db.exec("UPDATE artifacts SET bytes=(SELECT SUM(bytes) FROM chunks WHERE artifact='" +
                std::string(name) + "'),chunks=(SELECT COUNT(*) FROM chunks WHERE artifact='" + name +
                "') WHERE id='" + name + "'");
    }
    // Use the real v1 aggregate content-hash convention, not placeholder descriptor hashes.
    for (const auto *name : {"normal", "raw"}) {
        uint64_t aggregate = 14695981039346656037ULL;
        for (const auto &row :
             db.rows("SELECT hash FROM chunks WHERE artifact='" + std::string(name) + "' ORDER BY idx"))
            for (unsigned char c : row[0].get<std::string>()) {
                aggregate ^= c;
                aggregate *= 1099511628211ULL;
            }
        std::ostringstream digest;
        digest << std::hex << std::setfill('0') << std::setw(16) << aggregate;
        db.exec("UPDATE artifacts SET hash='" + digest.str() + "' WHERE id='" + name + "'");
    }
    auto beforeArtifacts = db.rows("SELECT * FROM artifacts ORDER BY id"),
         beforeChunks = db.rows("SELECT * FROM chunks ORDER BY artifact,idx");
    auto manifest = bytes(t.root / "manifest.json");
    std::map<std::filesystem::path, std::string> objects;
    for (auto &e : std::filesystem::recursive_directory_iterator(t.root / "objects"))
        if (e.is_regular_file())
            objects[e.path()] = bytes(e.path());
    {
        artifact::Store store(t.root);
        CHECK(store.record_count({"raw"}) == 6);
        CHECK(store.get({"normal"}).hash.hex == beforeArtifacts[0][5].get<std::string>());
    }
    CHECK(db.rows("PRAGMA user_version")[0][0] == "2");
    CHECK(db.rows("SELECT * FROM artifacts ORDER BY id") == beforeArtifacts);
    CHECK(db.rows("SELECT * FROM chunks ORDER BY artifact,idx") == beforeChunks);
    CHECK(bytes(t.root / "manifest.json") == manifest);
    for (auto &[path, b] : objects)
        CHECK(bytes(path) == b);
    {
        artifact::Store store(t.root);
        CHECK(store.get({"raw"}).provenance.calibration.schema_version == 1);
    }
    db.exec("PRAGMA user_version=3");
    rejects([&] { artifact::Store store(t.root); }, Status::incompatible);
    // Failed DDL rolls back the whole migration, including user_version.
    Temp bad;
    std::filesystem::create_directories(bad.root);
    Db conflict(bad.root);
    conflict.exec("CREATE TABLE active_calibrations(dummy TEXT);PRAGMA user_version=1;");
    rejects([&] { artifact::Store store(bad.root); }, Status::io);
    CHECK(conflict.rows("PRAGMA user_version")[0][0] == "1");
    CHECK(conflict.rows("SELECT name FROM sqlite_master WHERE name='calibration_revisions'").empty());
}
int main(int argc, char **argv) {
    try {
        std::string mode = argc > 1 ? argv[1] : "revision";
        if (mode == "migration")
            migration();
        else if (mode == "provenance")
            provenance();
        else
            revisions();
        std::cout << mode << " passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
