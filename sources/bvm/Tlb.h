#pragma once

#include "PagingProfile.h"

#include <array>
#include <cstdint>
#include <stdexcept>

struct TlbEntry {
    uint64_t VirtualPageBase = 0;
    uint64_t PhysicalPageBase = 0;
    uint32_t PageLog2 = PagingProfile::PageShift4K;
    uint8_t Perm = 0;
    bool Valid = false;
    bool Wired = false;
};

class Tlb {
public:
    void FlushAll();
    void FlushVirtual(uint64_t VirtAddr);

    bool Lookup(uint64_t VirtAddr, uint64_t& PhysAddr,
                PagingProfile::MemoryAccessKind Access) const;

    void Insert(uint64_t VirtPageBase, uint64_t PhysPageBase, uint32_t PageLog2,
                uint8_t Perm, bool Wired = false);

    void WireSlot(uint32_t Slot);
    void UnwireSlot(uint32_t Slot);

private:
    static bool AccessAllowed(uint8_t Perm, PagingProfile::MemoryAccessKind Kind) noexcept {
        if ((Perm & PagingProfile::PteFlags::Present) == 0) {
            return false;
        }
        switch (Kind) {
            case PagingProfile::MemoryAccessKind::Read:
                return (Perm & PagingProfile::PteFlags::Read) != 0;
            case PagingProfile::MemoryAccessKind::Write:
                return (Perm & PagingProfile::PteFlags::Write) != 0;
            case PagingProfile::MemoryAccessKind::Execute:
                return (Perm & PagingProfile::PteFlags::Execute) != 0;
        }
        return false;
    }

    int FindInvalidSlot() const;
    int ChooseEvictionSlot();

    std::array<TlbEntry, PagingProfile::TlbCapacity> Entries{};
    size_t EvictCursor = 0;
};
