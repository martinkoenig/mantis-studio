#include "../fixtures/calibration_artifacts.hpp"
#include <fstream>
#include <iostream>
#include <mantis/calibration_artifacts.hpp>
#include <mantis/capture_calibration.hpp>
#include <mantis/data_io.hpp>
#include <mantis/replay.hpp>
#include <mantis/services.hpp>
#include <nlohmann/json.hpp>
#include <thread>
using namespace mantis;
using namespace mantis::calibration;
namespace ca = calibration::artifacts;
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error("Line " + std::to_string(__LINE__) + ": " #x);                          \
    } while (false)
template <class T> T get(Result<T> r) {
    if (!r)
        throw Failure(r.error());
    return std::move(*r);
}
void get(Result<void> r) {
    if (!r)
        throw Failure(r.error());
}
struct Temp {
    std::filesystem::path path = std::filesystem::temp_directory_path() / Id::random().value;
    ~Temp() {
        std::filesystem::remove_all(path);
    }
};
bool same(const Reference &a, const Reference &b) {
    return a.id == b.id && a.schema_version == b.schema_version && a.revision == b.revision;
}
void discovery_geometry(const device::Descriptor &child) {
    const auto parsed = get(services::discovered_calibration_component(child));
    CHECK(parsed.role == child.metadata.at("role"));
    CHECK(parsed.camera_id.value == child.metadata.at("identity"));
    CHECK(parsed.image_width == 1280 && parsed.image_height == 960);
    for (const auto *field : {"width", "height"}) {
        auto missing = child;
        missing.metadata.erase(field);
        auto result = services::discovered_calibration_component(missing);
        CHECK(!result && result.error().code == Status::incompatible);
        CHECK(result.error().component == "capture");
        CHECK(result.error().message.find(std::string("missing required ") + field) != std::string::npos);
        for (const auto *invalid : {"", "not-a-number", "1280px", "-1", "0", "4294967296",
                                    "999999999999999999999", " 1280", "1280 ", "+1280", "1.5"}) {
            auto malformed = child;
            malformed.metadata[field] = invalid;
            result = services::discovered_calibration_component(malformed);
            CHECK(!result && result.error().code == Status::incompatible);
            CHECK(result.error().message.find(std::string("invalid ") + field + " '" + invalid + "'") !=
                  std::string::npos);
            CHECK(result.error().message.find(child.metadata.at("role")) != std::string::npos);
        }
    }
    auto packing = child;
    packing.metadata["fourcc"] = "Y10P";
    packing.metadata["target_fps"] = "120";
    packing.metadata["video_node"] = "/dev/video999";
    const auto unchanged = get(services::discovered_calibration_component(packing));
    CHECK(unchanged.camera_id == parsed.camera_id && unchanged.image_width == parsed.image_width &&
          unchanged.image_height == parsed.image_height);
}
void rejected_capture(services::Runtime &runtime, const Id &device, const std::string &diagnostic) {
    const auto captures_before = runtime.captures().size(), artifacts_before = runtime.artifacts().size();
    const auto events_before = runtime.events(0).size();
    bool rejected = false;
    try {
        runtime.start_capture({device});
    } catch (const Failure &error) {
        CHECK(error.error.code == Status::incompatible);
        CHECK(error.error.message.find(diagnostic) != std::string::npos);
        rejected = true;
    }
    CHECK(rejected && runtime.captures().size() == captures_before);
    CHECK(runtime.artifacts().size() == artifacts_before);
    CHECK(runtime.events(0).size() == events_before);
    for (const auto &capture : runtime.captures())
        CHECK(!capture.active);
}
void wait_job(services::Runtime &runtime, const Id &id) {
    for (unsigned i = 0; i < 3000; ++i) {
        for (auto &job : runtime.jobs())
            if (job.id == id) {
                if (job.state == jobs::State::failed)
                    throw std::runtime_error(job.diagnostics);
                if (job.state == jobs::State::completed)
                    return;
            }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    throw std::runtime_error("Job timeout");
}
void wait_frames(services::Runtime &runtime, const Id &id, uint64_t count) {
    for (unsigned i = 0; i < 1000; ++i) {
        for (auto &capture : runtime.captures())
            if (capture.id == id) {
                if (!capture.error.empty())
                    throw std::runtime_error("Capture " + id.value + ": " + capture.error);
                if (capture.committed >= count)
                    return;
            }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    throw std::runtime_error("Capture timeout");
}
artifact::ArtifactDescriptor finish(services::Runtime &runtime, services::CaptureInfo capture) {
    wait_frames(runtime, capture.id, 3);
    auto stopped = runtime.stop_capture(capture.id);
    if (!stopped.error.empty())
        throw std::runtime_error("Capture shutdown: " + stopped.error);
    wait_job(runtime, stopped.finalization_job);
    return runtime.project_store()->get(stopped.raw_artifact);
}
std::vector<Hash> check_replay(std::shared_ptr<artifact::Store> store, const Id &id,
                               const Reference &expected) {
    std::vector<Hash> pixels;
    auto source = device::recorded_source(store, id, false);
    get(source->start());
    for (;;) {
        auto p = get(source->next());
        if (!p)
            break;
        CHECK(same(p->header.calibration, expected));
        for (const auto &child : p->frames) {
            CHECK(same(child->header.calibration, expected));
            auto view = get(child->attributes[0].buffer.map_read());
            pixels.push_back(content_hash(view));
        }
    }
    get(source->stop());
    return pixels;
}
void seam(std::shared_ptr<artifact::Store> store, const artifact::ActiveCalibration &active) {
    auto raw = store->begin({"org.mantis.RawCapture", 2}, {});
    services::CaptureCalibrationBinding binding(store, raw, active);
    data::Packet fs;
    fs.type = schema::frameset;
    fs.header.calibration = {{"device.original"}, 9, 7};
    for (int i = 0; i < 2; ++i) {
        data::Packet image;
        image.type = schema::image;
        image.header.calibration = fs.header.calibration;
        image.attributes = {{{"org.mantis.pixels", schema::ScalarType::u8, {8, 8}, {8, 1}, "intensity"},
                             memory::copy(std::vector<std::byte>(64, std::byte{42}))}};
        fs.frames.push_back(data::publish(std::move(image)));
    }
    auto original = data::publish(fs), stamped = binding.stamp(original);
    for (size_t i = 0; i < 2; ++i) {
        CHECK(stamped->frames[i]->attributes[0].buffer.identity() ==
              original->frames[i]->attributes[0].buffer.identity());
        CHECK(get(stamped->frames[i]->attributes[0].buffer.map_read()).data() ==
              get(original->frames[i]->attributes[0].buffer.map_read()).data());
        CHECK(same(stamped->frames[i]->header.calibration, active.reference));
    }
    CHECK(same(original->header.calibration, Reference{{"device.original"}, 9, 7}));
    CHECK(store->get(raw).provenance.parameters.at("source_device_calibration_schema_version") == "9");
    auto mismatch = fs;
    auto changed = *fs.frames[1];
    changed.header.calibration.revision = 8;
    mismatch.frames[1] = data::publish(std::move(changed));
    bool rejected = false;
    try {
        binding.stamp(data::publish(mismatch));
    } catch (const Failure &e) {
        CHECK(e.error.code == Status::incompatible);
        rejected = true;
    }
    CHECK(rejected);
    auto later = fs;
    later.header.calibration.revision = 8;
    for (auto &child : later.frames) {
        auto c = *child;
        c.header.calibration.revision = 8;
        child = data::publish(std::move(c));
    }
    rejected = false;
    try {
        binding.stamp(data::publish(later));
    } catch (const Failure &e) {
        CHECK(e.error.code == Status::incompatible);
        rejected = true;
    }
    CHECK(rejected);
    store->abandon(raw);
    auto plain = store->begin({"org.mantis.RawCapture", 2}, {});
    services::CaptureCalibrationBinding no_active(store, plain, {});
    CHECK(no_active.stamp(original).get() == original.get());
    store->abandon(plain);
}
int main(int argc, char **argv) {
    try {
        CHECK(argc == 3);
        Temp tmp;
        std::filesystem::create_directories(tmp.path);
        nlohmann::json profile;
        {
            std::ifstream input(argv[2]);
            input >> profile;
        }
        profile["mode"] = {{"width", 1280}, {"height", 960}, {"fourcc", "GREY"}, {"fps", 5}};
        profile["calibration_id"] = "device.original";
        profile["calibration_revision"] = 7;
        profile["hardware_sync_configured"] = false;
        // Keep full-size geometry while limiting fixture throughput under sanitizers.
        // Software pairing requires tolerance above half the requested frame period.
        profile["max_v4l2_delta_ns"] = 110000000;
        profile["stall_timeout_ms"] = 1000;
        auto profile_path = tmp.path / "profile.json";
        std::ofstream(profile_path) << profile;
        setenv("MANTIS_X1_PROFILE", profile_path.c_str(), 1);
        setenv("MANTIS_X1_FAKE", "normal", 1);
        services::Configuration config{
            argv[1],
            std::filesystem::path(argv[1]).parent_path() / "bin/mantis-plugin-host",
            tmp.path / "project",
            std::filesystem::path(argv[1]).parent_path() / "recipes",
            {"org.mantis.x1", "org.mantis.virtual-scanner", "org.mantis.example-points", "org.mantis.ply"}};
        services::Runtime runtime(config);
        auto store = runtime.project_store();
        auto descriptors = runtime.devices();
        auto parent = *std::find_if(descriptors.begin(), descriptors.end(), [](const auto &d) {
            return d.plugin_id == "org.mantis.x1" && d.parent.value.empty();
        });
        std::vector<ca::CameraComponent> components;
        std::vector<DatasetCamera> cameras;
        for (const auto &child : descriptors)
            if (child.parent == parent.id) {
                auto role = child.metadata.at("role");
                Id identity{child.metadata.at("identity")};
                discovery_geometry(child);
                components.push_back(get(services::discovered_calibration_component(child)));
                cameras.push_back({role,
                                   identity,
                                   1280,
                                   960,
                                   {{"optical." + role}, "+X right, +Y down, +Z forward; millimeters"}});
            }
        std::sort(cameras.begin(), cameras.end(),
                  [](const auto &a, const auto &b) { return a.role < b.role; });
        CHECK(cameras.size() == 2);
        // Use the existing real Runtime/fake-X1 recording path even for the two upstream source captures.
        auto sourceA = runtime.start_capture({parent.id});
        wait_frames(runtime, sourceA.id, 14);
        auto unboundA = finish(runtime, sourceA);
        auto sourceB = runtime.start_capture({parent.id});
        wait_frames(runtime, sourceB.id, 14);
        auto unboundB = finish(runtime, sourceB);
        Reference source_ref{{"device.original"}, 1, 7};
        auto unbound_pixels = check_replay(store, unboundA.id, source_ref);
        CHECK(!unboundA.provenance.parameters.contains("active_calibration_id"));
        CHECK(!unboundA.provenance.parameters.contains("source_device_calibration_id"));
        CalibrationTarget target;
        target.grid = {8, 6, 40};
        target.pattern = CharucoDefinition{"DICT_6X6_250", 25};
        target.measurement = {8 * 41.3, 6 * 40.6, {}};
        auto persisted_target = get(ca::create_calibration_target(*store, target));
        std::vector<Id> raw_ids{unboundA.id, unboundB.id};
        std::sort(raw_ids.begin(), raw_ids.end());
        for (auto &camera : cameras) {
            auto fs = store->packet(unboundA.id);
            for (auto &child : fs->frames)
                if (child->header.metadata.at("role") == camera.role)
                    camera.optical_frame = child->header.frame;
        }
        auto dataset = m5fixture::fixture(persisted_target.value.target, raw_ids, cameras);
        for (auto &record : dataset.records) {
            record.key.frame.frameset_sequence -= 100;
            if (record.observation)
                record.observation->source.frameset_sequence -= 100;
        }
        get(validate_calibration_dataset(dataset));
        auto persisted_dataset =
            get(ca::create_calibration_dataset(*store, dataset, persisted_target.reference()));
        MonoSolveConfig mono;
        mono.heldout_per_camera = 3;
        auto left = get(solve_camera_intrinsics(dataset, "left", mono)),
             right = get(solve_camera_intrinsics(dataset, "right", mono));
        ca::SolverImplementation implementation{std::string(solver_opencv_version())};
        auto persisted_left = get(ca::create_camera_calibration(
            *store, left, persisted_dataset.reference(), persisted_target.reference(), implementation));
        auto persisted_right = get(ca::create_camera_calibration(
            *store, right, persisted_dataset.reference(), persisted_target.reference(), implementation));
        StereoSolveConfig stereo;
        stereo.left_role = "left";
        stereo.right_role = "right";
        stereo.heldout_pairs = 3;
        stereo.rig_frame = {{"rig.x1"}, "+X left to right, +Y forward, +Z cross; millimeters"};
        auto solved = get(solve_stereo_rig(dataset, left, right, stereo));
        auto rev1 = get(ca::create_rig_calibration(*store, solved, persisted_dataset.reference(),
                                                   persisted_target.reference(), persisted_left.reference(),
                                                   persisted_right.reference(), implementation));
        get(ca::activate_rig_calibration(*store, parent.id, rev1.descriptor.id, components));
        auto rev2 = get(ca::create_rig_calibration(
            *store, solved, persisted_dataset.reference(), persisted_target.reference(),
            persisted_left.reference(), persisted_right.reference(), implementation, rev1.value.revision.id));
        CHECK(store->active_calibration(parent.id)->reference.revision == 1);
        seam(store, *store->active_calibration(parent.id));
        auto captureA = runtime.start_capture({parent.id});
        wait_frames(runtime, captureA.id, 3);
        get(ca::activate_rig_calibration(*store, parent.id, rev2.descriptor.id, components));
        wait_frames(runtime, captureA.id, 6);
        auto a = finish(runtime, captureA);
        auto b = finish(runtime, runtime.start_capture({parent.id}));
        auto a_pixels = check_replay(store, a.id, rev1.value.revision),
             b_pixels = check_replay(store, b.id, rev2.value.revision);
        CHECK(unbound_pixels.size() >= 6 && a_pixels.size() >= 6 && b_pixels.size() >= 6);
        for (size_t i = 0; i < 6; ++i) {
            CHECK(unbound_pixels[i] == a_pixels[i]);
            CHECK(unbound_pixels[i] == b_pixels[i]);
        }
        for (const auto *capture : {&a, &b}) {
            auto expected = capture == &a ? rev1.value.revision : rev2.value.revision;
            auto ref = capture == &a ? rev1.reference() : rev2.reference();
            CHECK(same(capture->provenance.calibration, expected));
            CHECK(capture->provenance.parameters.at("logical_device_id") == parent.id.value);
            CHECK(capture->provenance.parameters.at("active_calibration_id") == expected.id.value);
            CHECK(capture->provenance.parameters.at("active_calibration_schema_version") == "1");
            CHECK(capture->provenance.parameters.at("active_calibration_revision") ==
                  std::to_string(expected.revision));
            CHECK(capture->provenance.parameters.at("active_rig_artifact_id") == ref.id.value);
            CHECK(capture->provenance.parameters.at("active_rig_artifact_hash") == ref.hash.hex);
            CHECK(capture->provenance.parameters.at("source_device_calibration_id") == "device.original");
            CHECK(capture->provenance.parameters.at("source_device_calibration_revision") == "7");
            auto initial = nlohmann::json::parse(capture->provenance.parameters.at("initial_observations"));
            for (const auto &item : initial) {
                CHECK(item["calibration"]["id"] == expected.id.value);
                CHECK(item["calibration"]["revision"] == expected.revision);
            }
        }
        // Exercise the runtime replay job in addition to its recorded_source implementation.
        wait_job(runtime, runtime.replay_capture(a.id, false, true));
        wait_job(runtime, runtime.replay_capture(b.id, false, true));
        CHECK(store->active_calibration(parent.id)->reference.revision == 2);
        CHECK(store->get(rev1.descriptor.id).hash == rev1.descriptor.hash);
        // Re-discover the SAME physical cameras with another capture geometry. Each rejection
        // must precede both RawCapture creation and acquisition Session ownership/start.
        for (const auto &[width, height] : {std::pair{1024, 960}, std::pair{1280, 800}}) {
            profile["mode"]["width"] = width;
            profile["mode"]["height"] = height;
            std::ofstream(profile_path) << profile;
            const auto changed = runtime.devices();
            size_t measurement_cameras = 0;
            for (const auto &child : changed)
                if (child.parent == parent.id) {
                    ++measurement_cameras;
                    const auto found = get(services::discovered_calibration_component(child));
                    const auto original = std::find_if(components.begin(), components.end(),
                                                       [&](const auto &c) { return c.role == found.role; });
                    CHECK(original != components.end() && found.camera_id == original->camera_id);
                    CHECK(found.image_width == uint32_t(width) && found.image_height == uint32_t(height));
                }
            CHECK(measurement_cameras == 2);
            rejected_capture(runtime, parent.id, "image geometry");
        }
        // Packing and FPS alone do not change geometry: actual packed fake-X1 capture succeeds.
        profile["mode"] = {{"width", 1280}, {"height", 960}, {"fourcc", "Y10P"}, {"fps", 10}};
        std::ofstream(profile_path) << profile;
        runtime.devices();
        auto packed = finish(runtime, runtime.start_capture({parent.id}));
        CHECK(same(packed.provenance.calibration, rev2.value.revision));
        check_replay(store, packed.id, rev2.value.revision);
        profile["mode"] = {{"width", 1280}, {"height", 960}, {"fourcc", "GREY"}, {"fps", 5}};
        std::ofstream(profile_path) << profile;
        runtime.devices();
        auto matching = finish(runtime, runtime.start_capture({parent.id}));
        CHECK(same(matching.provenance.calibration, rev2.value.revision));
        // Generic metadata can contain a binding for the wrong device; capture validates it before starting
        // Session.
        store->activate_calibration(parent.id, rev2.descriptor.id);
        auto bad_components = components;
        bad_components[0].camera_id = {"wrong"};
        CHECK(!ca::activate_rig_calibration(*store, parent.id, rev2.descriptor.id, bad_components));
        // A valid graph for a different physical LEFT camera must fail before Session starts.
        auto other_dataset = dataset;
        for (auto &camera : other_dataset.cameras)
            if (camera.role == "left")
                camera.camera_id = {"another.physical.camera"};
        for (auto &record : other_dataset.records)
            if (record.key.camera_role == "left") {
                record.key.camera_id = {"another.physical.camera"};
                if (record.observation)
                    record.observation->source.camera_id = record.key.camera_id;
            }
        auto other_data =
            get(ca::create_calibration_dataset(*store, other_dataset, persisted_target.reference()));
        auto other_left = left;
        other_left.camera.camera_id = {"another.physical.camera"};
        for (auto &sample : other_left.partition.samples)
            sample.key.camera_id = other_left.camera.camera_id;
        for (auto *stage : {&other_left.training_fit, &other_left.heldout_validation, &other_left.final_fit})
            for (auto &view : stage->views)
                view.key.camera_id = other_left.camera.camera_id;
        auto other_l = get(ca::create_camera_calibration(*store, other_left, other_data.reference(),
                                                         persisted_target.reference(), implementation));
        auto other_r = get(ca::create_camera_calibration(*store, right, other_data.reference(),
                                                         persisted_target.reference(), implementation));
        auto other_solved = solved;
        other_solved.left_camera = other_left.camera;
        auto other_rig = get(ca::create_rig_calibration(*store, other_solved, other_data.reference(),
                                                        persisted_target.reference(), other_l.reference(),
                                                        other_r.reference(), implementation));
        store->activate_calibration(parent.id, other_rig.descriptor.id);
        rejected_capture(runtime, parent.id, "physical cameras");
        store->clear_active_calibration(parent.id);
        auto after_clear = finish(runtime, runtime.start_capture({parent.id}));
        CHECK(same(after_clear.provenance.calibration, source_ref));
        check_replay(store, after_clear.id, source_ref);
        std::cout << "Real Runtime/fake-X1: no-active, snapshot rev1 while active changes, future rev2, "
                     "historical replay, identical pixel hashes and BufferView identities, strict discovery "
                     "geometry, capture-start geometry rejection and packing/FPS compatibility passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
