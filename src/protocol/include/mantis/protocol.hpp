#pragma once
#include <mantis.pb.h>
#include <mantis/platform.hpp>
namespace mantis::protocol {
inline constexpr uint32_t version = 1;
inline constexpr uint32_t max_control_bytes = 4 * 1024 * 1024;
void send(const platform::Socket &, const google::protobuf::MessageLite &);
void receive(const platform::Socket &, google::protobuf::MessageLite &);
} // namespace mantis::protocol
