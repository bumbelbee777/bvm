#include "GuestMem.h"

#include <cstring>

namespace GuestMem {

bool CopyRegion(uint64_t Dst, uint64_t Src, size_t Bytes) {
    if (Bytes == 0) {
        return true;
    }
    if (!RegionValid(Dst, Bytes) || !RegionValid(Src, Bytes)) {
        return false;
    }
    if (Dst == Src) {
        return true;
    }
    if (Dst < Src + Bytes && Src < Dst + Bytes) {
        std::memmove(Ram.data() + static_cast<size_t>(Dst), Ram.data() + static_cast<size_t>(Src),
                     Bytes);
    } else {
        std::memcpy(Ram.data() + static_cast<size_t>(Dst), Ram.data() + static_cast<size_t>(Src),
                    Bytes);
    }
    return true;
}

bool FillRegion(uint64_t Dst, uint8_t Byte, size_t Bytes) {
    if (Bytes == 0) {
        return true;
    }
    if (!RegionValid(Dst, Bytes)) {
        return false;
    }
    std::memset(Ram.data() + static_cast<size_t>(Dst), static_cast<int>(Byte), Bytes);
    return true;
}

} // namespace GuestMem
