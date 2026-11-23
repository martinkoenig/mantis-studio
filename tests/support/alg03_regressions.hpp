#pragma once
#include <sqlite3.h>

// Deliberately bypass typed creators only to construct hash-valid negative
// fixtures. Production load_plane must independently validate these objects.
inline data::ContentReference unchecked_plane(artifact::Store &s, const a3::Plane &p) {
    artifact::Provenance provenance;
    provenance.producer = p.producer.implementation.value;
    provenance.version = p.producer.version;
    provenance.calibration = p.rig.calibration;
    provenance.inputs = a3::detail::inputs(p);
    auto id = s.begin(a3::plane_type, provenance);
    s.append(id, a3::detail::document_packet(get(a3::encode_document(p))));
    auto d = s.finalize(id);
    return {id, {a3::plane_type.name, 1}, d.hash, 1};
}
inline data::ContentReference unchecked_policy(artifact::Store &s, const a3::Policy &p) {
    artifact::Provenance provenance;
    provenance.producer = p.producer.implementation.value;
    provenance.version = p.producer.version;
    provenance.inputs = a3::detail::inputs(p);
    auto id = s.begin(a3::policy_type, provenance);
    s.append(id, a3::detail::document_packet(get(a3::encode_document(p))));
    auto d = s.finalize(id);
    return {id, {a3::policy_type.name, 1}, d.hash, 1};
}
inline void plane_dependencies(artifact::Store &s, const Graph &g) {
    auto id = s.begin({"org.mantis.TestEvidence", 1}, {});
    s.append(id, a3::detail::document_packet("{}"));
    auto d = s.finalize(id);
    data::ContentReference evidence{id, {d.type.name, 1}, d.hash, 1};
    for (bool configuration : {false, true}) {
        auto p = plane(g);
        if (configuration)
            p.producer.configuration = evidence;
        else
            p.parameter_bounds = a3::PlaneBounds{0, 0, evidence};
        auto valid = unchecked_plane(s, p);
        CHECK(a3::load_plane(s, valid));
        auto bad = evidence;
        bad.id = {"missing-required-evidence"};
        if (configuration)
            p.producer.configuration = bad;
        else
            p.parameter_bounds->evidence = bad;
        rejects(a3::load_plane(s, unchecked_plane(s, p)), Status::not_found);
        bad = evidence;
        bad.hash = Hash{"fnv1a64", "0000000000000000"};
        if (configuration)
            p.producer.configuration = bad;
        else
            p.parameter_bounds->evidence = bad;
        rejects(a3::load_plane(s, unchecked_plane(s, p)), Status::incompatible);
    }
    // A deliberately corrupt descriptor closes a self-cycle without inventing
    // an impossible content-hash fixed point. Packet/hash remain untouched.
    sqlite3 *raw{};
    CHECK(sqlite3_open((s.root() / "project.sqlite").string().c_str(), &raw) == SQLITE_OK);
    std::unique_ptr<sqlite3, decltype(&sqlite3_close)> db(raw, sqlite3_close);
    auto sql = "UPDATE artifacts SET provenance=json_set(provenance,'$.inputs',json('[\"" + id.value +
               "\"]')) WHERE id='" + id.value + "'";
    CHECK(sqlite3_exec(db.get(), sql.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK);
    auto p = plane(g);
    p.producer.configuration = evidence;
    auto cycle = a3::load_plane(s, unchecked_plane(s, p));
    rejects(cycle, Status::corrupt);
    CHECK(cycle.error().message == "Self-referencing processing evidence");
}

inline void decoded_admission(artifact::Store &s, const Graph &g) {
    using namespace a3::detail;
    const std::string shallow = "[" + std::string(80000, ' ') + "0]";
    rejects(result([&] { return geometry_charge(shallow, 1024); }), Status::busy);
    std::string array = "[0";
    for (unsigned i = 0; i < 1000; ++i)
        array += ",0";
    array += ']';
    // Encoded bytes fit. Decoded nodes do not; fail before the DOM parser.
    auto failure = result([&] { return geometry_charge(array, array.size() * 16 + 1024); });
    rejects(failure, Status::busy);
    CHECK(failure.error().message == "Decoded geometry preallocation admission");
    CHECK(geometry_charge(array, a3::max_geometry_bytes) > array.size() * 16);
    auto o = observation(g, 1);
    auto b = bind(s, g, o, plane(g), policy());
    CHECK(a3::prepare(s, b));
    auto low = policy();
    low.max_geometry_bytes = 1024;
    b.policy = unchecked_policy(s, low);
    rejects(a3::prepare(s, b), Status::busy);
    // Policy-specific admission also applies BEFORE standalone plane decoding
    // inside prepare, not only when the later geometry graph is retained.
    const auto canonical = get(a3::encode_document(plane(g)));
    uint64_t metadata_remaining = a3::max_geometry_bytes;
    (void)s.get_bounded(b.plane.id, metadata_remaining);
    low.max_geometry_bytes =
        a3::max_geometry_bytes - metadata_remaining + geometry_charge(canonical, a3::max_geometry_bytes) - 1;
    b.policy = unchecked_policy(s, low);
    auto limited = a3::prepare(s, b);
    rejects(limited, Status::busy);
    CHECK(limited.error().message == "Decoded geometry preallocation admission");
    // Actual standalone loader: a large shallow hash-valid document fits the
    // canonical byte limit but cannot be decoded within the hard geometry cap.
    auto j = Json::parse(canonical);
    j["payload"]["cameras"] = Json::array();
    for (unsigned i = 0; i < 125000; ++i)
        j["payload"]["cameras"].push_back(0);
    auto bytes = j.dump();
    CHECK(bytes.size() < 262144);
    auto p = plane(g);
    artifact::Provenance provenance;
    provenance.producer = p.producer.implementation.value;
    provenance.version = p.producer.version;
    provenance.calibration = p.rig.calibration;
    provenance.inputs = inputs(p);
    auto id = s.begin(a3::plane_type, provenance);
    s.append(id, document_packet(bytes));
    const auto d = s.finalize(id);
    auto oversized = a3::load_plane(s, {id, {a3::plane_type.name, 1}, d.hash, 1});
    rejects(oversized, Status::busy);
    CHECK(oversized.error().message == "Decoded geometry preallocation admission");
}

inline void metadata_admission(artifact::Store &s, const Graph &g) {
    using namespace a3::detail;
    const auto p = plane(g);
    auto make = [&](artifact::ArtifactType type, artifact::Provenance provenance, std::string bytes) {
        auto id = s.begin(type, std::move(provenance));
        s.append(id, document_packet(std::move(bytes)));
        auto d = s.finalize(id);
        return data::ContentReference{id, {type.name, type.schema_version}, d.hash, 1};
    };
    auto check_predecode = [&](auto call, const char *path) {
        uint64_t allocated{};
        auto failed = [&] {
            AllocationCount count;
            auto value = result(call);
            allocated = count.bytes;
            return value;
        }();
        rejects(failed, Status::busy);
        CHECK(failed.error().message == "Descriptor predecode admission");
        // Decoding either the long string or the thousands of map/vector
        // entries would exceed this bound, even before typed/cached copies.
        CHECK(allocated < 16384);
        std::cout << "ALG03 " << path << " refused before metadata decode; C++ allocation bytes=" << allocated
                  << '\n';
    };
    for (bool collection : {false, true}) {
        artifact::Provenance provenance;
        if (collection) {
            for (unsigned i = 0; i < 3000; ++i)
                provenance.parameters.emplace("key" + std::to_string(i), "value");
        } else
            provenance.producer = std::string(65536, 'x');
        auto root = make(a3::plane_type, provenance, get(a3::encode_document(p)));
        uint64_t decoded_bytes{};
        {
            AllocationCount count;
            auto decoded = s.get(root.id); // Positive control for the live allocation observable.
            CHECK(decoded.provenance.producer == provenance.producer);
            CHECK(decoded.provenance.parameters == provenance.parameters);
            decoded_bytes = count.bytes;
        }
        CHECK(decoded_bytes > 16384);
        const uint64_t cap = collection ? 4ull << 20 : 1ull << 20;
        check_predecode([&] { return plane_snapshot(s, root, {}, cap); }, "root snapshot");
        auto evidence = make({"org.mantis.TestEvidence", 1}, provenance, "{}");
        auto outer = p;
        outer.producer.configuration = evidence;
        a3::Policy limits;
        limits.max_geometry_bytes = cap;
        check_predecode([&] { evidence_dependencies(s, outer, limits, {}); }, "required dependency");
    }
    // Exercise the public standalone loader's hard cap too, not only the
    // selected-cap helper. Tiny payload, independently oversized metadata.
    artifact::Provenance large;
    large.producer = std::string(2ull << 20, 'x');
    auto root = make(a3::plane_type, large, get(a3::encode_document(p)));
    check_predecode([&] { return get(a3::load_plane(s, root)); }, "public plane loader");
    uint64_t remaining = a3::max_geometry_bytes;
    auto original = s.get(g.rig.descriptor.id);
    auto bounded = s.get_bounded(original.id, remaining);
    CHECK(remaining < a3::max_geometry_bytes);
    CHECK(bounded.hash == original.hash && bounded.state == original.state &&
          bounded.provenance.inputs == original.provenance.inputs &&
          bounded.provenance.producer == original.provenance.producer);
}

inline void nested_snapshots(artifact::Store &s, const Graph &g) {
    using namespace a3::detail;
    const auto base = plane(g);
    auto make = [&](Json j, artifact::ArtifactType type, artifact::Provenance provenance) {
        auto id = s.begin(type, std::move(provenance));
        s.append(id, document_packet(j.dump()));
        auto d = s.finalize(id);
        return data::ContentReference{id, {type.name, type.schema_version}, d.hash, 1};
    };
    auto provenance = [&](const auto &v) {
        artifact::Provenance d;
        d.producer = v.producer.implementation.value;
        d.version = v.producer.version;
        d.inputs = inputs(v);
        return d;
    };
    for (bool is_plane : {true, false}) {
        auto p = base;
        auto q = policy();
        auto pd = provenance(p);
        pd.calibration = p.rig.calibration;
        auto qd = provenance(q);
        for (unsigned control = 0; control < (is_plane ? 5u : 3u); ++control) {
            auto type = is_plane ? a3::plane_type : a3::policy_type;
            Json j = Json::parse(is_plane ? get(a3::encode_document(p)) : get(a3::encode_document(q)));
            if (control == 1)
                type.schema_version = 2;
            if (control == 2) {
                if (is_plane)
                    j["payload"]["offset_mm"] = 800; // integer != typed f64 canonical 800.0
                else
                    j["payload"]["min_incidence_cosine"] = 1; // valid value, wrong typed canonical form
            }
            if (control == 3)
                j["payload"]["frame"]["name"] = "wrong-but-nonempty-frame";
            if (control == 4)
                j["payload"]["cameras"][0]["camera"]["camera_id"]["value"] = "wrong-physical-camera";
            auto dependency = make(j, type, is_plane ? pd : qd);
            auto direct = result([&] {
                if (is_plane)
                    (void)get(a3::load_plane(s, dependency));
                else
                    (void)get(a3::load_policy(s, dependency));
            });
            auto outer = base;
            outer.producer.configuration = dependency;
            auto nested = a3::load_plane(s, unchecked_plane(s, outer));
            CHECK(bool(direct) == (control == 0));
            CHECK(bool(nested) == bool(direct));
            if (!nested) {
                CHECK(nested.error().code == direct.error().code);
                CHECK(nested.error().message == direct.error().message);
                std::cout << "ALG03 nested snapshot control=" << control << " kind=" << type.name
                          << " refused: " << nested.error().message << '\n';
            }
        }
    }
}

struct ReservationFence final : memory::Fence {
    const artifact::Store &store;
    std::set<Id> before;
    std::function<void(const Id &)> action;
    mutable std::optional<Id> entered;
    ReservationFence(const artifact::Store &s, std::function<void(const Id &)> f)
        : store(s), action(std::move(f)) {
        for (const auto &d : s.list())
            before.insert(d.id);
    }
    Result<void> wait(const CancellationToken &) const override {
        if (entered)
            return {};
        // Same-thread re-entry through Store's existing recursive metadata lock.
        // Pre-reservation validator visits cannot trigger this controlled fault.
        for (const auto &d : store.list())
            if (!before.contains(d.id) && d.type.name == a3::result_type.name) {
                CHECK(!entered);
                CHECK(d.state == artifact::ArtifactState::open && d.chunks == 0);
                entered = d.id;
                action(d.id);
                break;
            }
        return {};
    }
};
inline data::Packet fenced_packet(const data::Packet &packet, const std::shared_ptr<ReservationFence> &f) {
    auto p = packet;
    auto &a = p.attributes.back();
    auto bytes = get(a.buffer.map_read());
    auto storage = std::make_shared<memory::Storage>();
    storage->ready = f;
    storage->owner = std::make_shared<memory::BufferView>(a.buffer);
    storage->host = bytes.data();
    storage->size = bytes.size();
    storage->alignment = a.buffer.alignment();
    storage->retained_extent = a.buffer.backing_size();
    a.buffer = {std::move(storage), 0, bytes.size()};
    return p;
}
inline void filesystem_failures(artifact::Store &s, const a3::Prepared &prepared,
                                const data::Packet &output) {
    for (bool snapshot : {false, true}) {
        const auto before = s.list();
        std::set<Id> ids;
        for (const auto &d : before)
            ids.insert(d.id);
        std::optional<Id> entered;
        struct Obstruction {
            std::filesystem::path directory;
            ~Obstruction() {
                if (!directory.empty()) {
                    std::error_code error;
                    std::filesystem::remove_all(directory, error);
                }
            }
        } obstruction;
        auto obstruct = [&](const Id &id) {
            CHECK(!entered);
            auto d = s.get(id);
            CHECK(d.state == artifact::ArtifactState::open && d.chunks == 0);
            CHECK(s.list().size() == before.size() + 1);
            entered = id;
            obstruction.directory = s.root() / "objects" / id.value / "0.packet";
            std::filesystem::create_directory(obstruction.directory);
            std::ofstream marker(obstruction.directory / "owned-obstruction");
            marker << "Nonempty owned directory obstructs file rename";
            CHECK(bool(marker));
        };
        std::function<void()> allocation_entry = [&] {
            if (entered)
                return;
            for (const auto &d : s.list())
                if (!ids.contains(d.id) && d.type.name == a3::policy_type.name)
                    obstruct(d.id);
        };
        struct AllocationObserver {
            explicit AllocationObserver(std::function<void()> &callback) {
                CHECK(!aligned_allocation_observer);
                aligned_allocation_observer = &callback;
            }
            ~AllocationObserver() {
                aligned_allocation_observer = nullptr;
            }
        };
        auto failed = [&]() -> Result<void> {
            if (snapshot) {
                // The existing test-only BufferBuilder allocator observes the
                // actual new OPEN snapshot before materialization. It does NOT
                // throw: Store's real packet rename raises filesystem_error.
                AllocationObserver observer(allocation_entry);
                auto r = a3::create_policy(s, policy());
                if (!r)
                    return std::unexpected(r.error());
            } else {
                auto fence = std::make_shared<ReservationFence>(s, obstruct);
                auto r = a3::persist_result(s, prepared, fenced_packet(output, fence));
                if (!r)
                    return std::unexpected(r.error());
            }
            return {};
        }();
        rejects(failed, Status::io);
        CHECK(entered);
        const auto id = *entered;
        const auto expected_code = std::make_error_code(std::errc::is_a_directory);
        const std::string expected = std::string("ALG03 filesystem phase=append ") +
                                     (snapshot ? "snapshot" : "result") +
                                     "; category=generic; code=" + std::to_string(expected_code.value());
        CHECK(failed.error().message == expected + "; ALG03 artifact " + id.value + "; observed state OPEN");
        CHECK(failed.error().message.find(s.root().string()) == std::string::npos);
        CHECK(s.list().size() == before.size() + 1);
        CHECK(s.get(id).state == artifact::ArtifactState::open && s.get(id).chunks == 0);
        CHECK(std::filesystem::is_regular_file(s.root() / "journal" / (id.value + "_0.json")));
        CHECK(std::filesystem::is_regular_file(s.root() / "objects" / id.value / "0.packet.part"));
        std::filesystem::remove_all(obstruction.directory);
        obstruction.directory.clear();
        s.abandon(id);
        auto d = s.recover(id);
        CHECK(d.id == id && d.state == artifact::ArtifactState::finalized && d.chunks == 1);
        if (snapshot)
            CHECK(a3::load_policy(s, {id, {a3::policy_type.name, 1}, d.hash, 1}));
        else
            CHECK(encoded(*get(a3::load_result(s, id))) == encoded(output));
        CHECK(s.list().size() == before.size() + 1);
        std::cout << "ALG03 actual post-reservation " << (snapshot ? "snapshot" : "result")
                  << " rename failure/root cause retained; same-ID recovery passed\n";
    }
}

inline void reserved_failures(artifact::Store &s, const a3::Prepared &prepared, const data::Packet &output,
                              const Id &source) {
    for (bool cancel : {false, true}) {
        const auto count = s.list().size();
        CancellationToken token;
        auto fence = std::make_shared<ReservationFence>(s, [&](const Id &) {
            if (cancel)
                token.cancel();
            else
                fail(Status::io, "Controlled actual post-reservation encode failure", "fixture");
        });
        auto failed = a3::persist_result(s, prepared, fenced_packet(output, fence), token);
        rejects(failed, cancel ? Status::cancelled : Status::io);
        CHECK(fence->entered);
        const auto id = *fence->entered;
        CHECK(s.list().size() == count + 1);
        CHECK(failed.error().message.find("ALG03 artifact " + id.value + "; observed state OPEN") !=
              std::string::npos);
        if (!cancel)
            CHECK(failed.error().message.starts_with("Controlled actual post-reservation encode failure"));
        else {
            CHECK(failed.error().message.starts_with("Operation cancelled"));
        }
        auto d = s.get(id);
        CHECK(d.state == artifact::ArtifactState::open);
        CHECK(d.chunks == (cancel ? 1u : 0u));
        CHECK(!a3::load_result(s, id));
        // No implicit retry or second reservation. Explicit supported recovery
        // of a completed append uses this same retained ID and fresh token.
        s.abandon(id);
        if (cancel) {
            CHECK(s.recover(id).state == artifact::ArtifactState::finalized);
            CHECK(encoded(*get(a3::load_result(s, id))) == encoded(output));
        } else {
            CHECK(s.get(id).state == artifact::ArtifactState::recoverable);
            CHECK(!a3::detail::result([&] { return s.recover(id); }));
        }
        CHECK(s.list().size() == count + 1);
    }
    struct Restore {
        std::filesystem::path file, saved;
        bool moved{};
        ~Restore() {
            if (moved) {
                std::error_code e;
                std::filesystem::rename(saved, file, e);
            }
        }
    } restore{s.object_path(source, 0), s.object_path(source, 0).string() + ".saved"};
    const auto count = s.list().size();
    auto fence = std::make_shared<ReservationFence>(s, [&](const Id &) {
        std::filesystem::rename(restore.file, restore.saved);
        restore.moved = true;
    });
    auto failed = a3::persist_result(s, prepared, fenced_packet(output, fence));
    rejects(failed, Status::io);
    CHECK(fence->entered && restore.moved);
    CHECK(failed.error().message.starts_with("Cannot open data object: "));
    const auto id = *fence->entered;
    CHECK(s.list().size() == count + 1);
    CHECK(s.get(id).state == artifact::ArtifactState::finalized);
    CHECK(failed.error().message.find("ALG03 artifact " + id.value + "; observed state FINALIZED") !=
          std::string::npos);
    CHECK(!a3::load_result(s, id));
    std::filesystem::rename(restore.saved, restore.file);
    restore.moved = false;
    CHECK(encoded(*get(a3::load_result(s, id))) == encoded(output));
    CHECK(s.list().size() == count + 1);
}
