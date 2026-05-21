#include "Shared.h"

#include <stdexcept>

using namespace Honeycomb;
using namespace PagingProfile;

namespace {

constexpr int LevelCount = 4;

constexpr int IndexShiftForLevel(int Level) noexcept {
    switch (Level) {
        case 0:
            return 39;
        case 1:
            return 30;
        case 2:
            return 21;
        default:
            return 12;
    }
}

constexpr uint32_t PageLog2ForHugeLevel(int Level) noexcept {
    if (Level == 1) {
        return PageShift1G;
    }
    if (Level == 2) {
        return PageShift2M;
    }
    return PageShift4K;
}

constexpr uint64_t PageOffsetMaskForLog2(uint32_t PageLog2) noexcept {
    return (1ULL << PageLog2) - 1ULL;
}

bool IsCanonicalVirtual(uint64_t VirtAddr) noexcept {
    const uint64_t Top = (VirtAddr >> 47) & 0x1FFFFULL;
    return Top == 0 || Top == 0x1FFFFULL;
}

uint64_t ExtractIndex(uint64_t VirtAddr, int Level) noexcept {
    return (VirtAddr >> IndexShiftForLevel(Level)) & IndexMask;
}

bool AccessAllowed(uint64_t Pte, MemoryAccessKind Kind) noexcept {
    if ((Pte & PteFlags::Present) == 0) {
        return false;
    }
    switch (Kind) {
        case MemoryAccessKind::Read:
            return (Pte & PteFlags::Read) != 0;
        case MemoryAccessKind::Write:
            return (Pte & PteFlags::Write) != 0;
        case MemoryAccessKind::Execute:
            return (Pte & PteFlags::Execute) != 0;
    }
    return false;
}

uint64_t ComposePhysical(uint64_t Pte, uint64_t VirtAddr, uint32_t PageLog2) noexcept {
    const uint64_t OffsetMask = PageOffsetMaskForLog2(PageLog2);
    const uint64_t FrameBase = Pte & PteFlags::PfnMask;
    return FrameBase | (VirtAddr & OffsetMask);
}

MemoryAccessKind KindFromRegister(uint64_t Raw) {
    switch (Raw & 3ULL) {
        case 1:
            return MemoryAccessKind::Write;
        case 2:
            return MemoryAccessKind::Execute;
        default:
            return MemoryAccessKind::Read;
    }
}

} // namespace

bool Cpu::PagingIsEnabled() const {
    return (Fr & StatusFlags::Paging) != 0;
}

bool Cpu::SoftwareTlbRefillEnabled() const {
    return (Fr & StatusFlags::SoftwareTlbRefill) != 0;
}

void Cpu::FlushAddressTlb() {
    AddressTlb.FlushAll();
}

void Cpu::FlushAddressTlbEntry(uint64_t VirtAddr) {
    AddressTlb.FlushVirtual(VirtAddr);
}

void Cpu::RaisePageFault(uint64_t VirtAddr, const char* Reason) {
    (void)Reason;
    (void)VirtAddr;
    DeliverInterruptVector(PageFaultVector);
}

void Cpu::RaiseTlbMiss(uint64_t VirtAddr, MemoryAccessKind Kind) {
    TlbFaultAddress = VirtAddr;
    TlbFaultAccess = static_cast<uint64_t>(Kind);
    DeliverInterruptVector(TlbMissVector);
}

