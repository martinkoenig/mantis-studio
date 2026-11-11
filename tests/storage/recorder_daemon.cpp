#include "recorder_observer.hpp"
#define main mantis_recorder_daemon_entry
#include "../../apps/mantisd/main.cpp"
#undef main
int main(int argc, char **argv) {
    // Reuse the real daemon entrypoint, server, Runtime and ownership. No second
    // runtime/supervisor or fault/pacing selector. Options exist only here.
    if (argc == 3 && (std::string_view(argv[1]) == "--observer-self-test" ||
                      std::string_view(argv[1]) == "--observer-cut-self-test" ||
                      std::string_view(argv[1]) == "--observer-failure-self-test" ||
                      std::string_view(argv[1]) == "--observer-nonstandard-failure-self-test")) {
        try {
            using mantis::recorder_observer::SelfTest;
            const auto option = std::string_view(argv[1]);
            const auto mode = option == "--observer-cut-self-test"       ? SelfTest::snapshot_cut
                              : option == "--observer-failure-self-test" ? SelfTest::callback_failure
                              : option == "--observer-nonstandard-failure-self-test"
                                  ? SelfTest::nonstandard_failure
                                  : SelfTest::buffer;
            return mantis::recorder_observer::self_test(argv[2], mode) ? 0 : 1;
        } catch (...) {
            return 1;
        }
    }
    if (argc < 5 || std::string_view(argv[1]) != "--observer-output" ||
        std::string_view(argv[3]) != "--observer-capture")
        return 2;
    try {
        mantis::recorder_observer::start(argv[2], std::stoull(argv[4]));
        struct Stop {
            ~Stop() {
                mantis::recorder_observer::stop();
            }
        } stop;
        argv[4] = argv[0];
        return mantis_recorder_daemon_entry(argc - 4, argv + 4);
    } catch (...) {
        // No raw exception/path exports. Primary daemon/capture errors stay in
        // their original real entrypoint and accepted acceptance evidence path.
        mantis::recorder_observer::stop();
        return 2;
    }
}
