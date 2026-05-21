#include "Shared.h"
#include "RegFile.h"
#include "Compact.h"
#include "Devices.h"
#include "Machine.h"
#include "CompactProfile.h"
#include "AluShiftProfile.h"
#include "MemAddrProfile.h"
#include "PhysMap.h"
#include "PagingProfile.h"
#include "NumaProfile.h"
#include "SkbProfile.h"
#include "CplxProfile.h"
#include "DaxMailbox.h"
#include "KBankProfile.h"
#include "SsxProfile.h"
#include "Security.h"
#include "CacheProfile.h"
#include "L1Cache.h"

#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>

#if defined(_M_X64) && defined(_MSC_VER)
#include <intrin.h>
#endif

using namespace Honeycomb;

std::array<uint8_t, IoPorts> Ports = {};

namespace {

int64_t SignedAddSaturated(int64_t A, int64_t B) {
    if (B > 0 && A > std::numeric_limits<int64_t>::max() - B) {
        return std::numeric_limits<int64_t>::max();
    }
    if (B < 0 && A < std::numeric_limits<int64_t>::min() - B) {
        return std::numeric_limits<int64_t>::min();
    }
    return A + B;
}

int64_t SignedMulSaturated(int64_t Lhs, int64_t Rhs) {
    if (Lhs == 0 || Rhs == 0) {
        return 0;
    }
#if defined(__SIZEOF_INT128__)
    const __int128 Product = static_cast<__int128>(Lhs) * static_cast<__int128>(Rhs);
    if (Product > static_cast<__int128>(std::numeric_limits<int64_t>::max())) {
        return std::numeric_limits<int64_t>::max();
    }
    if (Product < static_cast<__int128>(std::numeric_limits<int64_t>::min())) {
        return std::numeric_limits<int64_t>::min();
    }
    return static_cast<int64_t>(Product);
#elif (defined(__GNUC__) || defined(__clang__)) && !defined(_MSC_VER)
    int64_t Product = 0;
    if (__builtin_mul_overflow(Lhs, Rhs, &Product)) {
        const bool Negative = (Lhs > 0) != (Rhs > 0);
        return Negative ? std::numeric_limits<int64_t>::min()
                        : std::numeric_limits<int64_t>::max();
    }
    return Product;
#elif defined(_M_X64) && defined(_MSC_VER)
    const bool Negative = (Lhs > 0) != (Rhs > 0);
    const auto ToMag = [](int64_t Value) -> unsigned __int64 {
        const unsigned __int64 Bits = static_cast<unsigned __int64>(Value);
        constexpr unsigned __int64 SignMask =
            ~(std::numeric_limits<unsigned __int64>::max() >> 1);
        if ((Bits & SignMask) == 0) {
            return Bits;
        }
        return static_cast<unsigned __int64>(0) - Bits;
    };
    const unsigned __int64 MagLeft = ToMag(Lhs);
    const unsigned __int64 MagRight = ToMag(Rhs);
    unsigned __int64 Hi = 0;
    const unsigned __int64 Lo = _umul128(MagLeft, MagRight, &Hi);
    if (Hi != 0) {
        return Negative ? std::numeric_limits<int64_t>::min()
                        : std::numeric_limits<int64_t>::max();
    }
    constexpr unsigned __int64 Int64MaxU =
        static_cast<unsigned __int64>(std::numeric_limits<int64_t>::max());
    constexpr unsigned __int64 Int64MinMagU =
        static_cast<unsigned __int64>(std::numeric_limits<int64_t>::min());
    if (!Negative) {
        if (Lo > Int64MaxU) {
            return std::numeric_limits<int64_t>::max();
        }
        return static_cast<int64_t>(Lo);
    }
    if (Lo >= Int64MinMagU) {
        return std::numeric_limits<int64_t>::min();
    }
    return -static_cast<int64_t>(Lo);
#else
    const long double Wide =
        static_cast<long double>(Lhs) * static_cast<long double>(Rhs);
    if (Wide >
        static_cast<long double>(std::numeric_limits<int64_t>::max())) {
        return std::numeric_limits<int64_t>::max();
    }
    if (Wide <
        static_cast<long double>(std::numeric_limits<int64_t>::min())) {
        return std::numeric_limits<int64_t>::min();
    }
    return static_cast<int64_t>(Wide);
#endif
}

uint64_t& GprRef(Cpu& CpuState, uint8_t Code) {
    switch (Code) {
        case 0x00: return CpuState.R0;
        case 0x01: return CpuState.R1;
        case 0x02: return CpuState.R2; case 0x03: return CpuState.R3;
        case 0x04: return CpuState.R4; case 0x05: return CpuState.R5;
        case 0x06: return CpuState.R6; case 0x07: return CpuState.R7;
        case 0x08: return CpuState.R8; case 0x09: return CpuState.R9;
        case 0x0A: return CpuState.R10; case 0x0B: return CpuState.R11;
        case 0x0C: return CpuState.R12; case 0x0D: return CpuState.R13;
        case 0x0E: return CpuState.R14; case 0x0F: return CpuState.R15;
        case 0x10: return CpuState.R16; case 0x11: return CpuState.R17;
        case 0x12: return CpuState.R18; case 0x13: return CpuState.R19;
        case 0x14: return CpuState.R20; case 0x15: return CpuState.R21;
        case 0x16: return CpuState.R22; case 0x17: return CpuState.R23;
        case 0x18: return CpuState.R24; case 0x19: return CpuState.R25;
        case 0x1A: return CpuState.R26; case 0x1B: return CpuState.R27;
        case 0x1C: return CpuState.R28; case 0x1D: return CpuState.R29;
        case 0x1E: return CpuState.R30; case 0x1F: return CpuState.R31;
        case 0x20: return CpuState.Sp;
        case 0x21: return CpuState.Ic;
        case 0x22: return CpuState.Fr;
        case 0x23: return CpuState.Ptb;
        case 0x24: return CpuState.Ivb;
        case 0x25: return CpuState.Ivl;
        case 0x26: return CpuState.Wind;
        case 0x27: return CpuState.WinSz;
        case 0x28: return CpuState.I0;
        case 0x29: return CpuState.I1;
        case 0x2A: return CpuState.I2;
        case 0x2B: return CpuState.I3;
        case 0x58: return CpuState.Cwp;
        case 0x59: return CpuState.CanSave;
        case 0x5A: return CpuState.CanRestore;
        case 0x5B: return CpuState.OtherWin;
        case 0x5D: return CpuState.RotBase;
        case 0x5E: return CpuState.RotSz;
        case 0x5F: return CpuState.KMask;
        default: throw std::out_of_range("Invalid register code");
    }
}

uint64_t ThirdOperand(const Cpu& CpuState, const DecodedInsn& Insn) {
    uint64_t Raw = Insn.Imm1Flag ? Insn.Imm1 : CpuState.ReadGpr(Insn.Rt);
    return AluShiftProfile::Apply(Raw, AluShiftProfile::KindFromDisp(Insn.Disp),
                                  AluShiftProfile::AmountFromDisp(Insn.Disp));
}

uint64_t ResolveScalarMemAddr(const Cpu& CpuState, const DecodedInsn& Insn) {
    if (Insn.Imm1Flag) {
        return Insn.Imm1;
    }
    uint64_t Addr = static_cast<uint64_t>(static_cast<int64_t>(Insn.Disp));
    if (Insn.Rs != 0) {
        Addr += CpuState.ReadGpr(Insn.Rs);
    }
    if (Insn.Rt != 0) {
        Addr += CpuState.ReadGpr(Insn.Rt);
    }
    return Addr;
}

} // namespace

