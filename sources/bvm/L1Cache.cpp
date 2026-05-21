#include "L1Cache.h"

#include "Machine.h"
#include "PhysMap.h"

namespace {

uint8_t ReadUncachedRamByte(uint64_t PhysAddr) {
    const uint64_t Offset = PhysMap::LocalDramOffset(PhysAddr);
    return Ram[static_cast<size_t>(Offset)];
}

void WriteUncachedRamByte(uint64_t PhysAddr, uint8_t Value) {
    const uint64_t Offset = PhysMap::LocalDramOffset(PhysAddr);
    Ram[static_cast<size_t>(Offset)] = Value;
}

bool CoversCacheableDram(uint64_t PhysAddr, size_t Bytes) {
    return PhysMap::CoversLocalDram(PhysAddr, Bytes);
}

} // namespace

L1InstructionCache::L1InstructionCache() { Flush(); }

void L1InstructionCache::Flush() {
    for (Line& Entry : Sets) {
        Entry.Valid = false;
        Entry.Tag = 0;
        Entry.Bytes.fill(0);
    }
    TotalAccesses = 0;
    TotalMisses = 0;
}

bool L1InstructionCache::IsCacheable(uint64_t PhysAddr) const {
    return CoversCacheableDram(PhysAddr, 1);
}

L1InstructionCache::Line& L1InstructionCache::Locate(uint64_t PhysAddr, uint64_t& TagOut) {
    TagOut = TagFor(PhysAddr);
    return Sets[IndexFor(PhysAddr)];
}

void L1InstructionCache::FillLine(Line& LineState, uint64_t PhysAddr) {
    const uint64_t Base = LineBase(PhysAddr);
    for (size_t Index = 0; Index < LineBytes; ++Index) {
        LineState.Bytes[Index] = ReadUncachedRamByte(Base + Index);
    }
    LineState.Valid = true;
    LineState.Tag = TagFor(PhysAddr);
}

uint64_t L1InstructionCache::ReadPacked(const Line& LineState, uint64_t PhysAddr,
                                        size_t WidthBytes) const {
    const size_t Offset = static_cast<size_t>(PhysAddr & (LineBytes - 1));
    uint64_t Value = 0;
    for (size_t Index = 0; Index < WidthBytes; ++Index) {
        Value = (Value << 8) | LineState.Bytes[Offset + Index];
    }
    return Value;
}

uint16_t L1InstructionCache::ReadU16(uint64_t PhysAddr, bool& Miss) {
    Miss = false;
    ++TotalAccesses;
    if (!IsCacheable(PhysAddr)) {
        Miss = true;
        ++TotalMisses;
        return static_cast<uint16_t>((static_cast<uint16_t>(ReadUncachedRamByte(PhysAddr)) << 8) |
                                     ReadUncachedRamByte(PhysAddr + 1));
    }

    uint64_t Tag = 0;
    Line& Entry = Locate(PhysAddr, Tag);
    if (!Entry.Valid || Entry.Tag != Tag) {
        Miss = true;
        ++TotalMisses;
        FillLine(Entry, PhysAddr);
    }
    return static_cast<uint16_t>(ReadPacked(Entry, PhysAddr, 2));
}

uint64_t L1InstructionCache::ReadU64(uint64_t PhysAddr, bool& Miss) {
    Miss = false;
    ++TotalAccesses;
    if (!IsCacheable(PhysAddr)) {
        Miss = true;
        ++TotalMisses;
        return (static_cast<uint64_t>(ReadUncachedRamByte(PhysAddr)) << 56) |
               (static_cast<uint64_t>(ReadUncachedRamByte(PhysAddr + 1)) << 48) |
               (static_cast<uint64_t>(ReadUncachedRamByte(PhysAddr + 2)) << 40) |
               (static_cast<uint64_t>(ReadUncachedRamByte(PhysAddr + 3)) << 32) |
               (static_cast<uint64_t>(ReadUncachedRamByte(PhysAddr + 4)) << 24) |
               (static_cast<uint64_t>(ReadUncachedRamByte(PhysAddr + 5)) << 16) |
               (static_cast<uint64_t>(ReadUncachedRamByte(PhysAddr + 6)) << 8) |
               ReadUncachedRamByte(PhysAddr + 7);
    }

    uint64_t Tag = 0;
    Line& Entry = Locate(PhysAddr, Tag);
    if (!Entry.Valid || Entry.Tag != Tag) {
        Miss = true;
        ++TotalMisses;
        FillLine(Entry, PhysAddr);
    }
    return ReadPacked(Entry, PhysAddr, 8);
}

