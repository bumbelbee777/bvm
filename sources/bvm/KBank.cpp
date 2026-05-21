#include "Shared.h"
#include "KBankProfile.h"
#include "RegFile.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

using namespace Honeycomb;

namespace {

constexpr uint32_t KAddrModeShift = 28;
constexpr uint64_t KAddrOffsetMask = 0x0FFFFFFFULL;

uint64_t DispOffsetLow(const DecodedInsn& Insn) {
    return static_cast<uint64_t>(Insn.Disp) & KAddrOffsetMask;
}

uint8_t KAddrMode(const DecodedInsn& Insn) {
    return static_cast<uint8_t>((static_cast<uint32_t>(Insn.Disp) >> KAddrModeShift) & 3u);
}

} // namespace

uint64_t Cpu::ReadIndexReg(uint8_t Code) const {
    switch (Code) {
        case static_cast<uint8_t>(Reg::I0): return I0;
        case static_cast<uint8_t>(Reg::I1): return I1;
        case static_cast<uint8_t>(Reg::I2): return I2;
        case static_cast<uint8_t>(Reg::I3): return I3;
        default:
            throw std::runtime_error("Operand is not an index register (I0..I3)");
    }
}

void Cpu::WriteIndexReg(uint8_t Code, uint64_t Value) {
    switch (Code) {
        case static_cast<uint8_t>(Reg::I0): I0 = Value; return;
        case static_cast<uint8_t>(Reg::I1): I1 = Value; return;
        case static_cast<uint8_t>(Reg::I2): I2 = Value; return;
        case static_cast<uint8_t>(Reg::I3): I3 = Value; return;
        default:
            throw std::runtime_error("Operand is not an index register (I0..I3)");
    }
}

uint64_t Cpu::ReadKReg(uint64_t Index) const {
    uint64_t Bound = 0;
    if (TryReadSkbBoundK(Index, Bound)) {
        return Bound;
    }
    if (TryReadNumaBoundK(Index, Bound)) {
        return Bound;
    }
    if (Index >= KBank.size()) {
        throw std::out_of_range("K-Bank read out of range");
    }
    return KBank[Index];
}

void Cpu::WriteKReg(uint64_t Index, uint64_t Value) {
    if (TryWriteSkbBoundK(Index, Value)) {
        return;
    }
    if (TryWriteNumaBoundK(Index, Value)) {
        return;
    }
    if (Index >= KBank.size()) {
        throw std::out_of_range("K-Bank write out of range");
    }
    KBank[Index] = Value;
}

uint64_t Cpu::ComputeKAddress(const DecodedInsn& Insn, uint64_t Index) const {
    const uint64_t Offset = DispOffsetLow(Insn);
    const uint64_t BaseIndex = Index + Offset;
    uint64_t Addr = 0;

    switch (KAddrMode(Insn)) {
        case 0: // flat
            Addr = Wind + BaseIndex;
            break;
        case 1: { // windowed
            const uint64_t Ws = KBankProfile::EffectiveWindowSize(WinSz);
            Addr = (Cwp * Ws) + ((Wind + Index + Offset) & (Ws - 1));
            break;
        }
        case 2: { // rotating
            if (RotSz == 0) {
                throw std::runtime_error("KROT/rotate mode requires ROTSZ != 0");
            }
            const uint64_t Rel = (Wind + Index + Offset) % RotSz;
            Addr = RotBase + Rel;
            break;
        }
        default:
            throw std::runtime_error("Reserved K-Bank address mode (disp[29:28] == 11)");
    }

    return Addr & KMask;
}

void Cpu::OpKwind(const DecodedInsn& Insn) {
    if (Insn.Imm1Flag) {
        Wind = Insn.Imm1;
        return;
    }
    Wind = ReadGpr(Insn.Rs);
}

void Cpu::OpKload(const DecodedInsn& Insn) {
    const uint64_t Addr = ComputeKAddress(Insn, ReadIndexReg(Insn.Rs));
    WriteGpr(Insn.Rd, ReadKReg(Addr));
}

void Cpu::OpKstore(const DecodedInsn& Insn) {
    const uint64_t Addr = ComputeKAddress(Insn, ReadIndexReg(Insn.Rs));
    WriteKReg(Addr, ReadGpr(Insn.Rd));
}

