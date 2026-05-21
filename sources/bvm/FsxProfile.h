#pragma once

#include <cstdint>

/** Baseline FSX: native FP64 in the 64-bit container (`opmode` 001). */
struct FsxBaselineProfile {
    static constexpr uint32_t RegisterCount = 16;
    static constexpr uint32_t ContainerBits = 64;
};

