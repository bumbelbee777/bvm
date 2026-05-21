#pragma once

#include "PhysMap.h"

#include <cstddef>
#include <cstdint>

/** Per-core K-Bank — Honeycomb v0.7-DAX (256 KB token slots in baseline BVM). */
namespace KBankProfile {

constexpr std::size_t RegisterCount = PhysMap::LocalKRegisterCount;
constexpr std::size_t ByteSize = PhysMap::LocalKByteSize;
constexpr uint64_t BaseAddress = PhysMap::LocalKBaseAddress;
constexpr uint64_t DefaultKMask = static_cast<uint64_t>(RegisterCount - 1U);
constexpr uint64_t DefaultWinSz = 32;

/** Low 16 bits of `WINSZ`: K-Bank window slot count. */
constexpr uint64_t WinSzWindowMask = 0xFFFFULL;

inline uint64_t EffectiveWindowSize(uint64_t WinSz) noexcept {
    const uint64_t Low = WinSz & WinSzWindowMask;
    return Low != 0 ? Low : DefaultWinSz;
}

} // namespace KBankProfile
