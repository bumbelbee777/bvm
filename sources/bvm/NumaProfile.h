#pragma once

#include "PhysMap.h"
#include "SkbProfile.h"

#include <cstddef>
#include <cstdint>

size_t GetRemoteKBytes();
size_t GetRemoteSkbBytes();
size_t GetRemoteDramBytes();
uint32_t GetActiveNumaNodeCount();
bool IsNumaNodeActive(uint8_t Node);

/** Simulated NUMA / remote domains for baseline single-socket `bvm`. */
namespace NumaProfile {

constexpr std::size_t MaxNodes = 4;
constexpr std::size_t RemoteKBytes = PhysMap::LocalKByteSize;
constexpr std::size_t RemoteSkbBytes = 4U * 1024U * 1024U;
constexpr std::size_t RemoteDramBytes = 4U * 1024U * 1024U;
constexpr std::size_t RemoteSkbLineBytes = SkbProfile::LineBytes;
constexpr std::size_t RemoteSkbLineCount = RemoteSkbBytes / RemoteSkbLineBytes;

constexpr uint64_t RemoteKBaseAddress = 0x1000'0000'0000'0000ULL;
constexpr uint64_t RemoteSkbBaseAddress = 0x3000'0000'0000'0000ULL;
constexpr uint64_t RemoteDramBaseAddress = 0x5000'0000'0000'0000ULL;

constexpr uint64_t NodeShift = 56;
constexpr uint64_t NodeMask = 0xFULL;

namespace HintType {
constexpr uint64_t SkbTouch = 0x60;
constexpr uint64_t SkbPin = 0x61;
constexpr uint64_t SkbUnpin = 0x62;
constexpr uint64_t SkbPrefetch = 0x63;
constexpr uint64_t SkbEvict = 0x64;
constexpr uint64_t SkbMigrate = 0x65;
constexpr uint64_t Acquire = 0x80;
constexpr uint64_t Release = 0x81;
constexpr uint64_t Fence = 0x82;
constexpr uint64_t FlushScope = 0x83;
constexpr uint64_t NumaPlace = 0x90;
constexpr uint64_t NumaAffinity = 0x91;
constexpr uint64_t NumaReplicate = 0x92;
} // namespace HintType

inline uint8_t DecodeNode(uint64_t PhysicalAddress) noexcept {
    return static_cast<uint8_t>((PhysMap::StripDomain(PhysicalAddress) >> NodeShift) & NodeMask);
}

inline uint64_t StripNodeOffset(uint64_t PhysicalAddress) noexcept {
    return PhysMap::StripDomain(PhysicalAddress) & ((1ULL << NodeShift) - 1ULL);
}

inline bool CoversRemoteK(uint64_t PhysicalAddress, std::size_t AccessBytes) noexcept {
    return PhysMap::DecodeDomain(PhysicalAddress) == PhysMap::Domain::RemoteK &&
           StripNodeOffset(PhysicalAddress) + AccessBytes <= GetRemoteKBytes();
}

inline bool CoversRemoteSkb(uint64_t PhysicalAddress, std::size_t AccessBytes) noexcept {
    return PhysMap::DecodeDomain(PhysicalAddress) == PhysMap::Domain::RemoteSkb &&
           StripNodeOffset(PhysicalAddress) + AccessBytes <= GetRemoteSkbBytes();
}

inline bool CoversRemoteDram(uint64_t PhysicalAddress, std::size_t AccessBytes) noexcept {
    return PhysMap::DecodeDomain(PhysicalAddress) == PhysMap::Domain::RemoteDram &&
           StripNodeOffset(PhysicalAddress) + AccessBytes <= GetRemoteDramBytes();
}

} // namespace NumaProfile
