#pragma once

#include <cstdint>

/**
 * Baseline Honeycomb paging: 4-level tables (512 × 8 B), 4 KiB / 2 MiB / 1 GiB pages.
 * `PTB` holds the physical address of the root table. Enable with `EP`; disable with `DP`.
 */
namespace PagingProfile {

constexpr uint32_t TlbCapacity = 64;
constexpr uint8_t PageFaultVector = 14;
constexpr uint8_t TlbMissVector = 15;

constexpr uint64_t IndexBits = 9;
constexpr uint64_t IndexMask = (1ULL << IndexBits) - 1ULL;
constexpr uint64_t TableBytes = (1ULL << IndexBits) * 8ULL;

constexpr uint64_t PageShift4K = 12;
constexpr uint64_t PageShift2M = 21;
constexpr uint64_t PageShift1G = 30;

constexpr uint64_t PageOffset4KMask = (1ULL << PageShift4K) - 1ULL;
constexpr uint64_t PageOffset2MMask = (1ULL << PageShift2M) - 1ULL;
constexpr uint64_t PageOffset1GMask = (1ULL << PageShift1G) - 1ULL;

namespace PteFlags {
constexpr uint64_t Present = 1ULL << 0;
constexpr uint64_t Read = 1ULL << 1;
constexpr uint64_t Write = 1ULL << 2;
constexpr uint64_t Execute = 1ULL << 3;
constexpr uint64_t User = 1ULL << 4;
constexpr uint64_t Global = 1ULL << 5;
constexpr uint64_t Huge = 1ULL << 6;
constexpr uint64_t PfnMask = 0xFFFFFFFFFFFFF000ULL;
constexpr uint64_t AccessMask = Present | Read | Write | Execute;
} // namespace PteFlags

enum class MemoryAccessKind : uint8_t {
    Read,
    Write,
    Execute,
};

} // namespace PagingProfile
