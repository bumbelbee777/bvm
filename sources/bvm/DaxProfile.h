#pragma once

#include "KBankProfile.h"

#include <cstddef>
#include <cstdint>

/** DAX (DAtaflow eXtensions) — token metadata over K-Bank slots (Honeycomb v0.7-DAX). */
namespace DaxProfile {

constexpr std::size_t SlotCount = KBankProfile::RegisterCount;
constexpr std::size_t BytesPerSlot = 16;
constexpr std::size_t ByteSize = SlotCount * BytesPerSlot;

/** Comparison sub-op in `disp[2:0]` for `DAX_CMP` / `DAX_CMPI`. */
enum class CompareRel : uint8_t {
    Eq = 0,
    Ne = 1,
    Lt = 2,
    Gt = 3,
    Le = 4,
    Ge = 5,
};

/** Tree reduction sub-op in `disp[2:0]` for `DAX_REDUCE` / `DAX_REDUCE_SCATTER`. */
enum class ReduceOp : uint8_t {
    Sum = 0,
    Min = 1,
    Max = 2,
};

/** Atomic RMW sub-op in `disp[2:0]` for `DAX_ATOMIC`. */
enum class AtomicOp : uint8_t {
    Add = 0,
    Swap = 1,
    And = 2,
    Or = 3,
    Xor = 4,
};

/** Prefix scan sub-op in `disp[2:0]` for `DAX_SCAN`. */
enum class ScanOp : uint8_t {
    Sum = 0,
    Min = 1,
    Max = 2,
};

constexpr unsigned MaxSimulatedCores = 8;
constexpr unsigned MaxStreamTokens = 64;
constexpr unsigned MaxVectorLength = 16;

} // namespace DaxProfile
