#pragma once

#include <cstddef>
#include <cstdint>

/** Modest L1 parameters — direct-mapped, no speculation, no HW prefetch. */
namespace CacheProfile {

constexpr size_t LineBytes = 64;

constexpr size_t InstructionCacheBytes = 32U * 1024U;
constexpr size_t DataCacheBytes = 48U * 1024U;

constexpr size_t InstructionSetCount = InstructionCacheBytes / LineBytes; // 512
constexpr size_t DataSetCount = DataCacheBytes / LineBytes;               // 768

/** Extra cycle cost charged on an L1 miss (fill latency heuristic). */
constexpr uint32_t InstructionMissPenalty = 8;
constexpr uint32_t DataMissPenalty = 12;

} // namespace CacheProfile
