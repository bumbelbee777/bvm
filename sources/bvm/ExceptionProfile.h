#pragma once

#include <cstdint>

/** Hardware exception frame stack for nested interrupts (not `SYSCALL` frames). */
namespace ExceptionProfile {

constexpr uint32_t HardwareNestDepth = 3;

struct ExceptionFrame {
    uint64_t Ic = 0;
    uint64_t Fr = 0;
};

} // namespace ExceptionProfile