Cpu::Cpu() { Reset(); }

void Cpu::Reset() {
    R0 = R1 = R2 = R3 = R4 = R5 = R6 = R7 = 0;
    R8 = R9 = R10 = R11 = R12 = R13 = R14 = R15 = 0;
    R16 = R17 = R18 = R19 = R20 = R21 = R22 = R23 = 0;
    R24 = R25 = R26 = R27 = R28 = R29 = R30 = R31 = 0;
    Sp = GetRamSize();
    Ic = 0;
    Fr = 0;
    Ptb = 0;
    Ivb = 0;
    Ivl = 0;
    Wind = 0;
    WinSz = CplxProfile::DefaultWinSzMerged;
    I0 = I1 = I2 = I3 = 0;
    KMask = GetDefaultKMask();
    KBank.fill(0);
    DaxMeta.fill({});
    Cwp = 0;
    CanSave = 6;
    CanRestore = 0;
    OtherWin = 0;
    RotBase = 0;
    RotSz = 0;
    WindowSpillBase = 0;
    StreamTagDone.fill(true);
    XRegs.fill(0);
    A0 = A1 = A2 = A3 = 0;
    FRegs.fill(0.0);
    PendingInterruptVectors.clear();
    ExceptionFrameDepth = 0;
    ExceptionFrames.fill({});
    TlbFaultAddress = 0;
    TlbFaultAccess = 0;
    SyscallFrameDepth = 0;
    PerfInstructionsRetired = 0;
    PerfCycles = 0;
    PerfInterruptsDelivered = 0;
    Running = true;
    Halted = false;
    MmioBootControlRegister = 0;
    MmioSyscallProgramCounter = 0;
    MmioLastSyscallNumberRegister = 0;
    SsxVectorLength = SsxBaselineProfile::DefaultVectorLength;
    DaxMailbox::Reset();
    ResetSkb();
    ResetNuma();
    FlushAddressTlb();
    FlushL1Caches();
}

void Cpu::FlushL1Caches() {
    ICache.Flush();
    DCache.Flush();
}

void Cpu::ChargeInstructionCacheMiss(bool Miss) {
    if (Miss) {
        PerfCycles += CacheProfile::InstructionMissPenalty;
    }
}

void Cpu::ChargeDataCacheMiss(bool Miss) {
    if (Miss) {
        PerfCycles += CacheProfile::DataMissPenalty;
    }
}

void Cpu::InvalidateInstructionForDataStore(uint64_t PhysAddr) {
    ICache.InvalidateLine(PhysAddr);
}

void Cpu::LoadImage(const uint8_t* Data, size_t Size, uint64_t LoadAddr, uint64_t Entry) {
  if (LoadAddr + Size > GetRamSize()) {
        throw std::runtime_error("Image does not fit in RAM");
    }
    std::memcpy(Ram.data() + LoadAddr, Data, Size);
    FlushL1Caches();
    Ic = Entry & ~1ULL;
    Fr &= ~StatusFlags::CompactMode;
    if ((Entry & 1ULL) == 0) {
        Fr |= StatusFlags::CompactMode;
    }
}

uint64_t Cpu::ReadGpr(uint8_t Code) const {
    switch (Code) {
        case 0x00: return R0; case 0x01: return R1;
        case 0x02: return R2; case 0x03: return R3;
        case 0x04: return R4; case 0x05: return R5;
        case 0x06: return R6; case 0x07: return R7;
        case 0x08: return R8; case 0x09: return R9;
        case 0x0A: return R10; case 0x0B: return R11;
        case 0x0C: return R12; case 0x0D: return R13;
        case 0x0E: return R14; case 0x0F: return R15;
        case 0x10: return R16; case 0x11: return R17;
        case 0x12: return R18; case 0x13: return R19;
        case 0x14: return R20; case 0x15: return R21;
        case 0x16: return R22; case 0x17: return R23;
        case 0x18: return R24; case 0x19: return R25;
        case 0x1A: return R26; case 0x1B: return R27;
        case 0x1C: return R28; case 0x1D: return R29;
        case 0x1E: return R30; case 0x1F: return R31;
        case 0x20: return Sp;
        case 0x21: return Ic;
        case 0x22: return Fr;
        case 0x23: return Ptb;
        case 0x24: return Ivb;
        case 0x25: return Ivl;
        case 0x26: return Wind;
        case 0x27: return WinSz;
        case 0x28: return I0;
        case 0x29: return I1;
        case 0x2A: return I2;
        case 0x2B: return I3;
        case 0x2C: return TlbFaultAddress;
        case 0x2D: return TlbFaultAccess;
        case 0x5C: return 0;
        case 0x58: return Cwp;
        case 0x59: return CanSave;
        case 0x5A: return CanRestore;
        case 0x5B: return OtherWin;
        case 0x5D: return RotBase;
        case 0x5E: return RotSz;
        case 0x5F: return KMask;
        default:
            if (IsSecurityCsr(Code)) {
                return ReadSecurityCsr(*this, Code);
            }
            throw std::out_of_range("Invalid register code");
    }
}

void Cpu::WriteGpr(uint8_t Code, uint64_t Value) {
    if (Code == 0x23 && Value != Ptb) {
        Ptb = Value;
        FlushAddressTlb();
        return;
    }
    if (Code == 0x2C) {
        TlbFaultAddress = Value;
        return;
    }
    if (Code == 0x2D) {
        TlbFaultAccess = Value;
        return;
    }
    if (IsSecurityCsr(Code)) {
        WriteSecurityCsr(*this, Code, Value);
        return;
    }
    GprRef(*this, Code) = Value;
}

uint8_t Read8Physical(uint64_t Addr) {
    if (CoversMmioPhysicalAddress(Addr)) {
        throw std::runtime_error(
            "Sub-word accesses to Honeycomb MMIO aperture are unsupported in baseline BVM");
    }
    if (PhysMap::CoversLocalDram(Addr, 1)) {
        return Ram[PhysMap::LocalDramOffset(Addr)];
    }
    if (Addr >= GetRamSize()) {
        throw std::out_of_range("Memory read out of range");
    }
    return Ram[Addr];
}

