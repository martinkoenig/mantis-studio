#pragma once
#include <mantis/calibration.hpp>
#include <mantis/memory.hpp>
#include <mantis/schema.hpp>
#include <mantis/time.hpp>
#include <variant>
namespace mantis::data {
using Metadata = std::map<std::string, std::string>;
struct Attribute {
    schema::AttributeDescriptor descriptor;
    memory::BufferView buffer;
};
struct Header {
    time::SequenceNumber sequence;
    time::DeviceTimestamp timestamp;
    time::MonotonicTimestamp received;
    time::SyncGroup sync;
    time::SyncQuality sync_quality{time::SyncQuality::unknown};
    calibration::Reference calibration;
    spatial::CoordinateFrame frame{spatial::world};
    Metadata metadata;
};
// Build mutable values locally; publication and all downstream APIs use const shared ownership.
struct ImageFrame {
    Header header;
    Attribute pixels;
};
struct FrameSet {
    Header header;
    std::vector<std::shared_ptr<const ImageFrame>> frames;
};
struct PointCloud {
    Header header;
    std::map<std::string, Attribute> attributes;
};
struct Mesh {
    PointCloud vertices;
    Attribute indices;
};
struct Tensor {
    Header header;
    Attribute values;
};
struct Packet {
    schema::DataTypeId type;
    Header header;
    std::vector<Attribute> attributes;
    std::vector<std::shared_ptr<const Packet>> frames;
};
using Published = std::shared_ptr<const Packet>;
inline Published publish(Packet packet) {
    if (packet.type == schema::frameset) {
        if (packet.frames.empty() || packet.frames.size() > 16 || !packet.attributes.empty())
            fail(Status::invalid_argument, "FrameSet requires 1..16 image children and no flat attributes");
        for (const auto &frame : packet.frames)
            if (!frame || frame->type != schema::image || !frame->frames.empty())
                fail(Status::invalid_argument, "FrameSet child must be an ImageFrame");
    } else if (!packet.frames.empty())
        fail(Status::invalid_argument, "Only FrameSet may contain image children");
    for (const auto &a : packet.attributes) {
        auto r = schema::validate(a.descriptor, a.buffer.size());
        if (!r)
            throw Failure(r.error());
    }
    return std::make_shared<const Packet>(std::move(packet));
}
inline Result<PointCloud> point_cloud(const Packet &packet) {
    if (packet.type != schema::points)
        return std::unexpected(Error{Status::incompatible, "Expected PointCloud", "data"});
    PointCloud cloud;
    cloud.header = packet.header;
    for (const auto &a : packet.attributes)
        cloud.attributes.emplace(a.descriptor.name, a);
    if (!cloud.attributes.contains("org.mantis.position"))
        return std::unexpected(Error{Status::corrupt, "PointCloud has no positions", "data"});
    return cloud;
}
} // namespace mantis::data
