#pragma once

#include "Machine.h"

#include <cstring>
#include <type_traits>

/** Bounds-checked guest DRAM access helpers shared by MMIO devices. */
namespace GuestMem {

inline bool RegionValid(uint64_t Addr, size_t Bytes) {
    if (Bytes == 0) {
        return true;
    }
    return Addr <= GetRamSize() && Addr + Bytes <= GetRamSize();
}

bool CopyRegion(uint64_t Dst, uint64_t Src, size_t Bytes);
bool FillRegion(uint64_t Dst, uint8_t Byte, size_t Bytes);

inline bool ReadBytes(uint64_t Addr, void* Dest, size_t Bytes) {
    if (Dest == nullptr || !RegionValid(Addr, Bytes)) {
        return false;
    }
    std::memcpy(Dest, Ram.data() + static_cast<size_t>(Addr), Bytes);
    return true;
}

inline bool WriteBytes(uint64_t Addr, const void* Src, size_t Bytes) {
    if (Src == nullptr || !RegionValid(Addr, Bytes)) {
        return false;
    }
    std::memcpy(Ram.data() + static_cast<size_t>(Addr), Src, Bytes);
    return true;
}

inline bool ReadU64(uint64_t Addr, uint64_t& Out) {
    return ReadBytes(Addr, &Out, sizeof(Out));
}

inline bool WriteU64(uint64_t Addr, uint64_t Value) {
    return WriteBytes(Addr, &Value, sizeof(Value));
}

inline bool ReadU8(uint64_t Addr, uint8_t& Out) {
    return ReadBytes(Addr, &Out, sizeof(Out));
}

inline bool WriteU8(uint64_t Addr, uint8_t Value) {
    return WriteBytes(Addr, &Value, sizeof(Value));
}

} // namespace GuestMem