uint16_t Read16Physical(uint64_t Addr) {
    return (static_cast<uint16_t>(Read8Physical(Addr)) << 8) | Read8Physical(Addr + 1);
}

uint32_t Read32Physical(uint64_t Addr) {
    return (static_cast<uint32_t>(Read16Physical(Addr)) << 16) | Read16Physical(Addr + 2);
}

uint8_t Cpu::Read8(uint64_t Addr) const {
    const uint64_t Phys =
        TranslateVirtualAddress(Addr, PagingProfile::MemoryAccessKind::Read);
    if (DCache.IsCacheable(Phys)) {
        bool Miss = false;
        const uint8_t Value = DCache.ReadU8(Phys, Miss);
        const_cast<Cpu*>(this)->ChargeDataCacheMiss(Miss);
        return Value;
    }
    return Read8Physical(Phys);
}

uint16_t Cpu::Read16(uint64_t Addr) const {
    const uint64_t Phys =
        TranslateVirtualAddress(Addr, PagingProfile::MemoryAccessKind::Read);
    if (DCache.IsCacheable(Phys)) {
        bool Miss = false;
        const uint16_t Value = DCache.ReadU16(Phys, Miss);
        const_cast<Cpu*>(this)->ChargeDataCacheMiss(Miss);
        return Value;
    }
    return Read16Physical(Phys);
}

uint32_t Cpu::Read32(uint64_t Addr) const {
    const uint64_t Phys =
        TranslateVirtualAddress(Addr, PagingProfile::MemoryAccessKind::Read);
    if (DCache.IsCacheable(Phys)) {
        bool Miss = false;
        const uint32_t Value = DCache.ReadU32(Phys, Miss);
        const_cast<Cpu*>(this)->ChargeDataCacheMiss(Miss);
        return Value;
    }
    return Read32Physical(Phys);
}

uint64_t Cpu::Read64Physical(uint64_t Addr) const {
    if ((Addr & 7ULL) != 0) {
        throw std::runtime_error("Honeycomb LOAD requires 64-bit aligned addresses");
    }

    if (NumaProfile::CoversRemoteK(Addr, 8) || NumaProfile::CoversRemoteSkb(Addr, 8) ||
        NumaProfile::CoversRemoteDram(Addr, 8)) {
        return ReadRemotePhysical(Addr);
    }

    if (PhysMap::CoversLocalSkb(Addr, 8)) {
        return ReadSkbPhysical(Addr);
    }
    if (PhysMap::CoversLocalK(Addr, 8)) {
        return ReadKPhysical(Addr);
    }
    if (PhysMap::CoversLocalDram(Addr, 8)) {
        const uint64_t Offset = PhysMap::LocalDramOffset(Addr);
        return (static_cast<uint64_t>(Read32Physical(Offset)) << 32) |
               Read32Physical(Offset + 4);
    }

    const bool QuadInMmio =
        Addr >= GetMmioWindowBase() &&
        Addr + 8 <= GetMmioWindowBase() + MmioWindow::WindowByteExtent;
    if (QuadInMmio) {
        return MmioDispatchReadQuadWord(Addr);
    }

    throw std::out_of_range("Memory read out of range");
}

uint16_t Cpu::ReadInstructionU16(uint64_t VirtAddr) {
    const uint64_t Phys =
        TranslateVirtualAddress(VirtAddr, PagingProfile::MemoryAccessKind::Execute);
    if (ICache.IsCacheable(Phys)) {
        bool Miss = false;
        const uint16_t Value = ICache.ReadU16(Phys, Miss);
        ChargeInstructionCacheMiss(Miss);
        return Value;
    }
    return Read16Physical(Phys);
}

uint64_t Cpu::ReadInstructionU64(uint64_t VirtAddr) {
    const uint64_t Phys =
        TranslateVirtualAddress(VirtAddr, PagingProfile::MemoryAccessKind::Execute);
    if (ICache.IsCacheable(Phys)) {
        bool Miss = false;
        const uint64_t Value = ICache.ReadU64(Phys, Miss);
        ChargeInstructionCacheMiss(Miss);
        return Value;
    }
    return Read64Physical(Phys);
}

uint64_t Cpu::Read64(uint64_t Addr) const {
    const uint64_t Phys =
        TranslateVirtualAddress(Addr, PagingProfile::MemoryAccessKind::Read);
    if (DCache.IsCacheable(Phys)) {
        bool Miss = false;
        const uint64_t Value = DCache.ReadU64(Phys, Miss);
        const_cast<Cpu*>(this)->ChargeDataCacheMiss(Miss);
        return Value;
    }
    return Read64Physical(Phys);
}

void Write8Physical(uint64_t Addr, uint8_t Value) {
    if (CoversMmioPhysicalAddress(Addr)) {
        throw std::runtime_error(
            "Sub-word writes to Honeycomb MMIO aperture are unsupported in baseline BVM");
    }
    if (PhysMap::CoversLocalDram(Addr, 1)) {
        Ram[PhysMap::LocalDramOffset(Addr)] = Value;
        return;
    }
    if (Addr >= GetRamSize()) {
        throw std::out_of_range("Memory write out of range");
    }
    Ram[Addr] = Value;
}

void Write16Physical(uint64_t Addr, uint16_t Value) {
    Write8Physical(Addr, static_cast<uint8_t>(Value >> 8));
    Write8Physical(Addr + 1, static_cast<uint8_t>(Value));
}

void Write32Physical(uint64_t Addr, uint32_t Value) {
    Write16Physical(Addr, static_cast<uint16_t>(Value >> 16));
    Write16Physical(Addr + 2, static_cast<uint16_t>(Value));
}

void Cpu::Write64Physical(uint64_t Addr, uint64_t Value) {
    if ((Addr & 7ULL) != 0) {
        throw std::runtime_error("Honeycomb STORE requires 64-bit aligned addresses");
    }

    if (NumaProfile::CoversRemoteK(Addr, 8) || NumaProfile::CoversRemoteSkb(Addr, 8) ||
        NumaProfile::CoversRemoteDram(Addr, 8)) {
        WriteRemotePhysical(Addr, Value);
        return;
    }

    if (PhysMap::CoversLocalSkb(Addr, 8)) {
        WriteSkbPhysical(Addr, Value);
        return;
    }
    if (PhysMap::CoversLocalK(Addr, 8)) {
        WriteKPhysical(Addr, Value);
        return;
    }
    if (PhysMap::CoversLocalDram(Addr, 8)) {
        const uint64_t Offset = PhysMap::LocalDramOffset(Addr);
        Write32Physical(Offset, static_cast<uint32_t>(Value >> 32));
        Write32Physical(Offset + 4, static_cast<uint32_t>(Value));
        return;
    }

    const bool QuadInMmio =
        Addr >= GetMmioWindowBase() &&
        Addr + 8 <= GetMmioWindowBase() + MmioWindow::WindowByteExtent;
    if (QuadInMmio) {
        MmioDispatchWriteQuadWord(Addr, Value);
        return;
    }

    throw std::out_of_range("Memory write out of range");
}

