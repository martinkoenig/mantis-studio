#include "../../fixtures/x1-f2-pty/backend.hpp"
#include "allocation-count.hpp"
#include <filesystem>
#include <future>
#include <numeric>
#include <sys/eventfd.h>
namespace {
std::atomic<unsigned> failed_eventfd{};
}
extern "C" int __real_eventfd(unsigned, int);
extern "C" int __wrap_eventfd(unsigned value, int flags) {
    auto remaining = failed_eventfd.load();
    if (remaining && failed_eventfd.fetch_sub(1) == 1) {
        errno = EMFILE;
        return -1;
    }
    return __real_eventfd(value, flags);
}
using namespace ptytest;
using namespace x1::f2::simulation;
using x1::f2::simulation::pty::Backend;
namespace {
size_t entries(const char *path) {
    return static_cast<size_t>(
        std::distance(std::filesystem::directory_iterator(path), std::filesystem::directory_iterator{}));
}
void retired_threads(size_t expected) {
    // pthread_join has completed before this check. Linux can still expose an
    // exiting task in /proc during final kernel bookkeeping. Require the exact
    // baseline within a finite deadline; a retained worker cannot pass this test.
    auto until = deadline();
    std::mutex mutex;
    std::condition_variable wait;
    std::unique_lock lock(mutex);
    while (entries("/proc/self/task") != expected && Clock::now() < until)
        wait.wait_until(lock, std::min(until, Clock::now() + 1ms));
    CHECK(entries("/proc/self/task") == expected);
}
void failed(std::string_view fault, bool partial) {
    Backend transport;
    Host host(transport, host_selection());
    auto snapshot = host.discover(deadline());
    CHECK(transport.fault(fault));
    auto fence = host.fence();
    try {
        host.start(snapshot, BenchConfig{}.execution(), true, deadline(), fence);
        throw std::runtime_error("Missing ambiguous-start failure");
    } catch (const ProtocolError &e) {
        CHECK(e.result == (fault == "disconnect" ? Result::unavailable : Result::timeout));
    }
    CHECK(host.fence() != fence);
    if (fault == "queued-claim")
        CHECK(!host.association().session);
    auto stopped = host.stop(deadline());
    if (stopped != (fault != "disconnect"))
        std::cerr << "Fault=" << fault << " STOP error=" << static_cast<unsigned>(host.stop_error())
                  << " peer_failed=" << transport.byte_metrics().peer_failed
                  << " peer_error=" << transport.byte_metrics().error.data()
                  << " errno=" << transport.transport_metrics().failure_errno << "\n";
    CHECK(stopped == (fault != "disconnect"));
    CHECK(host.retire_heartbeat(deadline()));
    CHECK(transport.metrics().runs == 0); // Real 60 ms retry gap exceeds the 50 ms partial-frame idle window.
    CHECK(transport.metrics().pulses == 0);
    if (fault != "disconnect") {
        CHECK(transport.byte_metrics().run_requests == 0);
        if (partial)
            CHECK(transport.byte_metrics().malformed > 0);
        CHECK(transport.transport_metrics().cancelled >= Host::max_attempts - (partial ? 1 : 0));
    } else
        CHECK(host.stop_error() == Result::unavailable);
}
void blocked() {
    Backend transport;
    Host host(transport, host_selection());
    auto snapshot = host.discover(deadline());
    Injection f;
    f.block_message = 0x13;
    transport.inject(f);
    auto run = std::async(std::launch::async, [&] {
        try {
            host.start(snapshot, BenchConfig{}.execution(), true, deadline(), host.fence());
            throw std::runtime_error("Missing blocked-RUN failure");
        } catch (const ProtocolError &e) {
            CHECK(e.result == Result::timeout || e.result == Result::bad_state);
        }
    });
    // Finite kernel/peer barrier: response receive registered after actual RUN admission.
    CHECK(transport.wait_requests(Message::run, 1, deadline()));
    CHECK(transport.metrics().runs == 1);
    host.service_heartbeat();
    CHECK(transport.wait_requests(Message::keepalive, 1, deadline()));
    auto begin = Clock::now();
    CHECK(host.stop(deadline()));
    CHECK(Clock::now() - begin < 2s);
    run.get();
    CHECK(transport.metrics().runs == 1 && host.retire_heartbeat(deadline()));
}
} // namespace
int main() {
    try {
        auto fds = entries("/proc/self/fd"), threads = entries("/proc/self/task");
        for (unsigned construction = 0; construction < 100; ++construction)
            for (unsigned failure : {1u, 2u}) {
                failed_eventfd = failure; // Peer creation, then FD worker creation.
                bool rejected = false;
                try {
                    Backend rejected_backend;
                } catch (const std::exception &) {
                    rejected = true;
                }
                CHECK(rejected && failed_eventfd == 0);
                CHECK(entries("/proc/self/fd") == fds);
                retired_threads(threads);
            }
        std::vector<int64_t> discovery, stop_latency, run_duration;
        uint64_t idle_fd_polls{}, idle_peer_polls{};
        uint64_t maximum_polls{}, tx_high{}, rx_high{}, startup_allocations{}, discovery_allocations{};
        for (unsigned cycle = 0; cycle < 12; ++cycle) {
            allocations = 0;
            measuring = !cycle;
            Backend transport;
            Host host(transport, host_selection());
            measuring = false;
            if (!cycle)
                startup_allocations = allocations.exchange(0);
            auto begin = Clock::now();
            measuring = true;
            auto snapshot = host.discover(deadline());
            measuring = false;
            discovery_allocations += allocations.exchange(0);
            discovery.push_back(
                std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - begin).count());
            auto f = Injection{};
            if (cycle % 3 == 0) {
                f.drop_message = 0x13;
                f.drops = 1;
            }
            if (cycle % 3 == 1)
                f.lose_terminal = true;
            transport.inject(f);
            CHECK(transport.fault("fragmented"));
            auto run_begin = Clock::now();
            auto association =
                host.start(snapshot, BenchConfig{}.execution(), true, deadline(), host.fence());
            std::optional<Payload> result;
            auto completion_deadline = deadline();
            do {
                result = host.terminal(completion_deadline);
            } while (!result && Clock::now() < completion_deadline);
            run_duration.push_back(
                std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - run_begin).count());
            CHECK(result && valid_terminal(result->data()));
            CHECK(get(*result, 0, 8) == association.boot && get(*result, 8, 8) == association.session &&
                  get(*result, 16, 8) == association.execution && get(*result, 24, 4) == association.arm);
            CHECK(result->bytes[28] == 1 && get(*result, 31, 4) == BenchConfig{}.pulses);
            begin = Clock::now();
            CHECK(host.stop(deadline()) && host.retire_heartbeat(deadline()));
            stop_latency.push_back(
                std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - begin).count());
            auto m = transport.metrics();
            auto bytes = transport.byte_metrics();
            CHECK(m.runs == 1 && m.pulses == 20);
            CHECK(bytes.run_requests == (cycle % 3 == 0 ? 2 : 1));
            CHECK(bytes.identical_run_retries == (cycle % 3 == 0 ? 1 : 0));
            CHECK(bytes.requests >= 14 && bytes.responses >= 13 && bytes.stop_acks > 0);
            auto tm = transport.transport_metrics();
            CHECK(!tm.malformed && !tm.response_overflow && !tm.event_overflow);
            maximum_polls = std::max(maximum_polls, tm.polls);
            tx_high = std::max(tx_high, uint64_t(tm.tx_high));
            rx_high = std::max(rx_high, uint64_t(tm.response_high));
            CHECK(host.shutdown(deadline()));
            if (!cycle) {
                // shutdown dispatches a fresh non-waiting STOP; finish its actual byte ACK
                // before measuring idle, especially with one-byte reply fragmentation.
                CHECK(transport.receive(request(Message::stop), deadline()));
                // Read-only FIFO wire barrier: all earlier ordinary/event output
                // precedes this reply, including a terminal event arriving after lookup.
                auto hello = request(Message::hello);
                CHECK(transport.submit(*encode(hello)) == TransportStatus::ready);
                auto r = transport.receive(hello, deadline());
                CHECK(r && valid_message(*r) && get(r->payload, 4, 8) == association.boot);
                bool drained = false;
                for (size_t i = 0; i < FdTransport::event_capacity + 2; ++i) {
                    auto e = transport.event(Clock::now());
                    if (e.status == TransportStatus::interrupted)
                        continue;
                    if (e.status == TransportStatus::timeout) {
                        drained = true;
                        break;
                    }
                    CHECK(e && valid_message(*e) && e->message == 0x8001 &&
                          e->session == association.session && e->payload == *result);
                }
                CHECK(drained);
                auto fd_before = transport.transport_metrics().polls;
                auto peer_before = transport.byte_metrics().polls;
                CHECK(transport.event(Clock::now() + 100ms).status == TransportStatus::timeout);
                idle_fd_polls = transport.transport_metrics().polls - fd_before;
                idle_peer_polls = transport.byte_metrics().polls - peer_before;
                CHECK(idle_fd_polls <= 4 && idle_peer_polls <= 4); // No idle polling loop.
            }
        }
        CHECK(discovery_allocations == 0);
        blocked();
        failed("congestion", false);
        failed("partial-run", true);
        failed("queued-claim", false);
        failed("disconnect", false);
        // All three independent joined workers and both PTY/event FDs retire.
        CHECK(entries("/proc/self/fd") == fds);
        retired_threads(threads);
        std::sort(discovery.begin(), discovery.end());
        std::sort(stop_latency.begin(), stop_latency.end());
        std::sort(run_duration.begin(), run_duration.end());
        std::cout << "PTY Host lifetimes=17 discovery_us[p50,p95,max]=" << discovery[6] << ','
                  << discovery[11] << ',' << discovery.back()
                  << " stop_ack_and_heartbeat_join_us[p50,p95,max]=" << stop_latency[6] << ','
                  << stop_latency[11] << ',' << stop_latency.back()
                  << " transport_polls_max=" << maximum_polls << " tx_high=" << tx_high
                  << " response_high=" << rx_high << " Backend_bytes=" << sizeof(Backend)
                  << " FD_bytes=" << sizeof(FdTransport) << " Peer_bytes=" << Backend::peer_storage_bytes()
                  << " run_us[p50,p95,max]=" << run_duration[6] << ',' << run_duration[11] << ','
                  << run_duration.back() << " idle_100ms_fd_peer_polls=" << idle_fd_polls << ','
                  << idle_peer_polls << " startup_cpp_allocations=" << startup_allocations
                  << " discovery_cpp_allocations=" << discovery_allocations
                  << " fd/thread_delta=0 image_copies=0\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