void L1InstructionCache::InvalidateLine(uint64_t PhysAddr) {
    if (!IsCacheable(PhysAddr)) {
        return;
    }
    Line& Entry = Sets[IndexFor(PhysAddr)];
    if (Entry.Valid && Entry.Tag == TagFor(PhysAddr)) {
        Entry.Valid = false;
    }
}

L1DataCache::L1DataCache() { Flush(); }

void L1DataCache::Flush() {
    for (Line& Entry : Sets) {
        Entry.Valid = false;
        Entry.Tag = 0;
        Entry.Bytes.fill(0);
    }
    TotalAccesses = 0;
    TotalMisses = 0;
}

bool L1DataCache::IsCacheable(uint64_t PhysAddr) const {
    return CoversCacheableDram(PhysAddr, 1);
}

L1DataCache::Line& L1DataCache::Locate(uint64_t PhysAddr, uint64_t& TagOut) {
    TagOut = TagFor(PhysAddr);
    return Sets[IndexFor(PhysAddr)];
}

void L1DataCache::FillLine(Line& LineState, uint64_t PhysAddr) {
    const uint64_t Base = LineBase(PhysAddr);
    for (size_t Index = 0; Index < LineBytes; ++Index) {
        LineState.Bytes[Index] = ReadUncachedRamByte(Base + Index);
    }
    LineState.Valid = true;
    LineState.Tag = TagFor(PhysAddr);
}

void L1DataCache::WriteRamByte(uint64_t PhysAddr, uint8_t Value) const {
    WriteUncachedRamByte(PhysAddr, Value);
}

uint64_t L1DataCache::ReadPacked(const Line& LineState, uint64_t PhysAddr,
                                 size_t WidthBytes) const {
    const size_t Offset = static_cast<size_t>(PhysAddr & (LineBytes - 1));
    uint64_t Value = 0;
    for (size_t Index = 0; Index < WidthBytes; ++Index) {
        Value = (Value << 8) | LineState.Bytes[Offset + Index];
    }
    return Value;
}

void L1DataCache::UpdateCachedByte(Line& LineState, uint64_t PhysAddr, uint8_t Value) {
    if (!LineState.Valid || LineState.Tag != TagFor(PhysAddr)) {
        return;
    }
    LineState.Bytes[static_cast<size_t>(PhysAddr & (LineBytes - 1))] = Value;
}

uint8_t L1DataCache::ReadU8(uint64_t PhysAddr, bool& Miss) {
    Miss = false;
    ++TotalAccesses;
    if (!IsCacheable(PhysAddr)) {
        Miss = true;
        ++TotalMisses;
        return ReadUncachedRamByte(PhysAddr);
    }

    uint64_t Tag = 0;
    Line& Entry = Locate(PhysAddr, Tag);
    if (!Entry.Valid || Entry.Tag != Tag) {
        Miss = true;
        ++TotalMisses;
        FillLine(Entry, PhysAddr);
    }
    return Entry.Bytes[static_cast<size_t>(PhysAddr & (LineBytes - 1))];
}

uint16_t L1DataCache::ReadU16(uint64_t PhysAddr, bool& Miss) {
    Miss = false;
    ++TotalAccesses;
    if (!IsCacheable(PhysAddr)) {
        Miss = true;
        ++TotalMisses;
        return static_cast<uint16_t>((static_cast<uint16_t>(ReadUncachedRamByte(PhysAddr)) << 8) |
                                     ReadUncachedRamByte(PhysAddr + 1));
    }

    uint64_t Tag = 0;
    Line& Entry = Locate(PhysAddr, Tag);
    if (!Entry.Valid || Entry.Tag != Tag) {
        Miss = true;
        ++TotalMisses;
        FillLine(Entry, PhysAddr);
    }
    return static_cast<uint16_t>(ReadPacked(Entry, PhysAddr, 2));
}