void Cpu::OpKloadi(const DecodedInsn& Insn) {
    const uint64_t ImmIndex = Insn.Imm1Flag ? (Insn.Imm1 & 0x1FULL) : (Insn.Disp & 0x1FULL);
    const uint64_t Addr = (Wind + ImmIndex) & KMask;
    WriteGpr(Insn.Rd, ReadKReg(Addr));
}

void Cpu::OpKstorei(const DecodedInsn& Insn) {
    const uint64_t ImmIndex = Insn.Imm1Flag ? (Insn.Imm1 & 0x1FULL) : (Insn.Disp & 0x1FULL);
    const uint64_t Addr = (Wind + ImmIndex) & KMask;
    WriteKReg(Addr, ReadGpr(Insn.Rd));
}

void Cpu::OpKrot(const DecodedInsn& Insn) {
    DecodedInsn RotInsn = Insn;
    RotInsn.Disp = static_cast<int32_t>((static_cast<uint32_t>(Insn.Disp) & ~0xF0000000u) |
                                          (2u << KAddrModeShift));
    const uint64_t Addr = ComputeKAddress(RotInsn, ReadIndexReg(Insn.Rs));
    WriteGpr(Insn.Rd, ReadKReg(Addr));
}

void Cpu::OpIgload(const DecodedInsn& Insn) {
    const uint64_t GprIndex = (Wind + ReadIndexReg(Insn.Rs) + DispOffsetLow(Insn)) & 0x1FULL;
    WriteGpr(Insn.Rd, ReadGpr(static_cast<uint8_t>(GprIndex)));
}

void Cpu::OpIgstore(const DecodedInsn& Insn) {
    const uint64_t GprIndex = (Wind + ReadIndexReg(Insn.Rs) + DispOffsetLow(Insn)) & 0x1FULL;
    WriteGpr(static_cast<uint8_t>(GprIndex), ReadGpr(Insn.Rd));
}

void Cpu::OpKreload(const DecodedInsn& Insn) {
    const uint64_t Base = ComputeKAddress(Insn, ReadIndexReg(Insn.Rs));
    Uint128 Value = ReadKReg(Base);
    Value |= static_cast<Uint128>(ReadKReg(Base + 1)) << 64;
    WriteXReg(*this, Insn.Rd, Value);
}

void Cpu::OpKspill(const DecodedInsn& Insn) {
    const uint64_t Base = ComputeKAddress(Insn, ReadIndexReg(Insn.Rs));
    const Uint128 Value = ReadXReg(*this, Insn.Rd);
    WriteKReg(Base, static_cast<uint64_t>(Value));
    WriteKReg(Base + 1, static_cast<uint64_t>(Value >> 64));
}

void Cpu::OpXspill(const DecodedInsn& Insn) {
    const uint64_t Which = Insn.Imm1Flag ? Insn.Imm1 : DispOffsetLow(Insn);
    if (Which > 1) {
        throw std::runtime_error("XSPILL which must be 0 or 1");
    }
    const Uint128 Xv = ReadXReg(*this, Insn.Rd);
    const uint64_t Half = Which ? static_cast<uint64_t>(Xv >> 64)
                                : static_cast<uint64_t>(Xv);
    WriteGpr(Insn.Rs, Half);
}

void Cpu::OpXreload(const DecodedInsn& Insn) {
    const uint64_t Which = Insn.Imm1Flag ? Insn.Imm1 : DispOffsetLow(Insn);
    if (Which > 1) {
        throw std::runtime_error("XRELOAD which must be 0 or 1");
    }
    const Uint128 Src = ReadXReg(*this, Insn.Rs);
    Uint128 Dest = ReadXReg(*this, Insn.Rd);
    if (Which == 0) {
        Dest = (Dest & ~static_cast<Uint128>(0xFFFFFFFFFFFFFFFFULL)) |
               static_cast<Uint128>(static_cast<uint64_t>(Src));
    } else {
        Dest = (Dest & static_cast<Uint128>(0xFFFFFFFFFFFFFFFFULL)) |
               (Src & (static_cast<Uint128>(0xFFFFFFFFFFFFFFFFULL) << 64));
    }
    WriteXReg(*this, Insn.Rd, Dest);
}

