#pragma once
#include <filesystem>
#include <fstream>
#include <mantis/data_io.hpp>
namespace mantis::artifact::segments {
inline constexpr uint64_t max_segment_bytes = 64 * 1024 * 1024;
struct Record { size_t offset{}, bytes{}; };
struct Scan {
    memory::BufferView mapping;
    std::vector<Record> records;
    size_t complete_bytes{};
    bool incomplete{};
    std::string corruption;
};
// A mapped segment retains immutable ownership in every returned packet.
Scan scan(const std::filesystem::path &);
void append(std::ostream &, const data::Packet &);
data::Published packet(const Scan &, size_t);
} // namespace mantis::artifact::segments