void Cpu::Write64(uint64_t Addr, uint64_t Value) {
    const uint64_t Phys =
        TranslateVirtualAddress(Addr, PagingProfile::MemoryAccessKind::Write);
    if (DCache.IsCacheable(Phys)) {
        DCache.WriteU64(Phys, Value);
        InvalidateInstructionForDataStore(Phys);
        return;
    }
    Write64Physical(Phys, Value);
}

Uint128 Cpu::Read128(uint64_t Addr) const {
    return (static_cast<Uint128>(Read64(Addr)) << 64) | Read64(Addr + 8);
}

void Cpu::Write8(uint64_t Addr, uint8_t Value) {
    const uint64_t Phys =
        TranslateVirtualAddress(Addr, PagingProfile::MemoryAccessKind::Write);
    if (DCache.IsCacheable(Phys)) {
        DCache.WriteU8(Phys, Value);
        InvalidateInstructionForDataStore(Phys);
        return;
    }
    Write8Physical(Phys, Value);
}

void Cpu::Write16(uint64_t Addr, uint16_t Value) {
    const uint64_t Phys =
        TranslateVirtualAddress(Addr, PagingProfile::MemoryAccessKind::Write);
    if (DCache.IsCacheable(Phys)) {
        DCache.WriteU16(Phys, Value);
        InvalidateInstructionForDataStore(Phys);
        return;
    }
    Write16Physical(Phys, Value);
}

void Cpu::Write32(uint64_t Addr, uint32_t Value) {
    const uint64_t Phys =
        TranslateVirtualAddress(Addr, PagingProfile::MemoryAccessKind::Write);
    if (DCache.IsCacheable(Phys)) {
        DCache.WriteU32(Phys, Value);
        InvalidateInstructionForDataStore(Phys);
        return;
    }
    Write32Physical(Phys, Value);
}


void Cpu::Write128(uint64_t Addr, Uint128 Value) {
    Write64(Addr, static_cast<uint64_t>(Value >> 64));
    Write64(Addr + 8, static_cast<uint64_t>(Value));
}

void Cpu::Push64(uint64_t Value) {
    if (Sp < 8 || Sp > GetRamSize()) {
        throw std::runtime_error("Stack overflow");
    }
    Sp -= 8;
    Write64(Sp, Value);
}

uint64_t Cpu::Pop64() {
    if (Sp >= GetRamSize() || Sp + 8 > GetRamSize()) {
        throw std::runtime_error("Stack underflow");
    }
    uint64_t Value = Read64(Sp);
    Sp += 8;
    return Value;
}

DecodedInsn Cpu::Fetch() {
    const uint64_t Pc = Ic;
    if ((Fr & StatusFlags::CompactMode) != 0) {
        const uint16_t Word = ReadInstructionU16(Pc);
        DecodedInsn Insn = DecodeCompactInsn(Word, Pc);
        Ic = Pc + 2;
        return Insn;
    }
    if ((Pc & 7ULL) != 0) {
        throw std::runtime_error("Full-mode fetch requires 8-byte aligned IC");
    }
    uint64_t Base = ReadInstructionU64(Pc);
    DecodedInsn Insn = DecodeBase(Base);
    Insn.Address = Pc;
    uint64_t Next = Pc + 8;
    if (Insn.Imm1Flag) {
        Insn.Imm1 = ReadInstructionU64(Next);
        Next += 8;
    }
    if (Insn.Imm2Flag) {
        Insn.Imm2 = ReadInstructionU64(Next);
        Next += 8;
    }
    Ic = Next;
    return Insn;
}

void Cpu::OpCallEnter(uint64_t TaggedTarget) {
    Push64(Ic);
    Push64(Fr & StatusFlags::CompactMode);
    if (TaggedTarget & 1ULL) {
        Fr &= ~StatusFlags::CompactMode;
    } else {
        Fr |= StatusFlags::CompactMode;
    }
    Ic = TaggedTarget & ~1ULL;
    if ((Fr & StatusFlags::CompactMode) == 0 && (Ic & 7ULL) != 0) {
        throw std::runtime_error("Full-mode CALL target must be 8-byte aligned");
    }
}

void Cpu::OpRetLeave() {
    const uint64_t SavedMode = Pop64();
    Ic = Pop64();
    Fr = (Fr & ~StatusFlags::CompactMode) | (SavedMode & StatusFlags::CompactMode);
}

void Cpu::ExecuteCompact(const DecodedInsn& Insn) {
    if (Insn.Op == Opcode::JE || Insn.Op == Opcode::JNE) {
        SetCompareFlags(static_cast<int64_t>(ReadGpr(Insn.Rd)));
        OpBranch(Insn);
        return;
    }
    if (Insn.Op == Opcode::CALL) {
        const uint16_t Word = ReadInstructionU16(Insn.Address);
        const bool ToFull = (Word & CompactProfile::CallModeBit) != 0;
        const int32_t Slot = CompactProfile::SignExt9(Word & 0x1FF);
        const uint64_t Physical =
            static_cast<uint64_t>(static_cast<int64_t>(Insn.Address) + Slot * 2);
        OpCallEnter(ToFull ? (Physical | 1ULL) : Physical);
        return;
    }
    if (Insn.Op == Opcode::RET) {
        OpRetLeave();
        return;
    }
    if (Insn.Op == Opcode::JMP) {
        Ic = static_cast<uint64_t>(static_cast<int64_t>(Insn.Address) + Insn.Disp);
        return;
    }
    Execute(Insn);
}

void Cpu::SetCompareFlags(int64_t Diff) {
    Fr &= ~(StatusFlags::Zero | StatusFlags::Sign | StatusFlags::Equal |
            StatusFlags::Greater | StatusFlags::Carry | StatusFlags::Overflow);
    if (Diff == 0) Fr |= StatusFlags::Zero | StatusFlags::Equal;
    if (Diff > 0) Fr |= StatusFlags::Greater;
    if (Diff < 0) Fr |= StatusFlags::Sign;
}

void Cpu::UpdateCompareResult(int64_t Diff) { SetCompareFlags(Diff); }

void Cpu::SetZeroSign(uint64_t Result) {
    Fr &= ~(StatusFlags::Zero | StatusFlags::Sign);
    if (Result == 0) Fr |= StatusFlags::Zero;
    if (Result & (1ULL << 63)) Fr |= StatusFlags::Sign;
}