void Cpu::OpSavwin() {
    if (CanSave == 0) {
        throw std::runtime_error("Register window overflow (CANSAVE == 0)");
    }
    Cwp = (Cwp + 1) & 0xFF;
    CanSave -= 1;
    CanRestore += 1;
}

void Cpu::OpReswin() {
    if (CanRestore == 0) {
        throw std::runtime_error("Register window underflow (CANRESTORE == 0)");
    }
    Cwp = (Cwp == 0) ? 0xFF : (Cwp - 1);
    CanSave += 1;
    CanRestore -= 1;
}

void Cpu::OpFlushwin(const DecodedInsn& Insn) {
    const uint64_t Count = Insn.Imm1Flag ? Insn.Imm1 : DispOffsetLow(Insn);
    const uint64_t Ws = KBankProfile::EffectiveWindowSize(WinSz);
    for (uint64_t N = 0; N < Count; ++N) {
        const uint64_t Oldest = (Cwp + CanRestore + N) & 0xFF;
        const uint64_t Dst = WindowSpillBase + Oldest * Ws * 8;
        for (uint64_t Slot = 0; Slot < Ws; ++Slot) {
            const uint64_t Src = (Oldest * Ws + Slot) & KMask;
            Write64(Dst + Slot * 8, ReadKReg(Src));
        }
    }
    OtherWin += Count;
}

void Cpu::OpRestwin(const DecodedInsn& Insn) {
    const uint64_t Count = Insn.Imm1Flag ? Insn.Imm1 : DispOffsetLow(Insn);
    const uint64_t Ws = KBankProfile::EffectiveWindowSize(WinSz);
    for (uint64_t N = 0; N < Count; ++N) {
        const uint64_t Oldest = (Cwp + N) & 0xFF;
        const uint64_t Src = WindowSpillBase + Oldest * Ws * 8;
        for (uint64_t Slot = 0; Slot < Ws; ++Slot) {
            const uint64_t Dst = (Oldest * Ws + Slot) & KMask;
            WriteKReg(Dst, Read64(Src + Slot * 8));
        }
    }
}

void Cpu::OpRdwin(const DecodedInsn& Insn) {
    const uint64_t Sel = Insn.Imm1Flag ? Insn.Imm1 : DispOffsetLow(Insn);
    uint64_t Value = 0;
    switch (Sel) {
        case 0: Value = Cwp; break;
        case 1: Value = CanSave; break;
        case 2: Value = CanRestore; break;
        case 3: Value = OtherWin; break;
        case 4: Value = Wind; break;
        case 5: Value = WinSz; break;
        case 6: Value = RotBase; break;
        case 7: Value = RotSz; break;
        default: Value = 0; break;
    }
    WriteGpr(Insn.Rd, Value);
}

void Cpu::OpWrwin(const DecodedInsn& Insn) {
    const uint64_t Sel = Insn.Imm1Flag ? Insn.Imm1 : DispOffsetLow(Insn);
    const uint64_t Value = ReadGpr(Insn.Rd);
    switch (Sel) {
        case 0: Cwp = Value & 0xFF; break;
        case 1: CanSave = Value; break;
        case 2: CanRestore = Value; break;
        case 3: OtherWin = Value; break;
        case 4: Wind = Value; break;
        case 5: {
            const uint64_t Low = Value & KBankProfile::WinSzWindowMask;
            WinSz = (WinSz & ~KBankProfile::WinSzWindowMask) | (Low != 0 ? Low : 1);
            break;
        }
        case 6: RotBase = Value & KMask; break;
        case 7: RotSz = Value; break;
        default: break;
    }
}

void Cpu::MemcpyToKBank(uint64_t DstKIndex, uint64_t SrcAddr, uint64_t ByteCount) {
    for (uint64_t Off = 0; Off < ByteCount; Off += 8) {
        const uint64_t Ki = (DstKIndex + (Off / 8)) & KMask;
        WriteKReg(Ki, Read64(SrcAddr + Off));
    }
}

void Cpu::MemcpyFromKBank(uint64_t SrcKIndex, uint64_t DstAddr, uint64_t ByteCount) {
    for (uint64_t Off = 0; Off < ByteCount; Off += 8) {
        const uint64_t Ki = (SrcKIndex + (Off / 8)) & KMask;
        Write64(DstAddr + Off, ReadKReg(Ki));
    }
}

