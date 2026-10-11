#pragma once
#include "../../fixtures/x1-f2/link.hpp"
#include "common.hpp"
#include <exception>
#include <sys/eventfd.h>
namespace ptytest {
// The peer's real PTY ingress -> Parser -> unchanged accepted Link byte ingress.
// Controller semantics remain entirely in that fixture. Manual time and loss
// injection operate through its existing test interfaces, never native commands.
class Peer {
    int fd_, wake_;
    std::jthread thread_;
    std::mutex mutex_;
    std::condition_variable changed_;
    std::array<Wire, 256> requests_{};
    size_t count_{};
    std::exception_ptr error_;
    std::atomic<uint64_t> now_{};
    Parser parser_;
    x1::f2::simulation::Link link_{true};
    unsigned mutation_{};
    void run(std::stop_token);
    void send(Frame);

  public:
    explicit Peer(int transferred_fd);
    ~Peer();
    void advance(uint64_t us);
    void inject(x1::f2::simulation::Injection i) {
        link_.inject(i);
    }
    void mutation(unsigned m) {
        std::lock_guard lock(mutex_);
        mutation_ = m;
    }
    void check();
    bool wait(uint16_t opcode, size_t count);
    size_t request_count(uint16_t opcode);
    Wire request_at(size_t index);
    auto metrics() {
        return link_.metrics();
    }
};
} // namespace ptytest
