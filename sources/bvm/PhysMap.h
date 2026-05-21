#pragma once

#include "MmioMap.h"

#include <cstddef>
#include <cstdint>

size_t GetRamSize();
size_t GetSkbByteSize();

/**
 * Honeycomb v0.7-SKB unified physical address map ([63:60] domain decode).
 * Baseline BVM simulates remote domains via `Numa.cpp`; legacy flat DRAM is kept
 * for domain 0 offsets below RamSize (pre-v0.7-SKB binaries and examples).
 */
namespace PhysMap {

enum class Domain : uint8_t {
    LocalK = 0,
    RemoteK = 1,
    LocalSkb = 2,
    RemoteSkb = 3,
    LocalDram = 4,
    RemoteDram = 5,
};

constexpr uint64_t DomainShift = 60;
constexpr uint64_t DomainMask = 0xFULL;

constexpr uint64_t LocalKBaseAddress = 0x0000'0000'0000'0000ULL;
/** 256 KB K-Bank: 16,384 token slots × 16 B (DAX); legacy ops use the low 8 B data field. */
constexpr std::size_t LocalKByteSize = 256U * 1024U;
constexpr std::size_t LocalKSlotCount = 16384U;
constexpr std::size_t LocalKBytesPerSlot = 16U;
constexpr std::size_t LocalKRegisterCount = LocalKSlotCount;

constexpr uint64_t LocalSkbBaseAddress = 0x2000'0000'0000'0000ULL;
constexpr std::size_t LocalSkbByteSize = 32U * 1024U * 1024U;

constexpr uint64_t LocalDramBaseAddress = 0x4000'0000'0000'0000ULL;

inline Domain DecodeDomain(uint64_t PhysicalAddress) noexcept {
    return static_cast<Domain>((PhysicalAddress >> DomainShift) & DomainMask);
}

inline uint64_t StripDomain(uint64_t PhysicalAddress) noexcept {
    return PhysicalAddress & ((1ULL << DomainShift) - 1ULL);
}

inline bool IsLegacyFlatDramAddress(uint64_t PhysicalAddress) noexcept {
    return DecodeDomain(PhysicalAddress) == Domain::LocalK &&
           PhysicalAddress < static_cast<uint64_t>(GetRamSize());
}

inline bool CoversLocalK(uint64_t PhysicalAddress, std::size_t AccessBytes) noexcept {
    if (DecodeDomain(PhysicalAddress) != Domain::LocalK || IsLegacyFlatDramAddress(PhysicalAddress)) {
        return false;
    }
    const uint64_t Offset = StripDomain(PhysicalAddress);
    return Offset + AccessBytes <= LocalKByteSize;
}

inline bool CoversLocalSkb(uint64_t PhysicalAddress, std::size_t AccessBytes) noexcept {
    if (DecodeDomain(PhysicalAddress) != Domain::LocalSkb) {
        return false;
    }
    const uint64_t Offset = PhysicalAddress - LocalSkbBaseAddress;
    return Offset + AccessBytes <= GetSkbByteSize();
}

inline bool CoversLocalDram(uint64_t PhysicalAddress, std::size_t AccessBytes) noexcept {
    if (DecodeDomain(PhysicalAddress) == Domain::LocalDram) {
        const uint64_t Offset = StripDomain(PhysicalAddress);
        return Offset + AccessBytes <= static_cast<uint64_t>(GetRamSize());
    }
    return IsLegacyFlatDramAddress(PhysicalAddress) &&
           PhysicalAddress + AccessBytes <= static_cast<uint64_t>(GetRamSize());
}

inline uint64_t LocalDramOffset(uint64_t PhysicalAddress) noexcept {
    if (DecodeDomain(PhysicalAddress) == Domain::LocalDram) {
        return StripDomain(PhysicalAddress);
    }
    return PhysicalAddress;
}

inline uint64_t LocalKSlotIndex(uint64_t PhysicalAddress) noexcept {
    return StripDomain(PhysicalAddress) / LocalKBytesPerSlot;
}

inline uint64_t LocalKByteInSlot(uint64_t PhysicalAddress) noexcept {
    return StripDomain(PhysicalAddress) % LocalKBytesPerSlot;
}

inline uint64_t LocalKRegisterIndex(uint64_t PhysicalAddress) noexcept {
    return LocalKSlotIndex(PhysicalAddress);
}

inline uint64_t LocalSkbLineIndex(uint64_t PhysicalAddress) noexcept {
    return (PhysicalAddress - LocalSkbBaseAddress) / 64U;
}

inline uint64_t LocalSkbByteInLine(uint64_t PhysicalAddress) noexcept {
    return (PhysicalAddress - LocalSkbBaseAddress) % 64U;
}

} // namespace PhysMap
