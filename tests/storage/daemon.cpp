#include "faults.hpp"
#define main mantis_daemon_entry
#include "../../apps/mantisd/main.cpp"
#undef main
int main(int argc, char **argv) {
    // Isolated test executable only. The ordinary mantisd has no fault selector.
    std::filesystem::path root;
    for (int i = 1; i + 1 < argc; ++i)
        if (std::string_view(argv[i]) == "--project")
            root = argv[i + 1];
    if (root.empty())
        return 2;
    mantis::artifact::storage_test::Arm fault(
        {root, {}, "finalize-commit-before", mantis::artifact::storage_test::Action::reject_commit});
    return mantis_daemon_entry(argc, argv);
}
