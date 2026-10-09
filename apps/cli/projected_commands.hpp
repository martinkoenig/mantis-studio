#pragma once
#include <mantis/protocol.hpp>
#include <span>
mantis::wire::v1::Request projected_command(std::span<const std::string>);
