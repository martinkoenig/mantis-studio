#pragma once
// Private compile-time seam. Only the isolated storage qualification library
// defines MANTIS_STORAGE_FAULT_TESTING. Product builds have no control object,
// callback, environment selector, allocation or checkpoint branch.
#include <fstream>
#ifdef MANTIS_STORAGE_FAULT_TESTING
#include <filesystem>
#include <memory>
#include <string_view>
struct sqlite3;
namespace mantis::artifact::storage_test {
void checkpoint(std::string_view point, const std::filesystem::path &root, std::string_view id);
class Output final : public std::ostream {
    class Buffer;
    std::unique_ptr<Buffer> buffer_;

  public:
    Output();
    explicit Output(const std::filesystem::path &, std::ios::openmode = std::ios::out);
    ~Output() override;
    void open(const std::filesystem::path &, std::ios::openmode = std::ios::out);
    void close();
    bool is_open() const;
};
class CommitGuard {
    sqlite3 *db_{};
    bool rejected_{};
    size_t slot_{8};

  public:
    CommitGuard(std::string_view, const std::filesystem::path &, std::string_view, sqlite3 *);
    ~CommitGuard();
    CommitGuard(const CommitGuard &) = delete;
};
} // namespace mantis::artifact::storage_test
#define MANTIS_STORAGE_POINT(point, root, id) ::mantis::artifact::storage_test::checkpoint(point, root, id)
#define MANTIS_STORAGE_COMMIT(point, root, id, db)                                                           \
    ::mantis::artifact::storage_test::CommitGuard storage_commit_guard(point, root, id, db)
#else
#define MANTIS_STORAGE_POINT(point, root, id) ((void)0)
#define MANTIS_STORAGE_COMMIT(point, root, id, db) ((void)0)
#endif
namespace mantis::artifact {
#ifdef MANTIS_STORAGE_FAULT_TESTING
using CaptureOutput = storage_test::Output;
#else
using CaptureOutput = std::ofstream;
#endif
} // namespace mantis::artifact
