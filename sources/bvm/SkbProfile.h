#pragma once

#include "PhysMap.h"

#include <cstddef>
#include <cstdint>

/** Shared Secondary K-Bank (SKB) — Honeycomb v0.7-SKB (full socket pool). */
namespace SkbProfile {

constexpr uint64_t DomainNibble = static_cast<uint64_t>(PhysMap::Domain::LocalSkb);
constexpr uint64_t BaseAddress = PhysMap::LocalSkbBaseAddress;
constexpr std::size_t SizeBytes = PhysMap::LocalSkbByteSize;
constexpr std::size_t LineBytes = 64U;
constexpr std::size_t LineCount = SizeBytes / LineBytes;
constexpr std::size_t BankCount = 16U;
constexpr std::size_t BankByteSize = SizeBytes / BankCount;
constexpr std::size_t LinesPerBank = BankByteSize / LineBytes;

constexpr uint64_t WindowBasePhysicalAddress = BaseAddress;
constexpr uint64_t WindowByteExtent = static_cast<uint64_t>(SizeBytes);

enum class LineState : uint8_t {
    Invalid = 0,
    Shared = 1,
    Exclusive = 2,
    Modified = 3,
    Pinned = 4,
    Transition = 5
};

constexpr uint8_t OpmodeSkbGpr8 = 0;
constexpr uint8_t OpmodeSkbSsx16 = 3;
constexpr uint8_t OpmodeSkbLine64 = 7;

constexpr uint8_t BindModeShared = 0;
constexpr uint8_t BindModeExclusive = 1;
constexpr uint8_t BindModePinned = 2;

constexpr uint8_t ReduceOpSum = 0;
constexpr uint8_t ReduceOpMin = 1;
constexpr uint8_t ReduceOpMax = 2;
constexpr uint8_t ReduceOpAnd = 3;
constexpr uint8_t ReduceOpOr = 4;

} // namespace SkbProfile
