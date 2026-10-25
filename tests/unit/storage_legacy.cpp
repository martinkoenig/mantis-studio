#include "../../src/artifact-store/segments.hpp"
#include <fstream>
#include <mantis/data_io.hpp>
#include <sstream>
using namespace mantis;
void check(bool ok) {
    if (!ok)
        throw std::runtime_error("Legacy byte regression");
}
std::string bytes(const std::filesystem::path &p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), {}};
}
// Independent semantic value for the immutable legacy bytes, not a decode-derived expectation.
data::Published fixture(bool composite) {
    data::Packet image;
    image.type = schema::image;
    image.header.sequence.value = 7;
    image.header.timestamp = {0, {{"c"}, "clock"}};
    image.header.received = {9};
    image.header.sync = {{"sync"}, 2};
    image.header.sync_quality = time::SyncQuality::software;
    image.header.calibration = {{"cal"}, 1, 3};
    image.header.frame = {{"world"}, "World"};
    image.header.metadata = {{"role", "left"}};
    memory::BufferBuilder pixels(4);
    auto b = pixels.writable();
    b[0] = std::byte{0};
    b[1] = std::byte{1};
    b[2] = std::byte{127};
    b[3] = std::byte{255};
    image.attributes = {
        {{"org.mantis.pixels", schema::ScalarType::u8, {4}, {1}, "intensity"}, std::move(pixels).publish()}};
    auto published = data::publish(std::move(image));
    if (!composite)
        return published;
    data::Packet set;
    set.type = schema::frameset;
    set.header = published->header;
    set.frames = {published};
    return data::publish(std::move(set));
}
int main(int argc, char **argv) {
    try {
        check(argc == 2);
        std::filesystem::path root(argv[1]);
        for (auto name : {"mantis01", "mantis02"}) {
            auto file = root / (std::string(name) + ".bin");
            auto p = data::read_packet(file);
            check(p->header.sequence.value == 7 && p->header.received.nanoseconds == 9);
            check(p->header.metadata.at("role") == "left");
            auto image = p->type == schema::frameset ? p->frames.at(0) : p;
            check(image->type == schema::image && image->attributes.size() == 1);
            auto pixel = *image->attributes[0].buffer.map_read();
            check(pixel.size() == 4 && pixel[3] == std::byte{255});
            std::ostringstream out(std::ios::binary);
            data::write_packet(out, *p);
            check(out.str() == bytes(file));
            std::ostringstream independently_encoded;
            data::write_packet(independently_encoded, *fixture(std::string_view(name) == "mantis02"));
            check(independently_encoded.str() == bytes(file));
        }
        auto record = root / "mrawrec2.bin";
        auto scan = artifact::segments::scan(record);
        check(!scan.incomplete && scan.corruption.empty() && scan.records.size() == 1);
        auto p = artifact::segments::packet(scan, 0);
        std::ostringstream out(std::ios::binary);
        artifact::segments::append(out, *p);
        check(out.str() == bytes(record));
        std::ostringstream independently_framed;
        artifact::segments::append(independently_framed, *fixture(true));
        check(independently_framed.str() == bytes(record));
        return 0;
    } catch (const std::exception &e) {
        fprintf(stderr, "%s\n", e.what());
        return 1;
    }
}
