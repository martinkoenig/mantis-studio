#pragma once
#include <filesystem>
#include <mantis/semantic_packet.hpp>
#include <ostream>
namespace mantis::data {
inline constexpr uint64_t max_observation_record_bytes = 128 * 1024 * 1024;
void write_laser_observation(std::ostream &, const LaserObservation &);
void write_laser_observation(const std::filesystem::path &, const LaserObservation &);
LaserObservation read_laser_observation(memory::BufferView);
LaserObservation read_laser_observation(const std::filesystem::path &);
// Local isolated-host transport: explicitly tagged, never flattens composites.
void write_semantic_packet(std::ostream &, const SemanticPacket &);
void write_semantic_packet(const std::filesystem::path &, const SemanticPacket &);
SemanticPacket read_semantic_packet(memory::BufferView);
SemanticPacket read_semantic_packet(const std::filesystem::path &);
} // namespace mantis::data
