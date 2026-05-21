#include "Tlb.h"

void Tlb::FlushAll() {
    for (TlbEntry& Entry : Entries) {
        Entry.Valid = false;
        Entry.Wired = false;
    }
    EvictCursor = 0;
}

void Tlb::FlushVirtual(uint64_t VirtAddr) {
    for (TlbEntry& Entry : Entries) {
        if (!Entry.Valid || Entry.Wired) {
            continue;
        }
        const uint64_t Mask = (1ULL << Entry.PageLog2) - 1ULL;
        if ((Entry.VirtualPageBase & ~Mask) == (VirtAddr & ~Mask)) {
            Entry.Valid = false;
        }
    }
}

bool Tlb::Lookup(uint64_t VirtAddr, uint64_t& PhysAddr,
                 PagingProfile::MemoryAccessKind Access) const {
    for (const TlbEntry& Entry : Entries) {
        if (!Entry.Valid) {
            continue;
        }
        const uint64_t Mask = (1ULL << Entry.PageLog2) - 1ULL;
        if ((Entry.VirtualPageBase & ~Mask) != (VirtAddr & ~Mask)) {
            continue;
        }
        if (!AccessAllowed(Entry.Perm, Access)) {
            return false;
        }
        PhysAddr = Entry.PhysicalPageBase | (VirtAddr & Mask);
        return true;
    }
    return false;
}

int Tlb::FindInvalidSlot() const {
    for (size_t I = 0; I < Entries.size(); ++I) {
        if (!Entries[I].Valid) {
            return static_cast<int>(I);
        }
    }
    return -1;
}

int Tlb::ChooseEvictionSlot() {
    const size_t Capacity = Entries.size();
    for (size_t Attempt = 0; Attempt < Capacity; ++Attempt) {
        const size_t Index = (EvictCursor + Attempt) % Capacity;
        if (Entries[Index].Valid && !Entries[Index].Wired) {
            EvictCursor = (Index + 1) % Capacity;
            return static_cast<int>(Index);
        }
    }
    return -1;
}

void Tlb::Insert(uint64_t VirtPageBase, uint64_t PhysPageBase, uint32_t PageLog2,
                 uint8_t Perm, bool Wired) {
    int Slot = FindInvalidSlot();
    if (Slot < 0) {
        Slot = ChooseEvictionSlot();
    }
    if (Slot < 0) {
        throw std::runtime_error(
            "Honeycomb TLB full (all entries wired); cannot insert translation");
    }
    TlbEntry& Entry = Entries[static_cast<size_t>(Slot)];
    Entry.VirtualPageBase = VirtPageBase;
    Entry.PhysicalPageBase = PhysPageBase;
    Entry.PageLog2 = PageLog2;
    Entry.Perm = Perm;
    Entry.Wired = Wired;
    Entry.Valid = true;
}

void Tlb::WireSlot(uint32_t Slot) {
    if (Slot >= Entries.size()) {
        throw std::out_of_range("WireTlbEntry slot out of range");
    }
    if (!Entries[Slot].Valid) {
        throw std::runtime_error("WireTlbEntry requires a valid TLB slot");
    }
    Entries[Slot].Wired = true;
}

void Tlb::UnwireSlot(uint32_t Slot) {
    if (Slot >= Entries.size()) {
        throw std::out_of_range("WireTlbEntry slot out of range");
    }
    Entries[Slot].Wired = false;
}
