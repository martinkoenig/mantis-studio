#pragma once
#include <mantis/data.hpp>
namespace mantis::device {
inline constexpr std::string_view image_stream = "org.mantis.camera.image-stream.v1";
inline constexpr std::string_view frameset_stream = "org.mantis.camera.frameset-stream.v1";
struct Descriptor {
    Id id;
    std::string name, plugin_id;
    std::vector<std::string> capabilities;
    std::vector<Id> children;
    Id parent;
    data::Metadata metadata;
};
enum class State { discovered, open, streaming, stopped, failed };
struct CaptureDescriptor {
    Id id;
    std::vector<Id> devices;
    calibration::Reference calibration;
};
// Native implementations are adapted from the versioned C device interface; never a scanner hierarchy.
class ImageStream {
  public:
    virtual ~ImageStream() = default;
    virtual const Descriptor &descriptor() const = 0;
    virtual Result<void> start() = 0;
    virtual Result<data::Published> next() = 0;
    virtual Result<void> stop() = 0;
    virtual std::vector<Descriptor> components() const { return {}; }
    virtual data::Metadata diagnostics() const { return {}; }
    virtual bool finished() const { return false; }
    virtual bool source_paced() const { return false; }
};
} // namespace mantis::device
