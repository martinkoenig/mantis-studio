#pragma once
#include <mantis/protocol.hpp>
#include <mantis/services.hpp>
namespace mantis::protocol {
wire::v1::Response dispatch(services::Runtime &, const wire::v1::Request &);
}
