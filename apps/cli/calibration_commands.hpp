#pragma once
#include <mantis/protocol.hpp>
#include <span>
// Strict, bounded calibration CLI parser; no network/runtime ownership.
mantis::wire::v1::Request calibration_command(std::span<const std::string>);
