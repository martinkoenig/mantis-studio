#pragma once
#include <filesystem>
#include <fstream>
#include <mantis/data_io.hpp>
#include <mantis/projected_light_io.hpp>
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
Scan scan(const std::filesystem::path &, uint32_t schema = 2, const CancellationToken & = {});
void append(std::ostream &, const data::Packet &);
void append(std::ostream &, const data::AcquisitionBundle &);
data::AcquisitionBundle bundle(const Scan &, size_t);
data::Published packet(const Scan &, size_t);
} // namespace mantis::artifact::segments
