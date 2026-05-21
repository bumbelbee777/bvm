#pragma once

#include <cstdint>

/**
 * Baseline BVM SSX profile: 128-bit logical registers, short vector length for
 * preliminary lane-wise FP32 ops (see docs/Honeycomb.md SSX sections).
 */
struct SsxBaselineProfile {
    static constexpr uint32_t RegisterCount = 16;
    static constexpr uint32_t LogicalRegisterBits = 128;
    static constexpr uint32_t FixedPackByteCount = 16;
    static constexpr uint32_t DefaultVectorLength = 2;
    static constexpr uint32_t DefaultElementSizeBytes = 4;
};

