#include <mantis/protocol.hpp>
namespace mantis::protocol {
void send(const platform::Socket &socket, const google::protobuf::MessageLite &message) {
    auto bytes = message.SerializeAsString();
    if (bytes.size() > max_control_bytes)
        fail(Status::invalid_argument, "Control message exceeds limit");
    uint32_t size = static_cast<uint32_t>(bytes.size());
    std::array<std::byte, 4> header{};
    for (int i = 0; i < 4; ++i)
        header[static_cast<size_t>(i)] = static_cast<std::byte>(size >> (24 - i * 8));
    socket.send(header);
    socket.send(std::as_bytes(std::span(bytes)));
}
void receive(const platform::Socket &socket, google::protobuf::MessageLite &message) {
    std::array<std::byte, 4> header;
    socket.receive(header);
    uint32_t size = 0;
    for (auto b : header)
        size = (size << 8) | std::to_integer<uint8_t>(b);
    if (!size || size > max_control_bytes)
        fail(Status::invalid_argument, "Invalid control message size");
    std::string bytes(size, '\0');
    socket.receive(std::as_writable_bytes(std::span(bytes)));
    if (!message.ParseFromString(bytes))
        fail(Status::invalid_argument, "Malformed Protobuf message");
}
} // namespace mantis::protocol