void Cpu::SetFlagsAfterAdd(uint64_t Lhs, uint64_t Rhs, uint64_t Result) {
    Fr &= ~(StatusFlags::Carry | StatusFlags::Overflow | StatusFlags::Zero |
            StatusFlags::Sign | StatusFlags::Equal | StatusFlags::Greater);
    if (Result < Lhs) Fr |= StatusFlags::Carry;
    const uint64_t SignMask = (1ULL << 63);
    if ((~(Lhs ^ Rhs) & (Lhs ^ Result) & SignMask) != 0) {
        Fr |= StatusFlags::Overflow;
    }
    if (Result == 0) Fr |= StatusFlags::Zero;
    if (Result & SignMask) Fr |= StatusFlags::Sign;
}

void Cpu::SetFlagsAfterSub(uint64_t Lhs, uint64_t Rhs, uint64_t Result) {
    Fr &= ~(StatusFlags::Carry | StatusFlags::Overflow | StatusFlags::Zero |
            StatusFlags::Sign | StatusFlags::Equal | StatusFlags::Greater);
    if (Lhs < Rhs) Fr |= StatusFlags::Carry;
    const uint64_t SignMask = (1ULL << 63);
    if (((Lhs ^ Rhs) & (Lhs ^ Result) & SignMask) != 0) {
        Fr |= StatusFlags::Overflow;
    }
    if (Result == 0) Fr |= StatusFlags::Zero;
    if (Result & SignMask) Fr |= StatusFlags::Sign;
}

void Cpu::OpAdd(const DecodedInsn& Insn) {
    uint64_t Lhs = ReadGpr(Insn.Rs);
    uint64_t Rhs = ThirdOperand(*this, Insn);
    uint64_t Result = Lhs + Rhs;
    WriteGpr(Insn.Rd, Result);
    SetFlagsAfterAdd(Lhs, Rhs, Result);
}

void Cpu::OpSub(const DecodedInsn& Insn) {
    uint64_t Lhs = ReadGpr(Insn.Rs);
    uint64_t Rhs = ThirdOperand(*this, Insn);
    uint64_t Result = Lhs - Rhs;
    WriteGpr(Insn.Rd, Result);
    SetFlagsAfterSub(Lhs, Rhs, Result);
}

void Cpu::OpMul(const DecodedInsn& Insn) {
    uint64_t Result = ReadGpr(Insn.Rs) * ThirdOperand(*this, Insn);
    WriteGpr(Insn.Rd, Result);
    Fr &= ~(StatusFlags::Carry | StatusFlags::Overflow | StatusFlags::Equal |
            StatusFlags::Greater);
    SetZeroSign(Result);
}

void Cpu::OpDiv(const DecodedInsn& Insn) {
    int64_t Num = static_cast<int64_t>(ReadGpr(Insn.Rs));
    int64_t Den = static_cast<int64_t>(ThirdOperand(*this, Insn));
    if (Den == 0) throw std::runtime_error("Division by zero");
    if (Num == std::numeric_limits<int64_t>::min() && Den == -1) {
        throw std::runtime_error("Division overflow");
    }
    int64_t Quotient = Num / Den;
    uint64_t Result = static_cast<uint64_t>(Quotient);
    WriteGpr(Insn.Rd, Result);
    Fr &= ~(StatusFlags::Carry | StatusFlags::Overflow | StatusFlags::Equal |
            StatusFlags::Greater);
    SetZeroSign(Result);
}

void Cpu::OpMod(const DecodedInsn& Insn) {
    int64_t Num = static_cast<int64_t>(ReadGpr(Insn.Rs));
    int64_t Den = static_cast<int64_t>(ThirdOperand(*this, Insn));
    if (Den == 0) throw std::runtime_error("Division by zero");
    int64_t Rem = Num % Den;
    uint64_t Result = static_cast<uint64_t>(Rem);
    WriteGpr(Insn.Rd, Result);
    Fr &= ~(StatusFlags::Carry | StatusFlags::Overflow | StatusFlags::Equal |
            StatusFlags::Greater);
    SetZeroSign(Result);
}

void Cpu::OpAnd(const DecodedInsn& Insn) {
    uint64_t Result = ReadGpr(Insn.Rs) & ThirdOperand(*this, Insn);
    WriteGpr(Insn.Rd, Result);
    Fr &= ~(StatusFlags::Carry | StatusFlags::Overflow | StatusFlags::Equal |
            StatusFlags::Greater);
    SetZeroSign(Result);
}

void Cpu::OpOr(const DecodedInsn& Insn) {
    uint64_t Result = ReadGpr(Insn.Rs) | ThirdOperand(*this, Insn);
    WriteGpr(Insn.Rd, Result);
    Fr &= ~(StatusFlags::Carry | StatusFlags::Overflow | StatusFlags::Equal |
            StatusFlags::Greater);
    SetZeroSign(Result);
}

void Cpu::OpXor(const DecodedInsn& Insn) {
    uint64_t Result = ReadGpr(Insn.Rs) ^ ThirdOperand(*this, Insn);
    WriteGpr(Insn.Rd, Result);
    Fr &= ~(StatusFlags::Carry | StatusFlags::Overflow | StatusFlags::Equal |
            StatusFlags::Greater);
    SetZeroSign(Result);
}

void Cpu::OpNot(const DecodedInsn& Insn) {
    uint64_t Result = ~ReadGpr(Insn.Rs);
    WriteGpr(Insn.Rd, Result);
    Fr &= ~(StatusFlags::Carry | StatusFlags::Overflow | StatusFlags::Equal |
            StatusFlags::Greater);
    SetZeroSign(Result);
}

void Cpu::OpCmp(const DecodedInsn& Insn) {
    int64_t Lhs = static_cast<int64_t>(ReadGpr(Insn.Rs));
    uint64_t RawRhs = Insn.Imm1Flag ? Insn.Imm1 : ReadGpr(Insn.Rt);
    RawRhs = AluShiftProfile::Apply(RawRhs, AluShiftProfile::KindFromDisp(Insn.Disp),
                                    AluShiftProfile::AmountFromDisp(Insn.Disp));
    SetCompareFlags(Lhs - static_cast<int64_t>(RawRhs));
}

