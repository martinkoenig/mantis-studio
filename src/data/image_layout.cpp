#include <mantis/image_layout.hpp>
#include <charconv>
#include <cstring>
namespace mantis::data {
namespace {
uint32_t field(const Packet &packet, const std::string &key) {
    auto found = packet.header.metadata.find(key);
    if (found == packet.header.metadata.end()) fail(Status::corrupt, "Missing image metadata: " + key);
    uint32_t value{};
    auto parsed = std::from_chars(found->second.data(), found->second.data() + found->second.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != found->second.data() + found->second.size())
        fail(Status::corrupt, "Invalid image metadata: " + key);
    return value;
}
const Attribute &pixels(const Packet &packet, ImagePacking packing) {
    std::string name(packing == ImagePacking::mipi_raw10 ? packed_image_bytes : "org.mantis.pixels");
    const Attribute *found{};
    for (const auto &attribute : packet.attributes) if (attribute.descriptor.name == name) {
        if (found) fail(Status::corrupt, "Duplicate image byte attributes");
        found = &attribute;
    }
    if (!found) fail(Status::unsupported, "Image has no supported byte attribute");
    return *found;
}
}
Y10PView::Y10PView(std::span<const std::byte> bytes, uint32_t width, uint32_t height, uint32_t stride)
    : bytes_(bytes), width_(width), height_(height), stride_(stride) {
    if (!width || !height || width % 4 || uint64_t(width) * 5 / 4 > stride ||
        uint64_t(height - 1) * stride + uint64_t(width) * 5 / 4 > bytes.size())
        fail(Status::corrupt, "Invalid/truncated Y10P row layout");
}
uint16_t Y10PView::sample(uint32_t x, uint32_t y) const {
    if (x >= width_ || y >= height_) fail(Status::invalid_argument, "Y10P sample outside image");
    auto offset = size_t(y) * stride_ + size_t(x / 4) * 5;
    auto high = std::to_integer<uint16_t>(bytes_[offset + x % 4]);
    auto low = (std::to_integer<unsigned>(bytes_[offset + 4]) >> ((x % 4) * 2)) & 3u;
    return static_cast<uint16_t>((high << 2) | low);
}
void Y10PView::display_row(uint32_t y, std::span<std::byte> out) const {
    if (y >= height_ || out.size() != width_) fail(Status::invalid_argument, "Invalid Y10P display row");
    for (uint32_t x = 0; x < width_; ++x)
        out[x] = bytes_[size_t(y) * stride_ + size_t(x / 4) * 5 + x % 4];
}
ImageLayout image_layout(const Packet &packet) {
    if (packet.type != schema::image) fail(Status::incompatible, "Expected ImageFrame");
    bool packed = packet.header.metadata.contains("org.mantis.image.layout");
    auto fourcc = packet.header.metadata.find("fourcc");
    if (!packed && fourcc != packet.header.metadata.end() && fourcc->second == "Y10P")
        fail(Status::corrupt, "Y10P requires explicit packed image layout metadata");
    ImageLayout layout;
    if (packed) {
        if (packet.header.metadata.at("org.mantis.image.layout") != "mipi-raw10-v1" ||
            !packet.header.metadata.contains("fourcc") || packet.header.metadata.at("fourcc") != "Y10P" ||
            field(packet, "org.mantis.image.bits_per_sample") != 10)
            fail(Status::unsupported, "Unsupported packed image layout");
        layout = {field(packet, "org.mantis.image.width"), field(packet, "org.mantis.image.height"),
                  field(packet, "org.mantis.image.row_stride_bytes"), ImagePacking::mipi_raw10};
        const auto &a = pixels(packet, layout.packing);
        if (a.descriptor.scalar != schema::ScalarType::u8 || a.descriptor.shape != std::vector<uint64_t>{a.buffer.size()} ||
            a.descriptor.stride != std::vector<uint64_t>{1})
            fail(Status::corrupt, "Packed bytes must be a rank-one u8 byte extent");
        auto mapped = a.buffer.map_read(); if (!mapped) throw Failure(mapped.error());
        (void)Y10PView(*mapped, layout.width, layout.height, layout.row_stride);
    } else {
        const auto &a = pixels(packet, ImagePacking::raw8);
        if (a.descriptor.scalar != schema::ScalarType::u8 || a.descriptor.shape.size() != 2 ||
            a.descriptor.stride.size() != 2 || a.descriptor.stride[1] != 1 ||
            a.descriptor.shape[0] > UINT32_MAX || a.descriptor.shape[1] > UINT32_MAX || a.descriptor.stride[0] > UINT32_MAX)
            fail(Status::unsupported, "Unsupported RAW8 layout");
        layout = {static_cast<uint32_t>(a.descriptor.shape[1]), static_cast<uint32_t>(a.descriptor.shape[0]),
                  static_cast<uint32_t>(a.descriptor.stride[0]), ImagePacking::raw8};
        if (!layout.width || !layout.height || layout.row_stride < layout.width ||
            uint64_t(layout.height - 1) * layout.row_stride + layout.width > a.buffer.size())
            fail(Status::corrupt, "Invalid/truncated RAW8 row layout");
    }
    return layout;
}
void grayscale_row(const Packet &packet, const ImageLayout &layout, uint32_t y, std::span<std::byte> out) {
    if (y >= layout.height || out.size() != layout.width) fail(Status::invalid_argument, "Invalid grayscale row");
    auto mapped = pixels(packet, layout.packing).buffer.map_read(); if (!mapped) throw Failure(mapped.error());
    if (layout.packing == ImagePacking::mipi_raw10) Y10PView(*mapped, layout.width, layout.height, layout.row_stride).display_row(y, out);
    else {
        uint64_t offset = uint64_t(y) * layout.row_stride;
        if (offset > mapped->size() || out.size() > mapped->size() - offset) fail(Status::corrupt, "Truncated grayscale row");
        std::memcpy(out.data(), mapped->data() + offset, out.size());
    }
}
} // namespace mantis::data