uint64_t Cpu::WalkPageTables(uint64_t VirtAddr, MemoryAccessKind Kind) {
    if (!IsCanonicalVirtual(VirtAddr)) {
        RaisePageFault(VirtAddr, "non-canonical virtual address");
        return 0;
    }
    if ((Ptb & PageOffset4KMask) != 0) {
        throw std::runtime_error("PTB must be 4 KiB aligned");
    }

    uint64_t TablePhys = Ptb;
    for (int Level = 0; Level < LevelCount; ++Level) {
        const uint64_t Index = ExtractIndex(VirtAddr, Level);
        const uint64_t SlotPhys = TablePhys + Index * 8ULL;
        const uint64_t Pte = Read64Physical(SlotPhys);

        const bool Huge = (Pte & PteFlags::Huge) != 0;
        const bool LeafLevel = (Level == LevelCount - 1);

        if (LeafLevel || Huge) {
            if (!AccessAllowed(Pte, Kind)) {
                RaisePageFault(VirtAddr, "PTE not present or access denied");
                return 0;
            }
        } else if ((Pte & PteFlags::Present) == 0) {
            RaisePageFault(VirtAddr, "page table not present");
            return 0;
        }
        if (Huge && !LeafLevel) {
            const uint32_t PageLog2 = PageLog2ForHugeLevel(Level);
            const uint64_t Phys = ComposePhysical(Pte, VirtAddr, PageLog2);
            const uint64_t VirtPageBase = VirtAddr & ~PageOffsetMaskForLog2(PageLog2);
            const uint64_t PhysPageBase = Phys & ~PageOffsetMaskForLog2(PageLog2);
            AddressTlb.Insert(VirtPageBase, PhysPageBase, PageLog2,
                              static_cast<uint8_t>(Pte & PteFlags::AccessMask));
            return Phys;
        }

        if (LeafLevel) {
            const uint64_t Phys = ComposePhysical(Pte, VirtAddr, PageShift4K);
            const uint64_t VirtPageBase = VirtAddr & ~PageOffset4KMask;
            const uint64_t PhysPageBase = Phys & ~PageOffset4KMask;
            AddressTlb.Insert(VirtPageBase, PhysPageBase, PageShift4K,
                              static_cast<uint8_t>(Pte & PteFlags::AccessMask));
            return Phys;
        }

        TablePhys = Pte & PteFlags::PfnMask;
        if ((TablePhys & PageOffset4KMask) != 0) {
            RaisePageFault(VirtAddr, "misaligned page table pointer");
            return 0;
        }
    }

    RaisePageFault(VirtAddr, "page table walk failed");
    return 0;
}

uint64_t Cpu::TranslateVirtualAddress(uint64_t VirtAddr, MemoryAccessKind Kind) const {
    if (!PagingIsEnabled()) {
        return VirtAddr;
    }

    uint64_t Phys = 0;
    if (AddressTlb.Lookup(VirtAddr, Phys, Kind)) {
        return Phys;
    }

    if (SoftwareTlbRefillEnabled() && ExceptionFrameDepth == 0) {
        const_cast<Cpu*>(this)->RaiseTlbMiss(VirtAddr, Kind);
        return 0;
    }

    return const_cast<Cpu*>(this)->WalkPageTables(VirtAddr, Kind);
}

void Cpu::OpEnablePaging() {
    RequireSupervisor("Honeycomb EP is supervisor-only");
    if (Ptb == 0) {
        throw std::runtime_error("EP requires non-zero PTB");
    }
    FlushAddressTlb();
    Fr |= StatusFlags::Paging;
}

void Cpu::OpDisablePaging() {
    RequireSupervisor("Honeycomb DP is supervisor-only");
    Fr &= ~StatusFlags::Paging;
    FlushAddressTlb();
}

void Cpu::OpEnableSoftwareTlbRefill() {
    RequireSupervisor("Honeycomb ESR is supervisor-only");
    Fr |= StatusFlags::SoftwareTlbRefill;
}

void Cpu::OpDisableSoftwareTlbRefill() {
    RequireSupervisor("Honeycomb DSR is supervisor-only");
    Fr &= ~StatusFlags::SoftwareTlbRefill;
}

void Cpu::OpTlbInsert() {
    RequireSupervisor("Honeycomb TlbInsert is supervisor-only");
    if (!PagingIsEnabled()) {
        throw std::runtime_error("TlbInsert requires paging enabled (EP)");
    }
    WalkPageTables(TlbFaultAddress, KindFromRegister(TlbFaultAccess));
}

void Cpu::OpWireTlbEntry(const DecodedInsn& Insn) {
    RequireSupervisor("Honeycomb WireTlbEntry is supervisor-only");
    if (!Insn.Imm1Flag) {
        throw std::runtime_error("WireTlbEntry requires #slot immediate");
    }
    if (Insn.Imm1 >= TlbCapacity) {
        throw std::runtime_error("WireTlbEntry slot must be < TLB capacity (64)");
    }
    AddressTlb.WireSlot(static_cast<uint32_t>(Insn.Imm1));
}
