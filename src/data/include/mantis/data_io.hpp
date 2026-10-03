#pragma once
#include <filesystem>
#include <mantis/data.hpp>
namespace mantis::data {
// Versioned little-endian self-describing packet; immutable file mapping is the local data plane.
void write_packet(const std::filesystem::path &, const Packet &);
Published read_packet(const std::filesystem::path &);
} // namespace mantis::data