void Cpu::OpStream(const DecodedInsn& Insn, bool ToMemory) {
    const uint64_t Tag = static_cast<uint64_t>(Insn.Rt);
    const uint64_t Len = DispOffsetLow(Insn);
    const uint64_t MemAddr = Insn.Imm1Flag ? Insn.Imm1 : 0;
    if (!Insn.Imm1Flag) {
        throw std::runtime_error("STREAM/STREAMOUT require imm1 memory address");
    }
    const uint64_t KBase = Wind;
    if (ToMemory) {
        MemcpyFromKBank(KBase, MemAddr, Len);
    } else {
        MemcpyToKBank(KBase, MemAddr, Len);
    }
    if (Tag < StreamTagDone.size()) {
        StreamTagDone[Tag] = true;
    }
}

void Cpu::OpStreamWait(const DecodedInsn& Insn) {
    const uint64_t Tag = Insn.Imm1Flag ? Insn.Imm1 : DispOffsetLow(Insn);
    if (Tag < StreamTagDone.size() && !StreamTagDone[Tag]) {
        throw std::runtime_error("STREAMWAIT: tag not complete (unexpected in sync BVM)");
    }
}

void Cpu::ExecuteKBankOpcode(const DecodedInsn& Insn) {
    switch (Insn.Op) {
        case Opcode::KWIND:
        case Opcode::IWIND:
            OpKwind(Insn);
            break;
        case Opcode::KLOAD:
            OpKload(Insn);
            break;
        case Opcode::KSTORE:
            OpKstore(Insn);
            break;
        case Opcode::KLOADI:
            OpKloadi(Insn);
            break;
        case Opcode::KSTOREI:
            OpKstorei(Insn);
            break;
        case Opcode::KROT:
            OpKrot(Insn);
            break;
        case Opcode::IGLOAD:
            OpIgload(Insn);
            break;
        case Opcode::IGSTORE:
            OpIgstore(Insn);
            break;
        case Opcode::KRELOAD:
            OpKreload(Insn);
            break;
        case Opcode::KSPILL:
            OpKspill(Insn);
            break;
        case Opcode::XSPILL:
            OpXspill(Insn);
            break;
        case Opcode::XRELOAD:
            OpXreload(Insn);
            break;
        case Opcode::SAVWIN:
            OpSavwin();
            break;
        case Opcode::RESWIN:
            OpReswin();
            break;
        case Opcode::FLUSHWIN:
            OpFlushwin(Insn);
            break;
        case Opcode::RESTWIN:
            OpRestwin(Insn);
            break;
        case Opcode::RDWIN:
            OpRdwin(Insn);
            break;
        case Opcode::WRWIN:
            OpWrwin(Insn);
            break;
        case Opcode::STREAM:
            OpStream(Insn, false);
            break;
        case Opcode::STREAMOUT:
            OpStream(Insn, true);
            break;
        case Opcode::STREAMADV:
            Wind = (Wind + KBankProfile::EffectiveWindowSize(WinSz)) & KMask;
            break;
        case Opcode::STREAMWAIT:
            OpStreamWait(Insn);
            break;
        case Opcode::STREAMFENCE:
            std::fill(StreamTagDone.begin(), StreamTagDone.end(), true);
            break;
        case Opcode::STREAMBIND:
            OpStreamBind(Insn);
            break;
        case Opcode::STREAMUNBIND:
            OpStreamUnbind(Insn);
            break;
        case Opcode::NUMABIND:
            OpNumaBind(Insn);
            break;
        case Opcode::NUMASTREAM:
            OpNumaStream(Insn, Insn.Opmode != 0);
            break;
        case Opcode::NUMAFENCE:
            OpNumaFence();
            break;
        case Opcode::NUMAADV:
            OpNumaAdv(Insn);
            break;
        case Opcode::NUMAATOMIC:
            OpNumaAtomic(Insn);
            break;
        case Opcode::HINT_OP:
            OpHintInsn(Insn);
            break;
        case Opcode::HINT_BARRIER:
            OpHintBarrier();
            break;
        default:
            throw std::runtime_error("Unknown K-Bank opcode");
    }
}
