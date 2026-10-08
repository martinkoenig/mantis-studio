#include "../../src/artifact-store/segments.hpp"
#include "projected_storage_values.hpp"
#include <fstream>
#include <iostream>
using namespace storage_fixture;
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(std::string(#x) + " line " + std::to_string(__LINE__));                 \
    } while (false)
template <class F> void rejects(F f) {
    bool ok = false;
    try {
        f();
    } catch (const std::exception &) {
        ok = true;
    }
    CHECK(ok);
}
std::string file(const std::filesystem::path &p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}
memory::BufferView view(std::string_view b) {
    return memory::copy({reinterpret_cast<const std::byte *>(b.data()), b.size()});
}
void u64(std::string &b, size_t p, uint64_t n) {
    CHECK(p + 8 <= b.size());
    for (unsigned i = 0; i < 8; ++i)
        b[p + i] = static_cast<char>(n >> (8 * i));
}
uint64_t u64(const std::string &b, size_t p) {
    uint64_t n{};
    for (unsigned i = 0; i < 8; ++i)
        n |= uint64_t(static_cast<unsigned char>(b.at(p + i))) << (8 * i);
    return n;
}
int main(int argc, char **argv) {
    try {
        CHECK(argc == 2);
        std::filesystem::path root(argv[1]);
        unsigned n = 0;
        for (auto name : {"evidence", "captured", "trigger", "terminal"}) {
            auto expected = file(root / (std::string("mantis03-") + name + ".bin"));
            auto value = bundle(n++);
            CHECK(validate(value));
            CHECK(encode(value) == expected);
            CHECK(bundle_encoded_size(value) == expected.size());
            auto storage = view(expected);
            auto decoded = read_bundle(storage);
            CHECK(encode(decoded) == expected);
            CHECK(decoded.key == value.key);
            if (decoded.frameset) {
                auto &pixels = decoded.frameset->frames[0]->attributes[0].buffer;
                CHECK(pixels.identity() ==
                      storage.identity()); // application-level mapped slice, no pixel copy
                CHECK((*pixels.map_read())[3] == std::byte{255});
                CHECK(decoded.evidence.frames[0].camera_calibration.get()->content.get()->id ==
                      Id{"camera-art"});
            }
            for (size_t cut = 0; cut < expected.size(); ++cut)
                rejects([&] { (void)read_bundle(view(std::string_view(expected).substr(0, cut))); });
            auto trailing = expected + "x";
            rejects([&] { (void)read_bundle(view(trailing)); });
        }
        {
            // Exact multi-image membership/order and unknown namespaced attributes remain unchanged.
            auto value = bundle(1);
            auto set = *value.frameset;
            auto image = *set.frames[0];
            image.header.sequence.value = 42;
            image.header.metadata["role"] = "right";
            image.attributes.push_back({{"org.example.flags", schema::ScalarType::u32, {1}, {4}, "flags"},
                                        image.attributes[0].buffer});
            set.frames.push_back(publish(std::move(image)));
            value.frameset = publish(std::move(set));
            ComponentId second{{"cam-two"}};
            StreamId stream{{"stream-two"}};
            value.evidence.participants.cameras.push_back({second, stream, "right"});
            auto f = value.evidence.frames[0];
            f.frame.camera = second;
            f.frame.stream.id = stream;
            f.frame.native_sequence = 42;
            f.camera_role = "right";
            f.sync = Unknown{};
            auto exposure = *f.exposure.get();
            exposure.evidence.source = second;
            exposure.uncertainty_ns = -0.0;
            f.exposure = exposure;
            value.evidence.frames.push_back(f);
            value.evidence.emitters[0].exposure_effective.push_back({f.frame, Unavailable{}});
            auto encoded = encode(value);
            auto storage = view(encoded);
            auto decoded = read_bundle(storage);
            CHECK(encode(decoded) == encoded);
            CHECK(decoded.frameset->frames.size() == 2 &&
                  decoded.frameset->frames[0]->header.sequence.value == 7 &&
                  decoded.frameset->frames[1]->header.sequence.value == 42);
            CHECK(decoded.frameset->frames[1]->attributes.size() == 2);
            for (const auto &im : decoded.frameset->frames)
                for (const auto &a : im->attributes)
                    CHECK(a.buffer.identity() == storage.identity());
        }
        {
            // Structural lengths never map pixels; record checksum + direct write map exactly twice.
            struct CountFence final : memory::Fence {
                mutable unsigned calls{};
                Result<void> wait(const CancellationToken &) const override {
                    ++calls;
                    return {};
                }
            };
            auto counted = bundle(1);
            auto set = *counted.frameset;
            auto image = *set.frames[0];
            auto original = image.attributes[0].buffer.map_read();
            CHECK(original);
            auto backing = std::make_shared<memory::Storage>();
            auto fence = std::make_shared<CountFence>();
            backing->owner = std::make_shared<Published>(counted.frameset);
            backing->host = original->data();
            backing->size = original->size();
            backing->ready = fence;
            image.attributes[0].buffer = {backing, 0, backing->size};
            set.frames = {publish(std::move(image))};
            counted.frameset = publish(std::move(set));
            (void)bundle_encoded_size(counted);
            CHECK(fence->calls == 0);
            std::ostringstream measured;
            artifact::segments::append(measured, counted);
            CHECK(fence->calls == 2);
            // Bounds reject a claimed oversized payload before attempting any pixel mapping/allocation.
            auto oversized = bundle(1);
            set = *oversized.frameset;
            image = *set.frames[0];
            backing = std::make_shared<memory::Storage>();
            backing->size = 129ull * 1024 * 1024;
            image.attributes[0].buffer = {backing, 0, backing->size};
            image.attributes[0].descriptor.shape = {backing->size};
            image.attributes[0].descriptor.stride = {1};
            set.frames = {publish(std::move(image))};
            oversized.frameset = publish(std::move(set));
            rejects([&] { (void)bundle_encoded_size(oversized); });
            class Nonseekable final : public std::stringbuf {
                pos_type seekoff(off_type, std::ios_base::seekdir, std::ios_base::openmode) override {
                    return pos_type(off_type(-1));
                }
            } sink;
            std::ostream direct(&sink);
            write_bundle(direct, bundle(1));
            CHECK(sink.str() == encode(bundle(1)));
        }
        auto h = file(root / "run-header3.bin");
        CHECK(encode(header()) == h);
        CHECK(encode(read_capture_header(view(h))) == h);
        for (size_t cut = 0; cut < h.size(); ++cut)
            rejects([&] { (void)read_capture_header(view(std::string_view(h).substr(0, cut))); });
        for (auto presence : {0, 1}) {
            auto value = header();
            value.program.identity.hash =
                presence ? Evidence<Hash>{Unavailable{}} : Evidence<Hash>{Unknown{}};
            value.program.identity.content =
                presence ? Evidence<ContentReference>{Unknown{}} : Evidence<ContentReference>{Unavailable{}};
            CHECK(encode(read_capture_header(view(encode(value)))) == encode(value));
        }
        auto scan = artifact::segments::scan(root / "mrawrec3.bin", 3);
        CHECK(!scan.incomplete && scan.corruption.empty() && scan.records.size() == 1);
        std::ostringstream record;
        artifact::segments::append(record, bundle(0));
        CHECK(record.str() == file(root / "mrawrec3.bin"));
        CHECK(!artifact::segments::scan(root / "mrawrec3.bin", 2).corruption.empty());
        CHECK(!artifact::segments::scan(root / "mrawrec2.bin", 3).corruption.empty());
        auto base = encode(bundle(0));
        const auto member = base.find("MEVID001") - 16;
        CHECK(member < base.size());
        auto mutate = [&](size_t p, uint64_t v) {
            auto bad = base;
            u64(bad, p, v);
            rejects([&] { (void)read_bundle(view(bad)); });
        };
        auto bad = base;
        bad[0] = 'X';
        rejects([&] { (void)read_bundle(view(bad)); });
        mutate(8, 2);
        mutate(member - 8, 65);
        mutate(member - 8, 0);
        mutate(member, 0);
        mutate(member, 4);
        mutate(member + 8, UINT64_MAX);
        mutate(member + 8, 1);
        const auto evidence_start = member + 32;
        const auto step_presence =
            evidence_start + 16 + std::string("org.mantis.AcquisitionEvidence").size() + 19 + 134;
        bad = base;
        bad[step_presence] = char(3);
        rejects([&] { (void)read_bundle(view(bad)); });
        bad = base;
        auto diag = bad.find("fixture");
        CHECK(diag != std::string::npos);
        bad[diag - 26] = char(255);
        rejects([&] { (void)read_bundle(view(bad)); });
        bad = base;
        bad[diag - 27] = char(2);
        rejects([&] { (void)read_bundle(view(bad)); });
        bad = base;
        u64(bad, member - 8, 2);
        bad += base.substr(member);
        rejects([&] { (void)read_bundle(view(bad)); });
        auto missing = base.substr(0, member);
        u64(missing, member - 8, 0);
        rejects([&] { (void)read_bundle(view(missing)); });
        auto captured = encode(bundle(1));
        auto image = captured.find("MANTIS01");
        CHECK(image != std::string::npos);
        bad = captured;
        bad.replace(image, 8, "MANTIS02");
        rejects([&] { (void)read_bundle(view(bad)); });
        auto frame_member = captured.find("MANTIS02") - 16;
        CHECK(frame_member < captured.size());
        bad = captured;
        u64(bad, member - 8, 3);
        bad += captured.substr(frame_member);
        rejects([&] { (void)read_bundle(view(bad)); });
        bad = captured;
        auto command = bad.find("on-request");
        CHECK(command != std::string::npos);
        bad[command + 22] = char(255);
        rejects([&] { (void)read_bundle(view(bad)); });
        bad = captured;
        u64(bad, frame_member + 8, u64(bad, frame_member + 8) - 1);
        bad.pop_back();
        rejects([&] { (void)read_bundle(view(bad)); });
        // Excessive table count, ID/string bounds, uint32 overflow and invalid semantic version.
        mutate(evidence_start, UINT64_MAX);
        mutate(member + 24, 2);
        bad = base;
        u64(bad, evidence_start + 8 + std::string("org.mantis.AcquisitionEvidence").size(),
            uint64_t{1} << 32);
        rejects([&] { (void)read_bundle(view(bad)); });
        auto header_bad = h;
        header_bad[40] ^= 1;
        rejects([&] { (void)read_capture_header(view(header_bad)); });
        // Complete record framing failures do not become incomplete tails.
        auto temp = std::filesystem::temp_directory_path() / Id::random().value;
        struct Cleanup {
            std::filesystem::path p;
            ~Cleanup() { std::filesystem::remove(p); }
        } cleanup{temp};
        auto rec = record.str();
        for (unsigned mode = 0; mode < 5; ++mode) {
            auto malformed = rec;
            if (mode == 0) {
                u64(malformed, 8, 128ull * 1024 * 1024 + 1);
                u64(malformed, 24, ~(128ull * 1024 * 1024 + 1));
            }
            if (mode == 1)
                malformed[24] ^= 1;
            if (mode == 2)
                malformed[16] ^= 1;
            if (mode == 3)
                malformed.back() ^= 1;
            if (mode == 4)
                malformed.replace(0, 8, "MRAWREC2");
            {
                std::ofstream out(temp, std::ios::binary);
                out.write(malformed.data(), static_cast<std::streamsize>(malformed.size()));
            }
            auto scan_bad = artifact::segments::scan(temp, 3);
            CHECK(!scan_bad.corruption.empty() && !scan_bad.incomplete);
        }
        std::cout << "Independent legacy/v3 bytes, complete typed roundtrips, every truncation and zero-copy "
                     "slice passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
