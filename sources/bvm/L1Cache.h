#pragma once

#include "CacheProfile.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

class L1InstructionCache {
public:
    L1InstructionCache();

    void Flush();

    bool IsCacheable(uint64_t PhysAddr) const;

    uint16_t ReadU16(uint64_t PhysAddr, bool& Miss);
    uint64_t ReadU64(uint64_t PhysAddr, bool& Miss);

    void InvalidateLine(uint64_t PhysAddr);

    uint64_t AccessCount() const { return TotalAccesses; }
    uint64_t MissCount() const { return TotalMisses; }

private:
    struct Line {
        bool Valid = false;
        uint64_t Tag = 0;
        std::array<uint8_t, CacheProfile::LineBytes> Bytes{};
    };

    static constexpr size_t SetCount = CacheProfile::InstructionSetCount;
    static constexpr size_t LineBytes = CacheProfile::LineBytes;

    std::array<Line, SetCount> Sets{};

    uint64_t TotalAccesses = 0;
    uint64_t TotalMisses = 0;

    static uint64_t LineNumber(uint64_t PhysAddr) { return PhysAddr >> 6; }
    static uint64_t LineBase(uint64_t PhysAddr) { return PhysAddr & ~0x3FULL; }
    static size_t IndexFor(uint64_t PhysAddr) {
        return static_cast<size_t>(LineNumber(PhysAddr) % SetCount);
    }
    static uint64_t TagFor(uint64_t PhysAddr) { return LineNumber(PhysAddr) / SetCount; }

    Line& Locate(uint64_t PhysAddr, uint64_t& TagOut);
    void FillLine(Line& LineState, uint64_t PhysAddr);
    uint64_t ReadPacked(const Line& LineState, uint64_t PhysAddr, size_t WidthBytes) const;
};

class L1DataCache {
public:
    L1DataCache();

    void Flush();

    bool IsCacheable(uint64_t PhysAddr) const;

    uint8_t ReadU8(uint64_t PhysAddr, bool& Miss);
    uint16_t ReadU16(uint64_t PhysAddr, bool& Miss);
    uint32_t ReadU32(uint64_t PhysAddr, bool& Miss);
    uint64_t ReadU64(uint64_t PhysAddr, bool& Miss);

    void WriteU8(uint64_t PhysAddr, uint8_t Value);
    void WriteU16(uint64_t PhysAddr, uint16_t Value);
    void WriteU32(uint64_t PhysAddr, uint32_t Value);
    void WriteU64(uint64_t PhysAddr, uint64_t Value);

    void PrefetchLine(uint64_t PhysAddr);

    uint64_t AccessCount() const { return TotalAccesses; }
    uint64_t MissCount() const { return TotalMisses; }

private:
    struct Line {
        bool Valid = false;
        uint64_t Tag = 0;
        std::array<uint8_t, CacheProfile::LineBytes> Bytes{};
    };

    static constexpr size_t SetCount = CacheProfile::DataSetCount;
    static constexpr size_t LineBytes = CacheProfile::LineBytes;

    std::array<Line, SetCount> Sets{};

    uint64_t TotalAccesses = 0;
    uint64_t TotalMisses = 0;

    static uint64_t LineNumber(uint64_t PhysAddr) { return PhysAddr >> 6; }
    static uint64_t LineBase(uint64_t PhysAddr) { return PhysAddr & ~0x3FULL; }
    static size_t IndexFor(uint64_t PhysAddr) {
        return static_cast<size_t>(LineNumber(PhysAddr) % SetCount);
    }
    static uint64_t TagFor(uint64_t PhysAddr) { return LineNumber(PhysAddr) / SetCount; }

    Line& Locate(uint64_t PhysAddr, uint64_t& TagOut);
    void FillLine(Line& LineState, uint64_t PhysAddr);
    void WriteRamByte(uint64_t PhysAddr, uint8_t Value) const;
    uint64_t ReadPacked(const Line& LineState, uint64_t PhysAddr, size_t WidthBytes) const;
    void UpdateCachedByte(Line& LineState, uint64_t PhysAddr, uint8_t Value);
};
