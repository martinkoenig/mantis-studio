#include "../../src/alg03/private.hpp"
#include "../support/alg03_values.hpp"
#include "../support/l1_candidate_values.hpp"
#include <atomic>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <source_location>
#ifdef _MSC_VER
#include <malloc.h>
#endif
using namespace alg03_fixture;
using Json = nlohmann::json;
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(std::string(#x) + " line " + std::to_string(__LINE__));                 \
    } while (false)

// Test-only fault at the existing BufferBuilder allocation boundary; no product
// injection API and no global allocator failure outside the scoped row buffer.
namespace {
std::atomic<size_t> fail_aligned_size{};
thread_local uint64_t *count_allocated_bytes{};
thread_local std::function<void()> *aligned_allocation_observer{};
thread_local std::function<void()> *allocation_observer{};
thread_local bool observing_allocation{};
} // namespace
void *operator new(size_t n) {
    if (allocation_observer && !observing_allocation) {
        struct Guard {
            Guard() {
                observing_allocation = true;
            }
            ~Guard() {
                observing_allocation = false;
            }
        } guard;
        (*allocation_observer)();
    }
    if (count_allocated_bytes)
        *count_allocated_bytes += n;
    if (auto p = std::malloc(n ? n : 1))
        return p;
    throw std::bad_alloc();
}
void operator delete(void *p) noexcept {
    std::free(p);
}
void operator delete(void *p, size_t) noexcept {
    std::free(p);
}
void *operator new[](size_t n) {
    return ::operator new(n);
}
void operator delete[](void *p) noexcept {
    ::operator delete(p);
}
void operator delete[](void *p, size_t) noexcept {
    ::operator delete(p);
}
void *operator new(size_t n, std::align_val_t alignment) {
    if (aligned_allocation_observer)
        (*aligned_allocation_observer)();
    auto expected = n;
    if (fail_aligned_size.compare_exchange_strong(expected, 0))
        throw std::bad_alloc();
    void *p{};
#ifdef _MSC_VER
    p = _aligned_malloc(n, size_t(alignment));
#else
    if (posix_memalign(&p, size_t(alignment), n ? n : 1) != 0)
        p = nullptr;
#endif
    if (!p)
        throw std::bad_alloc();
    return p;
}
void operator delete(void *p, std::align_val_t) noexcept {
#ifdef _MSC_VER
    _aligned_free(p);
#else
    std::free(p);
#endif
}
void operator delete(void *p, size_t, std::align_val_t a) noexcept {
    ::operator delete(p, a);
}
struct AllocationFault {
    explicit AllocationFault(size_t n) {
        fail_aligned_size = n;
    }
    ~AllocationFault() {
        fail_aligned_size = 0;
    }
};
struct AllocationCount {
    uint64_t bytes{};
    AllocationCount() {
        CHECK(!count_allocated_bytes);
        count_allocated_bytes = &bytes;
    }
    ~AllocationCount() {
        count_allocated_bytes = nullptr;
    }
};
Json manifest(const data::Packet &p) {
    auto b = get(p.attributes[0].buffer.map_read());
    return Json::parse(std::string(reinterpret_cast<const char *>(b.data()), b.size())).at("payload");
}
std::string encoded(const data::Packet &p) {
    std::ostringstream out;
    data::write_packet(out, p);
    return out.str();
}
template <class T>
void rejects(const Result<T> &r, Status code,
             const std::source_location &caller = std::source_location::current()) {
    std::cerr << "ALG03 rejects caller=" << caller.file_name() << ':' << caller.line()
              << " function=" << caller.function_name() << " expected_status=" << static_cast<int>(code);
    if (!r)
        std::cerr << " actual_status=" << static_cast<int>(r.error().code)
                  << " component=" << r.error().component.substr(0, 128)
                  << " message=" << r.error().message.substr(0, 1024)
                  << " component_truncated=" << (r.error().component.size() > 128)
                  << " message_truncated=" << (r.error().message.size() > 1024);
    else
        std::cerr << " actual=SUCCESS";
    std::cerr << '\n';
    CHECK(!r);
    CHECK(r.error().code == code);
    CHECK(r.error().component == "alg03");
}
// Exercise the actual serializer/comparison overloads for both supported values.
// In particular, a mutable expected string must not become a writer callable.
void metadata_comparison_regression() {
    namespace detail = mantis::alg03::detail;
    constexpr uint64_t cap = 1ull << 20;
    const CancellationToken token;
    auto expected_pointer_calls = [&](const auto &value) {
        auto expected = detail::metadata(value, cap, token);
        CHECK(!expected.empty());
        const auto original = expected;
        const std::string constant_expected = expected;
        CHECK(detail::metadata(value, cap, token, &expected).empty());
        CHECK(expected == original);
        CHECK(detail::metadata(value, cap, token, &constant_expected).empty());
        CHECK(detail::metadata(value, cap, token, nullptr) == original);
        auto wrong = expected;
        wrong.front() ^= 1;
        auto mismatch = detail::result([&] { return detail::metadata(value, cap, token, &wrong); });
        rejects(mismatch, Status::incompatible);
        CHECK(mismatch.error().message == "Canonical metadata mismatch");
        wrong = expected + "x";
        auto length = detail::result([&] { return detail::metadata(value, cap, token, &wrong); });
        rejects(length, Status::incompatible);
        CHECK(length.error().message == "Canonical metadata length mismatch");
        CHECK(expected == original);
        auto budget = detail::result(
            [&] { return detail::metadata(value, expected.size() - 1, token, &constant_expected); });
        rejects(budget, Status::busy);
        CHECK(budget.error().message == "Canonical metadata budget");
        CancellationToken cancelled;
        cancelled.cancel();
        rejects(detail::result([&] { return detail::metadata(value, cap, cancelled, &expected); }),
                Status::cancelled);
        return expected;
    };
    data::Packet packet;
    packet.type = schema::tensor;
    packet.attributes = {{{"org.mantis.test.metadata", schema::ScalarType::u8, {1}, {1}, ""},
                          memory::copy(std::vector<std::byte>{std::byte{0xa5}})}};
    expected_pointer_calls(packet);
    get(detail::result([&] { detail::equal_packet(packet, packet, cap, token); }));
    auto changed_packet = packet;
    changed_packet.header.sequence.value = 1;
    auto packet_mismatch = detail::result([&] { detail::equal_packet(packet, changed_packet, cap, token); });
    rejects(packet_mismatch, Status::incompatible);
    CHECK(packet_mismatch.error().message == "Canonical metadata mismatch");
    auto observation = observation_fixture::observation();
    auto expected = expected_pointer_calls(observation);
    observation.diagnostic = "changed canonical observation metadata";
    rejects(detail::result([&] { return detail::metadata(observation, cap, token, &expected); }),
            Status::incompatible);
}
struct CancelFence final : memory::Fence {
    mutable bool entered{};
    Result<void> wait(const CancellationToken &token) const override {
        entered = true;
        token.cancel();
        return std::unexpected(Error{Status::cancelled, "Synthetic caller fence cancellation", "fixture"});
    }
};
void cancellation_fence(const a3::Prepared &prepared, const data::LaserObservation &o) {
    auto copy = o;
    auto &attribute = copy.attributes[0];
    auto mapped = get(attribute.buffer.map_read());
    auto fence = std::make_shared<CancelFence>();
    auto storage = std::make_shared<memory::Storage>();
    storage->ready = fence;
    storage->owner = std::make_shared<memory::BufferView>(attribute.buffer);
    storage->host = mapped.data();
    storage->size = mapped.size();
    storage->alignment = attribute.buffer.alignment();
    storage->retained_extent = attribute.buffer.backing_size();
    attribute.buffer = {std::move(storage), 0, mapped.size()};
    CancellationToken token;
    rejects(a3::process(prepared, copy, token), Status::cancelled);
    CHECK(fence->entered);
}
void schema_refusals(artifact::Store &store, const a3::Bindings &binding) {
    const auto original = store.packet(binding.policy.id);
    auto mapped = get(original->attributes[0].buffer.map_read());
    auto json = Json::parse(std::string(reinterpret_cast<const char *>(mapped.data()), mapped.size()));
    auto provenance = store.get(binding.policy.id).provenance;
    auto corrupt = [&](auto edit) {
        auto j = json;
        edit(j);
        auto text = j.dump();
        auto packet = *original;
        packet.attributes[0].descriptor.shape = {text.size()};
        packet.attributes[0].buffer = memory::copy(std::as_bytes(std::span(text)));
        auto id = store.begin(a3::policy_type, provenance);
        store.append(id, packet);
        auto d = store.finalize(id);
        CHECK(!a3::load_policy(store, {id, {a3::policy_type.name, 1}, d.hash, 1}));
    };
    corrupt([](Json &j) { j["payload"]["unknown"] = true; });
    corrupt([](Json &j) { j["schema_version"] = 2; });
    corrupt([](Json &j) { j["payload"]["algorithm"] = "unreviewed-fallback"; });
    corrupt([](Json &j) { j["payload"]["max_samples"] = -1; });
    corrupt([](Json &j) { j["payload"]["max_numeric_error_mm"] = 0; });
}
struct BlockObjects {
    std::filesystem::path object, saved;
    explicit BlockObjects(const artifact::Store &s)
        : object(s.root() / "objects"), saved(s.root() / "objects.saved") {
        std::filesystem::rename(object, saved);
        std::ofstream file(object);
        file << "ordinary fixture obstruction";
        file.close();
    }
    ~BlockObjects() {
        std::error_code error;
        std::filesystem::remove(object, error);
        std::filesystem::rename(saved, object, error);
    }
};
#include "../support/alg03_regressions.hpp"
void ordinary_and_storage(artifact::Store &s, const Graph &g) {
    auto o = observation(g);
    const auto original = observation_fixture::encode(o);
    auto b = bind(s, g, o, plane(g), policy());
    // Observation-only host: neither acquisition RAW, exact historical input nor
    // producer parameters are Store artifacts. Calibration RAW2 metadata IS local.
    for (auto id :
         {o.context.raw_input.get()->id, o.context.exact_inputs[0].id, o.context.parameters.get()->id}) {
        try {
            (void)s.get(id);
            CHECK(false);
        } catch (const Failure &e) {
            CHECK(e.error.code == Status::not_found);
        }
    }
    auto prepared = get(a3::prepare(s, b));
    schema_refusals(s, b);
    cancellation_fence(prepared, o);
    auto output = get(a3::process(prepared, o));
    CHECK(output->attributes.size() == 17);
    auto m = manifest(*output);
    std::cerr.precision(17);
    std::cerr << "ALG03 ordinary accepted_count=" << m.at("accepted_count") << " outcome=" << m.at("outcome")
              << '\n';
    for (size_t i = 0; i < o.sample_count; ++i) {
        std::cerr << "ALG03 ordinary row=" << i
                  << " reason_bits=" << value<uint32_t>(*output, "reason_bits", i)
                  << " diagnostic_valid=" << value<uint32_t>(*output, "diagnostic_valid", i);
        for (size_t j = 0; j < 3; ++j)
            std::cerr << " position=" << value<double>(*output, "position", i, j);
        for (const char *name :
             {"inverse_residual_px", "inverse_error_bound", "ray_denominator", "incidence_cosine",
              "ray_parameter_mm", "optical_depth_mm", "numeric_error_bound_mm", "declared_model_bound_mm",
              "source_quantization_bound_mm"})
            std::cerr << ' ' << name << '=' << value<double>(*output, name, i);
        std::cerr << '\n';
    }
    CHECK(m["accepted_count"] == 2 && m["outcome"] == "all_accepted");
    for (size_t i = 0; i < o.sample_count; ++i) {
        CHECK(value<uint32_t>(*output, "source_row", i) == i);
        CHECK(value<uint8_t>(*output, "accepted", i) == 1);
        auto u = value<float>(*output, "source_pixel", i), v = value<float>(*output, "source_pixel", i, 1);
        auto expected = oracle(g, u, v);
        for (size_t j = 0; j < 3; ++j)
            CHECK(std::abs(value<double>(*output, "position", i, j) - expected[j]) < 2e-8L);
        const auto error = std::hypot(value<double>(*output, "position", i, 0) - expected[0],
                                      value<double>(*output, "position", i, 1) - expected[1],
                                      value<double>(*output, "position", i, 2) - expected[2]);
        CHECK(error <= value<double>(*output, "numeric_error_bound_mm", i));
        CHECK(value<double>(*output, "numeric_error_bound_mm", i) <= policy().max_numeric_error_mm);
        CHECK((value<uint32_t>(*output, "diagnostic_valid", i) & 0x7f) == 0x7f);
        CHECK((value<uint32_t>(*output, "diagnostic_valid", i) & (1u << 7)) ==
              0); // Unknown model bound is not zero.
    }
    uint64_t row_storage = 0;
    for (size_t i = 1; i < 17; ++i) {
        row_storage += output->attributes[i].buffer.size();
        CHECK(output->attributes[i].buffer.identity() == output->attributes[1].buffer.identity());
    }
    CHECK(row_storage == 121 * o.sample_count);
    CHECK(observation_fixture::encode(o) == original);
    get(a3::validate_result(prepared, *output));
    auto stored = get(a3::persist_result(s, prepared, *output));
    CHECK(encoded(*get(a3::load_result(s, stored.id))) == encoded(*output));
    const auto before_reservation = s.list().size();
    {
        // Store::begin creates the object directory before inserting a row or
        // returning an ID. This obstruction is a pre-reservation I/O failure.
        BlockObjects blocked(s);
        auto failed = a3::persist_result(s, prepared, *output);
        rejects(failed, Status::io);
        CHECK(failed.error().message.starts_with("ALG03 filesystem phase=unreserved operation; category="));
        CHECK(failed.error().message.find("; code=") != std::string::npos);
        CHECK(failed.error().message.find(s.root().string()) == std::string::npos);
        CHECK(s.list().size() == before_reservation);
        auto snapshot = a3::create_policy(s, policy());
        rejects(snapshot, Status::io);
        CHECK(snapshot.error().message.starts_with("ALG03 filesystem phase=unreserved operation; category="));
        CHECK(s.list().size() == before_reservation);
    }
    // A real reserved identity must still be retained by the existing wrapper.
    // This synthetic error tests reporting, not a fabricated storage-fault run.
    auto reserved =
        a3::detail::reserved_error(s, stored.id, {Status::io, "Synthetic post-reservation failure", "alg03"});
    CHECK(reserved.code == Status::io);
    CHECK(reserved.component == "alg03");
    CHECK(reserved.message.find("Synthetic post-reservation failure") == 0);
    CHECK(reserved.message.find("ALG03 artifact " + stored.id.value) != std::string::npos);
    CHECK(reserved.message.find("observed state FINALIZED") != std::string::npos);
    CHECK(reserved.message.find("observed state UNKNOWN") == std::string::npos);
    auto malformed = a3::detail::result([] { throw std::runtime_error("Synthetic non-filesystem failure"); });
    rejects(malformed, Status::corrupt);
    CHECK(malformed.error().message == "Malformed ALG03 document/data");
    auto descriptor = s.get(stored.id);
    CHECK(
        (descriptor.provenance.inputs == std::vector<Id>{b.observation.id, b.camera.content.get()->id,
                                                         b.rig.content.get()->id, b.plane.id, b.policy.id}));
    // Typed reader must reject a valid generic packet with forged row/manifest,
    // hash-valid content alone is not a geometry/result validation verdict.
    auto bad = *output;
    auto bytes = get(bad.attributes[5].buffer.map_read());
    auto copy = std::vector<std::byte>(bytes.begin(), bytes.end());
    copy[0] ^= std::byte{1};
    bad.attributes[5].buffer = memory::copy(copy);
    rejects(a3::validate_result(prepared, bad), Status::incompatible);
    auto corrupt = s.begin(a3::result_type, descriptor.provenance);
    s.append(corrupt, bad);
    s.finalize(corrupt);
    CHECK(!a3::load_result(s, corrupt));
    auto wrong_header = *output;
    wrong_header.header.metadata["silent-rebind"] = "forbidden";
    CHECK(!a3::validate_result(prepared, wrong_header));
    auto changed = o;
    changed.diagnostic = "different observation metadata";
    rejects(a3::process(prepared, changed), Status::incompatible);
    changed = o;
    auto px = get(changed.attributes[0].buffer.map_read());
    copy.assign(px.begin(), px.end());
    copy[0] ^= std::byte{1};
    changed.attributes[0].buffer = memory::copy(copy);
    rejects(a3::process(prepared, changed), Status::incompatible);
    // Source-bound node composes through the actual single-result semantic DAG.
    pipeline::NodeDescriptor source;
    source.id = "recorded-observation";
    source.outputs = {{"observation", schema::laser_observation}};
    pipeline::SemanticGraph graph;
    graph.nodes = {{source, {}}, a3::triangulator(prepared)};
    graph.connections = {{0, 0, 1, 0, 1, pipeline::QueuePolicy::lossless}};
    auto plan = get(pipeline::compile(graph));
    auto result = get(pipeline::execute(plan, data::publish(data::SemanticPacket{o})));
    CHECK(encoded(**std::get_if<data::Published>(result.output.get())) == encoded(*output));
    CHECK(result.retained_payload_high_water > row_storage);
    // Prepared owns the exact source/geometry after caller wrappers are retired.
    auto node = a3::triangulator(prepared).factory();
    o = {};
    output.reset();
    auto reread = s.laser_observation(b.observation.id);
    std::array<data::SemanticPublished, 1> input{data::publish(data::SemanticPacket{reread})};
    CHECK(node->process(input, {}));
    {
        AllocationFault fault(reread.sample_count * 121);
        rejects(a3::process(prepared, reread), Status::busy);
        CHECK(fail_aligned_size == 0); // fault reached the intended row allocation.
    }
    CancellationToken cancelled;
    cancelled.cancel();
    rejects(a3::process(prepared, reread, cancelled), Status::cancelled);
    rejects(a3::prepare(s, b, cancelled), Status::cancelled);
    rejects(a3::persist_result(s, prepared, *get(a3::process(prepared, reread)), cancelled),
            Status::cancelled);
    auto budget = policy();
    budget.max_samples = 1;
    auto limited = b;
    limited.policy = get(a3::create_policy(s, budget));
    rejects(a3::prepare(s, limited), Status::busy);
    budget = policy();
    budget.max_result_bytes = 1;
    limited = b;
    limited.policy = get(a3::create_policy(s, budget));
    auto lp = get(a3::prepare(s, limited));
    rejects(a3::process(lp, reread), Status::busy);
    budget = policy();
    budget.max_dependency_nodes = 1;
    limited = b;
    limited.policy = get(a3::create_policy(s, budget));
    rejects(a3::prepare(s, limited), Status::busy);
    // Nine geometry/snapshot artifacts plus the required observation: a cap of
    // nine must fail, not forget the source at the processing-graph boundary.
    budget = policy();
    budget.max_dependency_nodes = 9;
    limited = b;
    limited.policy = get(a3::create_policy(s, budget));
    rejects(a3::prepare(s, limited), Status::busy);
    auto missing = b;
    missing.plane.id = {"missing-processing-plane"};
    CHECK(!a3::prepare(s, missing));
    // Establish a valid baseline; the negative changes only the exact content
    // revision, leaving the logical camera revision, identity/hash and all other
    // processing bindings unchanged.
    get(a3::prepare(s, b));
    const auto original_bindings = a3::detail::bindings(b);
    const auto *camera_content = b.camera.content.get();
    CHECK(camera_content && camera_content->revision == 1);
    auto version = b;
    auto cr = *camera_content;
    cr.revision = 2;
    version.camera.content = cr;
    auto expected_version = original_bindings;
    expected_version["camera"]["content"]["value"]["revision"] = 2;
    CHECK(a3::detail::bindings(version).dump() == expected_version.dump());
    rejects(a3::prepare(s, version), Status::incompatible);
    CHECK(a3::detail::bindings(b).dump() == original_bindings.dump());
    auto hash = b;
    hash.observation.hash = Hash{"fnv1a64", "0000000000000000"};
    rejects(a3::prepare(s, hash), Status::incompatible);
    auto schema = b;
    schema.observation.type.version = 2;
    rejects(a3::prepare(s, schema), Status::incompatible);
    auto bounded = policy();
    bounded.uncertainty_requirement = a3::Uncertainty::require_declared_bound;
    bounded.max_declared_model_error_mm = 1;
    auto required = b;
    required.policy = get(a3::create_policy(s, bounded));
    auto refused = get(a3::process(get(a3::prepare(s, required)), reread));
    CHECK(manifest(*refused)["outcome"] == "all_rejected");
    CHECK(value<uint32_t>(*refused, "reason_bits", 0) == a3::declared_bound_unavailable_or_exceeded);
    auto wrong = plane(g);
    wrong.cameras[0].component = {{"other-component"}};
    required = b;
    required.plane = get(a3::create_plane(s, wrong));
    rejects(a3::prepare(s, required), Status::incompatible);
}
void empty_and_order(artifact::Store &s, const Graph &g) {
    auto p = plane(g);
    auto pol = policy();
    auto zero = observation(g, 0);
    auto b = bind(s, g, zero, p, pol);
    auto prepared = get(a3::prepare(s, b));
    auto out = get(a3::process(prepared, zero));
    CHECK(out->attributes.size() == 1 && manifest(*out)["outcome"] == "empty_input");
    auto r = get(a3::persist_result(s, prepared, *out));
    CHECK(get(a3::load_result(s, r.id))->attributes.size() == 1);
    for (auto disposition : {data::ObservationDisposition::extractor_failed,
                             data::ObservationDisposition::extractor_unavailable}) {
        auto failed = zero;
        failed.disposition = disposition;
        auto refs = b;
        refs.observation = persist(s, failed);
        rejects(a3::prepare(s, refs), disposition == data::ObservationDisposition::extractor_failed
                                          ? Status::plugin_failed
                                          : Status::unsupported);
    }
    auto o = observation(g, 4);
    std::vector<uint32_t> flags{data::laser::producer_rejected | data::laser::ambiguous, 0, 0, 0};
    replace(o, observation_fixture::column(std::string(data::laser::quality_flags), flags,
                                           schema::ScalarType::u32));
    auto coords = get(o.attributes[0].buffer.map_read());
    std::vector<float> pixels(o.sample_count * 2);
    std::memcpy(pixels.data(), coords.data(), coords.size());
    pixels[2] = -1;
    pixels[4] = pixels[6];
    pixels[5] = pixels[7];
    replace(o, {data::laser::source_pixel_descriptor(o.sample_count),
                memory::copy(std::as_bytes(std::span(pixels)))});
    auto mixed = bind(s, g, o, p, pol);
    auto ms = get(a3::prepare(s, mixed));
    out = get(a3::process(ms, o));
    CHECK(manifest(*out)["outcome"] == "mixed");
    CHECK(value<uint32_t>(*out, "reason_bits", 0) == (a3::producer_rejected | a3::ambiguous));
    CHECK(value<uint32_t>(*out, "reason_bits", 1) == a3::image_fov);
    for (size_t i = 0; i < 2; ++i) {
        CHECK(value<uint32_t>(*out, "diagnostic_valid", i) == 0);
        for (size_t j = 0; j < 3; ++j)
            CHECK(value<double>(*out, "position", i, j) == 0 &&
                  !std::signbit(value<double>(*out, "position", i, j)));
    }
    for (size_t i = 0; i < 4; ++i)
        CHECK(value<uint32_t>(*out, "source_row", i) == i); // duplicates are retained, never deduplicated.
    flags.assign(4, data::laser::ambiguous);
    replace(o, observation_fixture::column(std::string(data::laser::quality_flags), flags,
                                           schema::ScalarType::u32));
    mixed.observation = persist(s, o);
    out = get(a3::process(get(a3::prepare(s, mixed)), o));
    CHECK(out->attributes.size() == 17 && manifest(*out)["outcome"] == "all_rejected");
    // Known dictionary mismatch is a row refusal, not a guessed line binding.
    o = observation(g, 1);
    o.line_dictionary[0].local_line = {{"other-line"}};
    mixed.observation = persist(s, o);
    out = get(a3::process(get(a3::prepare(s, mixed)), o));
    CHECK(value<uint32_t>(*out, "reason_bits", 0) == a3::line_mismatch);
    o = observation(g, 1);
    o.context.source.width -= 1;
    mixed.observation = persist(s, o);
    rejects(a3::prepare(s, mixed), Status::incompatible);
    o = observation(g, 1);
    o.context.optical_frame.name = "different convention";
    mixed.observation = persist(s, o);
    rejects(a3::prepare(s, mixed), Status::incompatible);
    auto invalid = p;
    invalid.normal_xyz = {0, -2, 0};
    CHECK(!a3::create_plane(s, invalid));
    invalid = p;
    invalid.cameras[0].camera.camera_id = {"role-is-not-a-physical-id"};
    CHECK(!a3::create_plane(s, invalid));
    invalid = p;
    invalid.line.pattern_revision = 0;
    CHECK(!a3::create_plane(s, invalid));
}
void right_camera(artifact::Store &s, const Graph &g) {
    auto o = observation(g, 1);
    auto &camera = g.right.value.solution.camera;
    o.context.source.camera_role = camera.role;
    o.context.source.camera_calibration = exact(g.right);
    o.context.optical_frame = camera.optical_frame;
    auto pixel =
        forward(g.right.value.solution.final_model, g.rig.value.solution.rig.T_rig_from_right, {15, 800, 8});
    std::vector<float> values{float(pixel[0]), float(pixel[1])};
    replace(o, {data::laser::source_pixel_descriptor(1), memory::copy(std::as_bytes(std::span(values)))});
    auto p = plane(g);
    p.cameras[0].camera = camera;
    p.cameras[0].calibration = exact(g.right);
    auto b = bind(s, g, o, p, policy());
    b.camera = exact(g.right);
    auto out = get(a3::process(get(a3::prepare(s, b)), o));
    CHECK(value<uint8_t>(*out, "accepted", 0) == 1);
    auto expected = oracle(g, values[0], values[1], true);
    for (size_t j = 0; j < 3; ++j)
        CHECK(std::abs(value<double>(*out, "position", 0, j) - expected[j]) < 2e-8L);
}
void replay(artifact::Store &s, const Graph &g) {
    auto o = observation(g, 1), source = o;
    auto b = bind(s, g, o, plane(g), policy());
    auto before = get(a3::process(get(a3::prepare(s, b)), o));
    auto revised = get(ca::create_rig_calibration(
        s, g.rig.value.solution, g.dataset.reference(), g.target.reference(), g.left.reference(),
        g.right.reference(), g.rig.value.implementation, g.rig.value.revision.id));
    auto p = plane(g);
    p.rig = exact(revised);
    auto alternate = b;
    alternate.rig = p.rig;
    alternate.plane = get(a3::create_plane(s, p));
    rejects(a3::prepare(s, alternate), Status::incompatible);
    alternate.calibration_selection = a3::Selection::explicit_processing;
    auto processing = get(a3::prepare(s, alternate));
    auto out = get(a3::process(processing, o));
    CHECK(manifest(*out)["inputs"]["calibration_selection"] == "explicit_processing");
    CHECK(observation_fixture::encode(o) == observation_fixture::encode(source));
    s.activate_calibration({"alg03-test-device"}, revised.descriptor.id);
    s.clear_active_calibration({"alg03-test-device"});
    CHECK(encoded(*get(a3::process(get(a3::prepare(s, b)), o))) == encoded(*before));
    auto ref = get(a3::persist_result(s, processing, *out));
    CHECK(encoded(*get(a3::load_result(s, ref.id))) == encoded(*out));
}
void actual_alg02(artifact::Store &s, const Graph &g) {
    const auto &camera = g.left.value.solution.camera;
    auto bundle = l1_fixture::image_bundle(
        false, camera.image_width, camera.image_height, [](uint32_t, uint32_t y) -> uint16_t {
            return y == 399 ? 155 : y == 400 ? 179 : y == 401 ? 171 : y == 398 ? 99 : y == 402 ? 131 : 12;
        });
    bundle.evidence.frames[0].camera_calibration = exact(g.left);
    bundle.evidence.frames[0].rig_calibration = exact(g.rig);
    l1_fixture::edit_image(bundle, [&](data::Packet &image) {
        image.header.frame = camera.optical_frame;
        image.header.calibration = g.rig.value.revision;
    });
    auto config = l1_fixture::subpixel_config();
    config.optical_frame = camera.optical_frame;
    config.roi = pipeline::L1CandidateConfig::Roi{637, 396, 3, 9};
    auto dark = l1_fixture::image_bundle(
        false, camera.image_width, camera.image_height, [](uint32_t, uint32_t) -> uint16_t { return 0; }, 3,
        true);
    dark.evidence.frames[0].camera_calibration = exact(g.left);
    dark.evidence.frames[0].rig_calibration = exact(g.rig);
    l1_fixture::edit_image(dark, [&](data::Packet &image) {
        image.header.frame = camera.optical_frame;
        image.header.calibration = g.rig.value.revision;
    });
    config.dark = pipeline::L1DarkReference{std::make_shared<const data::AcquisitionBundle>(dark),
                                            data::ContentReference{{"unavailable-historical-dark"},
                                                                   {"org.mantis.RawCapture", 3},
                                                                   Hash{"fnv1a64", "0123456789abcdef"},
                                                                   0}};
    auto node = pipeline::l1_candidate_extractor(config).factory();
    std::array<data::SemanticPublished, 1> inputs{data::publish(data::SemanticPacket{bundle})};
    auto o = std::get<data::LaserObservation>(*get(node->process(inputs, {})));
    CHECK(o.sample_count == 3);
    auto p = plane(g);
    p.line = o.line_dictionary.at(0);
    p.cameras[0].component = o.context.source.frame.camera;
    auto b = bind(s, g, o, p, policy(a3::SourceContract::alg02_subpixel_1));
    auto prepared = get(a3::prepare(s, b));
    auto out = get(a3::process(prepared, o));
    CHECK(manifest(*out)["accepted_count"] == 3);
    CHECK(o.context.exact_inputs.size() >= 2);
    for (auto &historical : o.context.exact_inputs) {
        try {
            (void)s.get(historical.id);
            CHECK(false);
        } catch (const Failure &e) {
            CHECK(e.error.code == Status::not_found);
        }
    }
    // Accepted producer is deliberately opaque: there is no parameter artifact.
    try {
        (void)s.get(o.context.parameters.get()->id);
        CHECK(false);
    } catch (const Failure &e) {
        CHECK(e.error.code == Status::not_found);
    }
    pipeline::NodeDescriptor source;
    source.id = "recorded-bundle";
    source.outputs = {{"bundle", schema::acquisition_bundle}};
    pipeline::SemanticGraph graph;
    graph.nodes = {{source, {}}, pipeline::l1_candidate_extractor(config), a3::triangulator(prepared)};
    graph.connections = {{0, 0, 1, 0, 1, pipeline::QueuePolicy::lossless},
                         {1, 0, 2, 0, 1, pipeline::QueuePolicy::lossless}};
    auto result = get(pipeline::execute(get(pipeline::compile(graph)), inputs[0]));
    CHECK(encoded(**std::get_if<data::Published>(result.output.get())) == encoded(*out));
    auto r = get(a3::persist_result(s, prepared, *out));
    CHECK(a3::load_result(s, r.id));
    auto corrupt = o;
    auto parameters = *corrupt.context.parameters.get();
    parameters.id = {"not-the-opaque-fingerprint"};
    corrupt.context.parameters = parameters;
    b.observation = persist(s, corrupt);
    rejects(a3::prepare(s, b), Status::incompatible);
    corrupt = o;
    parameters = *corrupt.context.parameters.get();
    parameters.type.version = 2;
    corrupt.context.parameters = parameters;
    corrupt.context.producer.configuration = parameters;
    b.observation = persist(s, corrupt);
    rejects(a3::prepare(s, b), Status::incompatible);
}
#include "../support/alg03_recorded.hpp"
#include "../support/alg03_recorded_packed.hpp"
// Exercise the current typed reader on a finalized, generically readable result
// whose only difference is a canonical, same-length prior implementation stamp.
void result_build_identity(artifact::Store &s, const Graph &g, const std::string &expected_digest) {
    CHECK(expected_digest.size() == 64);
    auto source = observation(g, 1);
    auto prepared = get(a3::prepare(s, bind(s, g, source, plane(g), policy())));
    auto current = get(a3::process(prepared, source));
    const std::string prefix = std::string(build_version) + ";alg03-source-sha256:";
    CHECK(manifest(*current).at("algorithm").at("build") == prefix + expected_digest);
    auto ref = get(a3::persist_result(s, prepared, *current));
    const auto current_bytes = encoded(*current);
    CHECK(encoded(*get(a3::load_result(s, ref.id))) == current_bytes);

    auto prior = *current;
    auto mapped = get(prior.attributes.at(0).buffer.map_read());
    const std::string text(reinterpret_cast<const char *>(mapped.data()), mapped.size());
    auto doc = Json::parse(text);
    CHECK(doc.dump() == text);
    // Actual source digest of the preserved pre-recorder implementation. If
    // that source is ever restored exactly, use this integration's stamp instead.
    const std::string archived = "5511fca4db65cb3abce166a70c9c7608ab6cbdda581852931b78b66d7c63f8b1";
    const std::string alternate = expected_digest == archived
                                      ? "62b2ba240c0781e65c38225ce85da1cff0a83ab18ec9e4afd1c62a0262876b94"
                                      : archived;
    doc.at("payload").at("algorithm").at("build") = prefix + alternate;
    const auto changed = doc.dump();
    CHECK(changed.size() == text.size() && changed != text);
    prior.attributes[0].buffer = memory::copy(std::as_bytes(std::span(changed)));
    const auto old_bytes = encoded(prior);
    auto id = s.begin(a3::result_type, s.get(ref.id).provenance);
    s.append(id, prior);
    auto finalized = s.finalize(id);
    CHECK(finalized.hash != ref.hash);
    CHECK(encoded(*s.packet(id)) == old_bytes);
    const auto count = s.list().size();
    auto invalid = a3::validate_result(prepared, prior);
    rejects(invalid, Status::incompatible);
    CHECK(invalid.error().message == "Attribute byte mismatch");
    auto unreadable = a3::load_result(s, id);
    rejects(unreadable, Status::incompatible);
    CHECK(unreadable.error().message == "Attribute byte mismatch");
    CHECK(s.get(id).state == artifact::ArtifactState::finalized && s.get(id).hash == finalized.hash);
    CHECK(encoded(*s.packet(id)) == old_bytes);
    CHECK(encoded(*get(a3::load_result(s, ref.id))) == current_bytes && s.list().size() == count);
    std::cout << "current=" << expected_digest << " prior=" << alternate
              << " current_artifact=" << ref.hash.hex << " prior_artifact=" << finalized.hash.hex << '\n';
}
int main(int argc, char **argv) {
    try {
        const bool artifact_regressions = argc == 2 && std::string_view(argv[1]) == "--artifact-regressions";
        const bool geometry_regressions = argc == 2 && std::string_view(argv[1]) == "--geometry-regressions";
        const bool identity = argc == 3 && std::string_view(argv[1]) == "--result-build-identity";
        const bool recorded = argc == 2 && std::string_view(argv[1]) == "--recorded";
        const bool recorded_packed = argc == 2 && std::string_view(argv[1]) == "--recorded-packed";
        CHECK(argc == 1 || artifact_regressions || geometry_regressions || identity || recorded ||
              recorded_packed);
        if (argc == 1)
            metadata_comparison_regression();
        Temp temp;
        auto store = std::make_unique<artifact::Store>(temp.path, recorded_checkpoint);
        auto geometry = graph(*store);
        if (recorded) {
            std::shared_ptr<artifact::Store> recorded_store(std::move(store));
            recorded_no_line(recorded_store, geometry);
            recorded_metric(recorded_store, geometry);
            std::cout << "Recorded empty and nonempty metric roundtrips and refusal controls passed\n";
            return 0;
        }
        if (recorded_packed) {
            packed_recorded_fixture::run(std::shared_ptr<artifact::Store>(std::move(store)), geometry);
            std::cout << "Recorded packed-pixel golden and revision replay passed\n";
            return 0;
        }
        if (identity) {
            result_build_identity(*store, geometry, argv[2]);
            return 0;
        }
        if (geometry_regressions) {
            // The new metadata/recursive-schema controls get an independent
            // fixture, not a larger deadline. All integration limits stay 60s.
            metadata_admission(*store, geometry);
            nested_snapshots(*store, geometry);
            std::cout << "ALG03 metadata admission and nested geometry regressions passed\n";
            return 0;
        }
        if (artifact_regressions) {
            // Preserve all existing artifact/failure controls and deadline.
            plane_dependencies(*store, geometry);
            decoded_admission(*store, geometry);
            auto source = observation(geometry);
            auto bindings = bind(*store, geometry, source, plane(geometry), policy());
            auto prepared = get(a3::prepare(*store, bindings));
            auto output = get(a3::process(prepared, source));
            reserved_failures(*store, prepared, *output, bindings.observation.id);
            filesystem_failures(*store, prepared, *output);
            plane_readback(*store, geometry);
            std::cout << "ALG03 artifact dependency, admission and entered-state regressions passed\n";
            return 0;
        }
        ordinary_and_storage(*store, geometry);
        empty_and_order(*store, geometry);
        right_camera(*store, geometry);
        replay(*store, geometry);
        actual_alg02(*store, geometry);
        auto o = observation(geometry, 1);
        auto b = bind(*store, geometry, o, plane(geometry), policy());
        auto prepared = get(a3::prepare(*store, b));
        auto persisted_source = store->laser_observation(b.observation.id);
        auto before = encoded(*get(a3::process(prepared, persisted_source)));
        store.reset();
        o = {};
        CHECK(encoded(*get(a3::process(prepared, persisted_source))) == before);
        std::cout << "ALG03 typed persistence, replay, ALG02 lineage and refusal cases passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