uint32_t L1DataCache::ReadU32(uint64_t PhysAddr, bool& Miss) {
    Miss = false;
    ++TotalAccesses;
    if (!IsCacheable(PhysAddr)) {
        Miss = true;
        ++TotalMisses;
        return (static_cast<uint32_t>(ReadUncachedRamByte(PhysAddr)) << 24) |
               (static_cast<uint32_t>(ReadUncachedRamByte(PhysAddr + 1)) << 16) |
               (static_cast<uint32_t>(ReadUncachedRamByte(PhysAddr + 2)) << 8) |
               ReadUncachedRamByte(PhysAddr + 3);
    }

    uint64_t Tag = 0;
    Line& Entry = Locate(PhysAddr, Tag);
    if (!Entry.Valid || Entry.Tag != Tag) {
        Miss = true;
        ++TotalMisses;
        FillLine(Entry, PhysAddr);
    }
    return static_cast<uint32_t>(ReadPacked(Entry, PhysAddr, 4));
}

uint64_t L1DataCache::ReadU64(uint64_t PhysAddr, bool& Miss) {
    Miss = false;
    ++TotalAccesses;
    if (!IsCacheable(PhysAddr)) {
        Miss = true;
        ++TotalMisses;
        return (static_cast<uint64_t>(ReadUncachedRamByte(PhysAddr)) << 56) |
               (static_cast<uint64_t>(ReadUncachedRamByte(PhysAddr + 1)) << 48) |
               (static_cast<uint64_t>(ReadUncachedRamByte(PhysAddr + 2)) << 40) |
               (static_cast<uint64_t>(ReadUncachedRamByte(PhysAddr + 3)) << 32) |
               (static_cast<uint64_t>(ReadUncachedRamByte(PhysAddr + 4)) << 24) |
               (static_cast<uint64_t>(ReadUncachedRamByte(PhysAddr + 5)) << 16) |
               (static_cast<uint64_t>(ReadUncachedRamByte(PhysAddr + 6)) << 8) |
               ReadUncachedRamByte(PhysAddr + 7);
    }

    uint64_t Tag = 0;
    Line& Entry = Locate(PhysAddr, Tag);
    if (!Entry.Valid || Entry.Tag != Tag) {
        Miss = true;
        ++TotalMisses;
        FillLine(Entry, PhysAddr);
    }
    return ReadPacked(Entry, PhysAddr, 8);
}

void L1DataCache::WriteU8(uint64_t PhysAddr, uint8_t Value) {
    WriteRamByte(PhysAddr, Value);
    if (!IsCacheable(PhysAddr)) {
        return;
    }
    Line& Entry = Sets[IndexFor(PhysAddr)];
    UpdateCachedByte(Entry, PhysAddr, Value);
}

void L1DataCache::WriteU16(uint64_t PhysAddr, uint16_t Value) {
    WriteU8(PhysAddr, static_cast<uint8_t>(Value >> 8));
    WriteU8(PhysAddr + 1, static_cast<uint8_t>(Value));
}

void L1DataCache::WriteU32(uint64_t PhysAddr, uint32_t Value) {
    WriteU16(PhysAddr, static_cast<uint16_t>(Value >> 16));
    WriteU16(PhysAddr + 2, static_cast<uint16_t>(Value));
}

void L1DataCache::WriteU64(uint64_t PhysAddr, uint64_t Value) {
    WriteU32(PhysAddr, static_cast<uint32_t>(Value >> 32));
    WriteU32(PhysAddr + 4, static_cast<uint32_t>(Value));
}

void L1DataCache::PrefetchLine(uint64_t PhysAddr) {
    if (!IsCacheable(PhysAddr)) {
        return;
    }
    Line& Entry = Sets[IndexFor(PhysAddr)];
    const uint64_t Tag = TagFor(PhysAddr);
    if (Entry.Valid && Entry.Tag == Tag) {
        return;
    }
    FillLine(Entry, PhysAddr);
}
