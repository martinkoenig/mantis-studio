#pragma once
// Private compile-time sampling seam, linked only by the Linux storage
// qualification executable. Product Session has no sampler or runtime selector.
#ifdef MANTIS_SESSION_QUALIFICATION
#include <cstdint>
namespace mantis::device::qualification_test {
void admission(int64_t began, int64_t ended, bool accepted) noexcept;
}
#endif
