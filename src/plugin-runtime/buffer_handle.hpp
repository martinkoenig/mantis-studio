#pragma once
#include <mantis/memory.hpp>
#include <mantis/plugin.h>

namespace mantis::plugins::detail {
// Internal host handle: shares immutable storage, including slice ownership.
// The returned reference is released through the host API; never public C++ data.
MantisBuffer *wrap_buffer(memory::Buffer);
} // namespace mantis::plugins::detail
