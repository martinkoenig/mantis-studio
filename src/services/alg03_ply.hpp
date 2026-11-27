#pragma once

#include <mantis/artifact_store.hpp>

namespace mantis::services::detail {
// Integer-only IEEE 754 round-to-nearest, ties-to-even narrowing.
uint32_t binary32_rne_bits(uint64_t binary64_bits);

// Export-only read side. Never registers or persists the temporary PointCloud.
data::Published alg03_ply_input(const artifact::Store &, const Id &, const CancellationToken &);
} // namespace mantis::services::detail