void Cpu::OpShiftRotate(Opcode Op, const DecodedInsn& Insn) {
    uint64_t Src = ReadGpr(Insn.Rs);
    uint64_t Amount = ThirdOperand(*this, Insn) & 63ULL;
    Fr &= ~(StatusFlags::Carry | StatusFlags::Overflow | StatusFlags::Zero |
            StatusFlags::Sign | StatusFlags::Equal | StatusFlags::Greater);
    if (Amount == 0) {
        SetZeroSign(Src);
        return;
    }
    uint64_t Result = Src;
    uint64_t LastCarry = 0;
    switch (Op) {
        case Opcode::LSH: {
            uint64_t Word = Src;
            for (uint64_t K = 0; K < Amount; ++K) {
                LastCarry = (Word >> 63) & 1ULL;
                Word <<= 1;
            }
            Result = Word;
            break;
        }
        case Opcode::RSH: {
            uint64_t Word = Src;
            for (uint64_t K = 0; K < Amount; ++K) {
                LastCarry = Word & 1ULL;
                Word >>= 1;
            }
            Result = Word;
            break;
        }
        case Opcode::ROR:
            Result = (Src >> Amount) | (Src << (static_cast<uint64_t>(64 - Amount)));
            LastCarry = Result & 1ULL;
            break;
        case Opcode::ROL:
            Result = (Src << Amount) | (Src >> (static_cast<uint64_t>(64 - Amount)));
            LastCarry = (Result >> 63) & 1ULL;
            break;
        default:
            throw std::runtime_error("Unknown shift opcode");
    }
    WriteGpr(Insn.Rd, Result);
    if (LastCarry != 0) Fr |= StatusFlags::Carry;
    SetZeroSign(Result);
}

void Cpu::OpSwapRegs(const DecodedInsn& Insn) {
    uint64_t A = ReadGpr(Insn.Rd);
    uint64_t B = ReadGpr(Insn.Rs);
    WriteGpr(Insn.Rd, B);
    WriteGpr(Insn.Rs, A);
}

void Cpu::OpCopyReg(const DecodedInsn& Insn) {
    WriteGpr(Insn.Rd, ReadGpr(Insn.Rs));
}

void Cpu::OpPushReg(const DecodedInsn& Insn) {
    Push64(ReadGpr(Insn.Rd));
}

void Cpu::OpPushImm(const DecodedInsn& Insn) {
    Push64(Insn.Imm1);
}

void Cpu::OpPopReg(const DecodedInsn& Insn) {
    WriteGpr(Insn.Rd, Pop64());
}

void Cpu::OpLoad(const DecodedInsn& Insn) {
    const uint64_t Addr = ResolveScalarMemAddr(*this, Insn);
    SecurityCheckMemAccess(*this, Addr, 8);
    WriteGpr(Insn.Rd, Read64(Addr));
}

void Cpu::OpStore(const DecodedInsn& Insn) {
    const uint64_t Addr = ResolveScalarMemAddr(*this, Insn);
    SecurityCheckMemAccess(*this, Addr, 8);
    Write64(Addr, ReadGpr(Insn.Rd));
}

void Cpu::OpPrefetchInsn(const DecodedInsn& Insn) {
    const uint64_t Addr = ResolveScalarMemAddr(*this, Insn);
    const uint64_t Phys =
        TranslateVirtualAddress(Addr, PagingProfile::MemoryAccessKind::Read);
    if (DCache.IsCacheable(Phys)) {
        DCache.PrefetchLine(Phys);
    }
}

void Cpu::OpInInsn(const DecodedInsn& Insn) {
    const uint64_t Port =
        Insn.Imm1Flag ? Insn.Imm1 : (static_cast<uint64_t>(Insn.Disp) & 0xFFULL);
    if (Port >= IoPorts) {
        throw std::runtime_error("IN port out of range");
    }
    WriteGpr(Insn.Rd, static_cast<uint64_t>(Ports[Port]));
}

void Cpu::OpOutInsn(const DecodedInsn& Insn) {
    const uint64_t Port =
        Insn.Imm1Flag ? Insn.Imm1 : (static_cast<uint64_t>(Insn.Disp) & 0xFFULL);
    if (Port >= IoPorts) {
        throw std::runtime_error("OUT port out of range");
    }
    const uint8_t Value = static_cast<uint8_t>(ReadGpr(Insn.Rs) & 0xFFULL);
    Ports[Port] = Value;
}

void Cpu::OpSatAdd(const DecodedInsn& Insn) {
    const int64_t Lhs = static_cast<int64_t>(ReadGpr(Insn.Rs));
    const int64_t Rhs =
        Insn.Imm1Flag ? static_cast<int64_t>(Insn.Imm1)
                       : static_cast<int64_t>(ReadGpr(Insn.Rt));
    const int64_t Result = SignedAddSaturated(Lhs, Rhs);
    WriteGpr(Insn.Rd, static_cast<uint64_t>(Result));
    Fr &= ~(StatusFlags::Carry | StatusFlags::Overflow | StatusFlags::Equal |
            StatusFlags::Greater);
    SetZeroSign(static_cast<uint64_t>(Result));
}

void Cpu::OpSatMul(const DecodedInsn& Insn) {
    const int64_t Lhs = static_cast<int64_t>(ReadGpr(Insn.Rs));
    const int64_t Rhs =
        Insn.Imm1Flag ? static_cast<int64_t>(Insn.Imm1)
                       : static_cast<int64_t>(ReadGpr(Insn.Rt));
    const int64_t Result = SignedMulSaturated(Lhs, Rhs);
    WriteGpr(Insn.Rd, static_cast<uint64_t>(Result));
    Fr &= ~(StatusFlags::Carry | StatusFlags::Overflow | StatusFlags::Equal |
            StatusFlags::Greater);
    SetZeroSign(static_cast<uint64_t>(Result));
}

void Cpu::OpIncDec(Opcode Op, const DecodedInsn& Insn) {
    uint64_t Value = ReadGpr(Insn.Rd);
    if (Op == Opcode::INC) {
        uint64_t Result = Value + 1;
        WriteGpr(Insn.Rd, Result);
        SetFlagsAfterAdd(Value, 1, Result);
        return;
    }
    uint64_t Result = Value - 1;
    WriteGpr(Insn.Rd, Result);
    SetFlagsAfterSub(Value, 1, Result);
}

void Cpu::OpBranch(const DecodedInsn& Insn) {
    const bool FarTarget = Insn.Imm1Flag;
    bool Take = false;
    switch (Insn.Op) {
        case Opcode::JMP: Take = true; break;
        case Opcode::JE: Take = (Fr & StatusFlags::Equal) != 0; break;
        case Opcode::JNE: Take = (Fr & StatusFlags::Equal) == 0; break;
        case Opcode::JZ: Take = (Fr & StatusFlags::Zero) != 0; break;
        case Opcode::JNZ: Take = (Fr & StatusFlags::Zero) == 0; break;
        case Opcode::JC: Take = (Fr & StatusFlags::Carry) != 0; break;
        case Opcode::JNC: Take = (Fr & StatusFlags::Carry) == 0; break;
        case Opcode::JLT: Take = (Fr & StatusFlags::Sign) != 0; break;
        case Opcode::JGT: Take = (Fr & StatusFlags::Greater) != 0; break;
        case Opcode::JO: Take = (Fr & StatusFlags::Overflow) != 0; break;
        case Opcode::JNO: Take = (Fr & StatusFlags::Overflow) == 0; break;
        case Opcode::CALL:
            Take = true;
            break;
        default: break;
    }
    if (!Take) {
        return;
    }
    if (Insn.Op == Opcode::CALL) {
        const uint64_t Target = FarTarget ? Insn.Imm1 : 0;
        if (!FarTarget) {
            throw std::runtime_error("CALL requires absolute target with mode tag (use label)");
        }
        OpCallEnter(Target);
        return;
    }
    if (FarTarget) {
        Ic = Insn.Imm1;
        return;
    }
    Ic = static_cast<uint64_t>(static_cast<int64_t>(Insn.Address) +
                               static_cast<int64_t>(Insn.Disp));
}

