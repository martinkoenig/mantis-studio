#include <array>
#include <cstdio>
#include <cstring>
#include <mantis/sdk.hpp>
#include <new>

// Private test-only host storage; no runtime/plugin-host implementation is linked.
struct MantisBuffer {
    std::array<std::byte, 8> data{};
    unsigned refs{1};
    bool published{};
};
namespace {
unsigned allocated{}, freed{}, retained{}, released{};
bool fail_allocate{};
void require(bool condition, const char *diagnosis) {
    if (!condition)
        throw std::runtime_error(diagnosis);
}
MantisBuffer *allocate(uint64_t bytes, uint64_t alignment) noexcept {
    if (fail_allocate || bytes != 8 || alignment != 64)
        return nullptr;
    auto *b = new (std::nothrow) MantisBuffer;
    if (b)
        ++allocated;
    return b;
}
void retain(MantisBuffer *b) noexcept {
    ++b->refs;
    ++retained;
}
void release(MantisBuffer *b) noexcept {
    ++released;
    if (--b->refs == 0) {
        ++freed;
        delete b;
    }
}
int write_map(MantisBuffer *b, void **data, uint64_t *size) noexcept {
    if (b->published)
        return 1;
    *data = b->data.data();
    *size = b->data.size();
    return 0;
}
int read_map(const MantisBuffer *b, const void **data, uint64_t *size) noexcept {
    *data = b->data.data();
    *size = b->data.size();
    return 0;
}
int publish(MantisBuffer *b) noexcept {
    b->published = true;
    return 0;
}
const MantisHostV1 host{
    sizeof(MantisHostV1), 1, allocate, retain, release, write_map, read_map, publish, nullptr};
MantisProcessorV2 processor{sizeof(MantisProcessorV2), 1, nullptr, nullptr};
const void *query(const char *id) noexcept {
    return std::strcmp(id, MANTIS_PROCESSOR_V2) == 0 ? &processor : nullptr;
}
void contracts() {
    using namespace mantis::sdk;
    static_assert(!std::is_copy_constructible_v<Buffer>);
    static_assert(std::is_nothrow_move_constructible_v<Buffer>);
    static_assert(!std::is_copy_constructible_v<RetainedBuffer>);
    static_assert(noexcept(boundary([] {})));
    require(compatible(&host) && !compatible(nullptr), "host compatibility positive/null");
    auto invalid_host = host;
    invalid_host.struct_size = sizeof(host) - 1;
    require(!compatible(&invalid_host), "short host accepted");
    invalid_host = host;
    invalid_host.abi_version = 2;
    require(!compatible(&invalid_host), "unsupported host ABI accepted");
    MantisPluginV1 root{sizeof(MantisPluginV1), 1, "fixture", "0", nullptr, nullptr, query};
    require(query_optional<MantisProcessorV2>(&root, "absent") == nullptr, "optional absent query");
    processor.abi_version = 2;
    require(!compatible_table(&processor), "unsupported queried ABI accepted");
    require(boundary([&] { (void)processor_v2(&root); }) == 1, "unsupported queried ABI accepted");
    processor.abi_version = 1;
    processor.struct_size = sizeof(processor) - 1;
    require(boundary([&] { (void)processor_v2(&root); }) == 1, "short queried table accepted");
    processor.struct_size = sizeof(processor);
    require(boundary([&] { (void)processor_v2(&root); }) == 1, "missing queried callbacks accepted");
    bool invoked = false;
    auto callback = [&](const MantisSemanticPacketV1 &) {
        invoked = true;
        throw std::runtime_error("callback fault");
    };
    MantisSemanticPacketV1 view{};
    view.struct_size = sizeof(view);
    view.abi_version = 1;
    require(borrowed_callback<MantisSemanticPacketV1, decltype(callback)>(&callback, &view) == 1 && invoked,
            "callback exception escaped or callback not exercised");
    invoked = false;
    view.struct_size = sizeof(view) - 1;
    require(borrowed_callback<MantisSemanticPacketV1, decltype(callback)>(&callback, &view) == 1 && !invoked,
            "short borrowed view reached callback");
    view.struct_size = sizeof(view);
    view.abi_version = 2;
    require(borrowed_callback<MantisSemanticPacketV1, decltype(callback)>(&callback, &view) == 1 && !invoked,
            "unsupported borrowed ABI reached callback");
    require(boundary([] {}) == 0 && boundary([] { throw 42; }) == 1, "boundary containment failed");
    {
        Buffer owner(&host, 8);
        owner.writable()[3] = std::byte{0x5a};
        Buffer moved(std::move(owner));
        require(owner.get() == nullptr, "moved Buffer still owns reference");
        moved.publish();
        require(boundary([&] { (void)moved.writable(); }) == 1, "published write not rejected");
        MantisAttributeV1 attribute{};
        attribute.buffer = moved.get();
        attribute.offset = 3;
        attribute.bytes = 1;
        require(read(&host, attribute)[0] == std::byte{0x5a}, "mapped payload changed");
        attribute.offset = 8;
        attribute.bytes = 1;
        require(boundary([&] { (void)read(&host, attribute); }) == 1, "out-of-range read accepted");
        RetainedBuffer retained_owner(&host, moved.get());
        RetainedBuffer retained_moved(std::move(retained_owner));
        require(!retained_owner.get() && retained_moved.get() == moved.get(), "retained move");
        require(boundary([&] { RetainedBuffer bad(&host, nullptr); }) == 1, "null retained buffer accepted");
        require(retained == 1 && freed == 0, "retain ownership accounting");
    }
    require(allocated == 1 && freed == 1 && released == 2, "RAII release imbalance");
    fail_allocate = true;
    bool bad_alloc = false;
    try {
        Buffer unavailable(&host, 8);
    } catch (const std::bad_alloc &) {
        bad_alloc = true;
    }
    require(bad_alloc && allocated == 1 && freed == 1 && released == 2,
            "allocation failure changed ownership");
    fail_allocate = false;
    require(boundary([&] {
                Buffer unwind(&host, 8);
                throw std::runtime_error("work failed");
            }) == 1,
            "ownership unwind boundary");
    require(allocated == 2 && freed == 2 && released == 3, "exception unwind leaked buffer");
}
} // namespace
int main() {
    try {
        contracts();
        std::puts("C++ installed wrapper ABI/size/exception/ownership contracts passed");
        return 0;
    } catch (const std::exception &e) {
        std::fprintf(stderr, "SDK contract failed: %s\n", e.what());
        return 1;
    }
}
