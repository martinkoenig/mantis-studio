#pragma once
#include <filesystem>
#include <ostream>
#include <mantis/data.hpp>
namespace mantis::data {
void write_packet(std::ostream &, const Packet &);
Published read_packet(memory::BufferView);
// Versioned little-endian self-describing packet; immutable file mapping is the local data plane.
void write_packet(const std::filesystem::path &, const Packet &);
Published read_packet(const std::filesystem::path &);
} // namespace mantis::data
