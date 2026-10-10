#pragma once
#include <cstdint>

namespace x1::f2 {
// No PhysicalQualified policy/backend exists. Extending this private policy to
// physical operation requires a separate reviewed qualification/integration gate.
enum class ExecutionMode { simulation_only };
struct Selection {
    uint32_t controller_id{}, board_revision{};
    uint64_t channel_uid{};
    uint8_t channel_index{};
    ExecutionMode mode = ExecutionMode::simulation_only;
    uint8_t calibration_scope = 1;
    uint16_t calibration_provenance{};
    bool operator==(const Selection &) const = default;
    bool valid() const {
        return controller_id && board_revision && channel_uid && mode == ExecutionMode::simulation_only &&
               calibration_scope == 1 && calibration_provenance;
    }
};
// Explicit selected finite operation, not a controller's advertised limits or a
// synthetic calibration credential. Capability/Studio bounds are checked later.
struct FiniteExecution {
    uint32_t current_ua{}, period_us{}, high_us{}, pulses{};
    bool valid() const {
        return current_ua && period_us && high_us && high_us < period_us && pulses;
    }
    uint64_t duration_us() const {
        // Both operands are u32; their full product fits u64 without wrap.
        return uint64_t(period_us) * pulses;
    }
};
} // namespace x1::f2