void Cpu::OpSyscallInsn(const DecodedInsn& Insn) {
    if (SyscallFrameDepth >= MaxSyscallNestDepth) {
        throw std::runtime_error("Honeycomb SYSCALL frame nesting limit exceeded");
    }
    if (MmioSyscallProgramCounter == 0) {
        throw std::runtime_error(
            "Honeycomb SYSCALL dispatched with ClearedMmioSyscallEntry (program SyscallHandlerIc first)");
    }
    const uint64_t Number = Insn.Imm1Flag ? Insn.Imm1 : 0ULL;
    SecurityCheckSyscall(*this, Number);
    MmioLastSyscallNumberRegister = Number;
    Push64(Ic);
    Push64(Fr);
    Fr &= ~(StatusFlags::Interrupt | StatusFlags::UserMode);
    Ic = MmioSyscallProgramCounter;
    SyscallFrameDepth++;
}

void Cpu::OpSysretInsn() {
    if (SyscallFrameDepth == 0) {
        throw std::runtime_error(
            "Honeycomb SYSRET reached without OutstandingSyscallFrame (use SYSRET only after SYSCALL)");
    }
    const uint64_t NewFr = Pop64();
    const uint64_t NewIc = Pop64();
    Fr = NewFr;
    Ic = NewIc;
    SyscallFrameDepth--;
}

