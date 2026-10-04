#pragma once
// Private native/fake seam: entity IDs and pad descriptions, never Linux ABI structs.
#include "backend.hpp"
namespace x1 {
struct Pad { uint32_t index{}; bool source{}, sink{}; };
struct Entity { uint32_t id{}; std::string name, node; bool capture{}; std::vector<Pad> pads; };
struct Endpoint { uint32_t entity{}, pad{}; auto operator<=>(const Endpoint &) const = default; };
struct Link { Endpoint source, sink; bool enabled{}, immutable{}; };
struct Graph { std::string path, bus; std::vector<Entity> entities; std::vector<Link> links; };
struct PadFormat {
    uint32_t code{}, width{}, height{}, field{}, colorspace{}, ycbcr{}, quantization{}, transfer{}, flags{};
    auto operator<=>(const PadFormat &) const = default;
};
struct Route { std::vector<Entity> entities; std::vector<Link> links; std::string role; };
Route select_route(const Graph &, const std::vector<std::string> &, const std::string &role);
std::vector<std::vector<std::string>> possible_routes(const Graph &, const Entity &, bool enabled_only);
class MediaIo {
  public:
    virtual ~MediaIo() = default;
    virtual Graph graph(const std::string &path) = 0;
    virtual void link(const std::string &media, const Link &, bool enabled) = 0;
    virtual uint32_t format_code(const std::string &) = 0;
    virtual std::vector<uint32_t> codes(const Entity &, uint32_t pad) = 0;
    virtual PadFormat get_format(const Entity &, uint32_t pad) = 0;
    virtual PadFormat set_format(const Entity &, uint32_t pad, const PadFormat &) = 0;
    virtual int32_t get_vblank(const Entity &) = 0;
    virtual int32_t set_vblank(const Entity &, int32_t) = 0;
    virtual Metadata timing(const Entity &) = 0;
};
std::unique_ptr<MediaIo> fake_media(const Profile &, const std::array<CameraInfo, 2> &, const std::string &scenario);
std::unique_ptr<Setup> configure_media(MediaIo &, const Profile &, const std::array<CameraInfo, 2> &);
} // namespace x1
