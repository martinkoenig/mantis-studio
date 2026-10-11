#include "common.hpp"
using namespace interop;
namespace {
struct Reference {
    std::string_view name, request, response;
};
#include "wire-fixtures.hpp"
// Supplementary immutable firmware captures, NOT a controller model. No lifecycle logic.
class CapturedTransport final : public Transport {
    const Reference &sample_;
    Parser receiver_;
    std::optional<Frame> response_;

  public:
    explicit CapturedTransport(const Reference &r) : sample_(r) {}
    TransportStatus submit(const Wire &w) override {
        if (w != unhex(sample_.request))
            return TransportStatus::invalid;
        auto bytes = unhex(sample_.response);
        for (auto b : bytes.data())
            if (auto f = receiver_.feed(b, 0))
                response_ = *f;
        return TransportStatus::ready;
    }
    FrameRead receive(const Frame &q, Deadline) override {
        if (response_ && matches_response(*response_, q)) {
            auto f = *response_;
            response_.reset();
            return {TransportStatus::ready, f};
        }
        return {TransportStatus::timeout, {}};
    }
    FrameRead event(Deadline) override {
        return {TransportStatus::timeout, {}};
    }
    void interrupt() noexcept override {
        response_.reset();
    }
};
Frame parse(const Wire &w) {
    Parser parser;
    std::optional<Frame> frame;
    // Arbitrary fragmentation is byte-oriented; no native structure translations.
    for (auto b : w.data())
        if (auto f = parser.feed(b, 0))
            frame = *f;
    CHECK(frame && *encode(*frame) == w);
    return *frame;
}
} // namespace
int main() {
    try {
        CHECK(reference_sha == firmware_sha && reference.size() <= 128);
        size_t unknown = 0, errors = 0, events = 0, empty_inventory = 0;
        for (const auto &r : reference) {
            auto bytes = unhex(r.response);
            auto expected = parse(bytes);
            if (r.request.empty()) {
                CHECK(expected.kind == Class::event && valid_message(expected) && expected.message == 0x8001);
                CHECK(get(expected.payload, 0, 8) == boot && get(expected.payload, 8, 8) == session);
                CHECK(expected.payload.bytes[35] == 1 && get(expected.payload, 31, 4) == 0 &&
                      expected.payload.bytes[49] == 2);
                ++events;
                continue;
            }
            auto q = parse(unhex(r.request));
            CHECK(matches_response(expected, q));
            auto code = static_cast<Result>(get(expected.payload, 0, 2));
            if (!request_layout(q.message)) {
                CHECK(!valid_message(expected) && expected.payload.size == 4 && code != Result::ok);
                ++unknown;
            } else
                CHECK(valid_message(expected));
            if (q.message == 0x5 && code == Result::ok) {
                CHECK(expected.payload.bytes[63] == 0);
                ++empty_inventory;
            }
            if (code != Result::ok && request_layout(q.message))
                for (size_t i = 4; i < expected.payload.size; ++i)
                    CHECK(expected.payload.bytes[i] == 0);
            CapturedTransport t(r);
            Host h(t, selection());
            bool consumed = q.session && q.request && code != Result::no_session &&
                            code != Result::stale_session && code != Result::stale_request &&
                            code != Result::conflicting_duplicate;
            if (code == Result::ok)
                CHECK(HostConformanceAccess::transaction(h, q, deadline()) == expected);
            else {
                ++errors;
                error([&] { HostConformanceAccess::transaction(h, q, deadline()); }, code,
                      request_layout(q.message)
                          ? (code != Result::no_session && code != Result::stale_session &&
                             code != Result::stale_request && code != Result::conflicting_duplicate)
                          : consumed);
            }
            // Known layouts remain strict despite actual negative prefixes in captures.
            if (request_layout(q.message) && code != Result::ok && expected.payload.size > 4) {
                auto truncated = expected;
                truncated.payload.size = 4;
                CHECK(!valid_message(truncated));
            }
        }
        CHECK(unknown >= 40 && errors >= 50 && events && empty_inventory);
        std::cout << "Firmware wire captures " << reference_sha << ": " << reference.size()
                  << " parsed exchanges; " << unknown << " unknown replies, " << errors
                  << " errors, canonical terminal and empty inventory PASS\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