void Cpu::Execute(const DecodedInsn& Insn) {
    switch (Insn.Op) {
        case Opcode::NOP: break;
        case Opcode::HLT: Halted = true; break;
        case Opcode::WAIT:
            Devices::Service(*this);
            break;
        case Opcode::Pause: break;
        case Opcode::EI:
            RequireSupervisor("Honeycomb EI is supervisor-only");
            Fr |= StatusFlags::Interrupt;
            break;
        case Opcode::DI:
            RequireSupervisor("Honeycomb DI is supervisor-only");
            Fr &= ~StatusFlags::Interrupt;
            break;
        case Opcode::PERFMON: OpPerfmon(Insn); break;
        case Opcode::EnablePaging: OpEnablePaging(); break;
        case Opcode::DisablePaging: OpDisablePaging(); break;
        case Opcode::EnableUser: OpEnableUser(); break;
        case Opcode::DisableUser: OpDisableUser(); break;
        case Opcode::EnableSoftwareTlbRefill: OpEnableSoftwareTlbRefill(); break;
        case Opcode::DisableSoftwareTlbRefill: OpDisableSoftwareTlbRefill(); break;
        case Opcode::TlbInsert: OpTlbInsert(); break;
        case Opcode::WireTlbEntry: OpWireTlbEntry(Insn); break;
        case Opcode::INT: {
            const uint64_t Imm = Insn.Imm1Flag ? Insn.Imm1 : (Insn.Disp & 0xFF);
            DeliverInterruptVector(static_cast<uint8_t>(Imm));
            break;
        }
        case Opcode::IRET: OpIret(); break;
        case Opcode::SYSCALL:
            OpSyscallInsn(Insn);
            break;
        case Opcode::SYSRET:
            OpSysretInsn();
            break;
        case Opcode::ADD: OpAdd(Insn); break;
        case Opcode::SUB: OpSub(Insn); break;
        case Opcode::MUL: OpMul(Insn); break;
        case Opcode::DIV: OpDiv(Insn); break;
        case Opcode::MOD: OpMod(Insn); break;
        case Opcode::AND: OpAnd(Insn); break;
        case Opcode::OR: OpOr(Insn); break;
        case Opcode::XOR: OpXor(Insn); break;
        case Opcode::NOT: OpNot(Insn); break;
        case Opcode::RSH:
        case Opcode::LSH:
        case Opcode::ROR:
        case Opcode::ROL: OpShiftRotate(Insn.Op, Insn); break;
        case Opcode::CMP: OpCmp(Insn); break;
        case Opcode::LOAD: OpLoad(Insn); break;
        case Opcode::STORE: OpStore(Insn); break;
        case Opcode::PREFETCH: OpPrefetchInsn(Insn); break;
        case Opcode::IN: OpInInsn(Insn); break;
        case Opcode::OUT: OpOutInsn(Insn); break;
        case Opcode::SATADD: OpSatAdd(Insn); break;
        case Opcode::SATMUL: OpSatMul(Insn); break;
        case Opcode::ATLOAD: OpLoad(Insn); break;
        case Opcode::ATSTORE: OpStore(Insn); break;
        case Opcode::ATAND:
        case Opcode::ATOR:
        case Opcode::ATNOT:
        case Opcode::ATXOR:
        case Opcode::ATADD:
        case Opcode::ATSUB:
        case Opcode::ATMUL:
        case Opcode::ATDIV:
        case Opcode::ATFADD:
        case Opcode::ATFSUB:
        case Opcode::ATCAS:
        case Opcode::ATCMP:
        case Opcode::ATINC:
        case Opcode::ATDEC:
        case Opcode::ATSWAP:
        case Opcode::ATREDUCE:
            ExecuteAtomicOpcode(Insn);
            break;
        case Opcode::FLOAD:
        case Opcode::FSTORE:
            ExecuteFsxOpcode(Insn);
            break;
        case Opcode::FADD:
        case Opcode::FSUB:
        case Opcode::FMUL:
        case Opcode::FDIV:
        case Opcode::FMOD:
        case Opcode::FMA:
        case Opcode::FCOPY:
        case Opcode::FCMP:
        case Opcode::FSQRT:
        case Opcode::FABS:
        case Opcode::FNEG:
        case Opcode::RECIP:
        case Opcode::RSQRT:
        case Opcode::FRSH:
        case Opcode::FLSH:
        case Opcode::FROL:
        case Opcode::FROR:
            ExecuteFsxOpcode(Insn);
            break;
        case Opcode::FTOI:
            WriteGpr(Insn.Rd, static_cast<uint64_t>(
                static_cast<int64_t>(ReadFsReg(*this, Insn.Rs))));
            break;
        case Opcode::ITOF:
            WriteFsReg(*this, Insn.Rd, static_cast<double>(static_cast<int64_t>(ReadGpr(Insn.Rs))));
            break;
        case Opcode::XLOAD:
        case Opcode::XSTORE:
        case Opcode::VLOAD:
        case Opcode::VSTORE:
        case Opcode::XLOAD128:
        case Opcode::XSTORE128:
        case Opcode::XCOPY:
        case Opcode::XSWAP128:
        case Opcode::XADD:
        case Opcode::XSUB:
        case Opcode::XMUL:
        case Opcode::XDIV:
        case Opcode::XMOD:
        case Opcode::XCMP:
        case Opcode::XAND:
        case Opcode::XXOR:
        case Opcode::XNOT:
        case Opcode::XRSH:
        case Opcode::XLSH:
        case Opcode::XROR:
        case Opcode::XROL:
        case Opcode::XCRYPT:
        case Opcode::XDECRYPT:
        case Opcode::XGEN:
        case Opcode::VADD:
        case Opcode::VSUB:
        case Opcode::VMUL:
        case Opcode::VDIV:
        case Opcode::VFMA:
        case Opcode::VFMSUB:
        case Opcode::VFNMADD:
        case Opcode::VFNMSUB:
        case Opcode::VBROADCAST:
        case Opcode::VMIN:
        case Opcode::VMAX:
        case Opcode::VABS:
        case Opcode::VNEG:
        case Opcode::VSQRT:
        case Opcode::VRCP:
        case Opcode::VRSQRT:
            ExecuteSsxOpcode(Insn);
            break;
        case Opcode::VDOTP:
        case Opcode::MATMUL:
        case Opcode::GEMM_OFFLOAD:
        case Opcode::TENSOR_LOAD:
        case Opcode::TENSOR_STORE:
        case Opcode::CONV2D:
        case Opcode::DOTP_ACC:
        case Opcode::QDOT:
        case Opcode::QGEMM:
            ExecuteMatrixOpcode(Insn);
            break;
        case Opcode::XSWAP:
            ExecuteSsxOpcode(Insn);
            break;
        case Opcode::INC:
        case Opcode::DEC: OpIncDec(Insn.Op, Insn); break;
        case Opcode::JMP: case Opcode::JE: case Opcode::JNE: case Opcode::JZ:
        case Opcode::JNZ: case Opcode::JC: case Opcode::JNC: case Opcode::JLT:
        case Opcode::JGT: case Opcode::JO: case Opcode::JNO: case Opcode::CALL:
            OpBranch(Insn); break;
        case Opcode::RET:
            OpRetLeave();
            break;
        case Opcode::SWAP: OpSwapRegs(Insn); break;
        case Opcode::COPY: OpCopyReg(Insn); break;
        case Opcode::PUSH:
            if (Insn.Imm1Flag) {
                OpPushImm(Insn);
            } else {
                OpPushReg(Insn);
            }
            break;
        case Opcode::POP: OpPopReg(Insn); break;
        case Opcode::IGLOAD:
        case Opcode::IGSTORE:
        case Opcode::IWIND:
        case Opcode::XSPILL:
        case Opcode::XRELOAD:
        case Opcode::KWIND:
        case Opcode::KLOAD:
        case Opcode::KSTORE:
        case Opcode::KLOADI:
        case Opcode::KSTOREI:
        case Opcode::KROT:
        case Opcode::KRELOAD:
        case Opcode::KSPILL:
        case Opcode::SAVWIN:
        case Opcode::RESWIN:
        case Opcode::FLUSHWIN:
        case Opcode::RESTWIN:
        case Opcode::RDWIN:
        case Opcode::WRWIN:
        case Opcode::STREAM:
        case Opcode::STREAMOUT:
        case Opcode::STREAMADV:
        case Opcode::STREAMWAIT:
        case Opcode::STREAMFENCE:
        case Opcode::STREAMBIND:
        case Opcode::STREAMUNBIND:
        case Opcode::NUMABIND:
        case Opcode::NUMASTREAM:
        case Opcode::NUMAFENCE:
        case Opcode::NUMAADV:
        case Opcode::NUMAATOMIC:
        case Opcode::HINT_OP:
        case Opcode::HINT_BARRIER:
            ExecuteKBankOpcode(Insn);
            break;
        case Opcode::SKB_LOAD:
        case Opcode::SKB_STORE:
        case Opcode::SKB_BIND:
        case Opcode::SKB_UNBIND:
        case Opcode::SKB_FLUSH:
        case Opcode::SKB_INVALIDATE:
        case Opcode::SKB_PREFETCH:
        case Opcode::SKB_ZERO:
        case Opcode::SKB_AT_LOAD:
        case Opcode::SKB_AT_STORE:
        case Opcode::SKB_AT_ADD:
        case Opcode::SKB_AT_CAS:
        case Opcode::SKB_AT_SWAP:
        case Opcode::SKB_AT_REDUCE:
        case Opcode::SKB_PIN:
        case Opcode::SKB_UNPIN:
        case Opcode::SKB_MOVE:
        case Opcode::SKB_BULK_LOAD:
        case Opcode::SKB_BULK_STORE:
        case Opcode::SKB_BULK_ZERO:
        case Opcode::SKB_STATUS:
            ExecuteSkbOpcode(Insn);
            break;
        default:
            if (IsSecurityOpcode(Insn.Op)) {
                Honeycomb::ExecuteSecurityOpcode(*this, Insn);
                break;
            }
            if (IsNsxOpcode(Insn.Op)) {
                ExecuteNsxOpcode(Insn);
                break;
            }
            if (IsDaxOpcode(Insn.Op)) {
                ExecuteDaxOpcode(Insn);
                break;
            }
            if (IsComplexOpcode(Insn.Op)) {
                ExecuteCplxOpcode(Insn);
                break;
            }
            throw std::runtime_error("Unimplemented opcode");
    }
}

void Cpu::Step() {
    if (Halted) return;
    DecodedInsn Insn = Fetch();
    if (Insn.CompactForm) {
        ExecuteCompact(Insn);
    } else {
        Execute(Insn);
    }
    PerfInstructionsRetired++;
    PerfCycles++;
    if (!Halted) {
        DispatchQueuedInterrupt();
    }
}

void Cpu::Run() {
    uint32_t ServiceCounter = 0;
    while (Running && !Halted) {
        Step();
        if (++ServiceCounter >= 512) {
            ServiceCounter = 0;
            Devices::Service(*this);
        }
    }
}

void Cpu::Halt() { Halted = true; }

void Cpu::DumpRegisters() const {
    auto Hex = [](uint64_t Value) {
        return static_cast<unsigned long long>(Value);
    };
    std::printf("R0=0x%016llX R1=0x%016llX R2=0x%016llX R3=0x%016llX\n",
                Hex(R0), Hex(R1), Hex(R2), Hex(R3));
    std::printf("R8=0x%016llX R9=0x%016llX R10=0x%016llX IC=0x%016llX FR=0x%016llX\n",
                Hex(R8), Hex(R9), Hex(R10), Hex(Ic), Hex(Fr));
}
