#include "Shared.h"
#include "Machine.h"
#include "PhysMap.h"
#include "NumaProfile.h"
#include "SkbProfile.h"
#include "RegFile.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <vector>

using namespace Honeycomb;

#pragma pack(push, 1)
struct SkbLineMeta {
    uint8_t State = static_cast<uint8_t>(SkbProfile::LineState::Invalid);
    uint8_t OwnerMask = 0;
    uint8_t PendingMask = 0;
    uint8_t Version = 0;
    uint8_t LruAge = 0;
    uint8_t HomeNode = 0;
    uint8_t LockCount = 0;
    uint8_t Flags = 0;
    uint8_t SharerVector = 0;
    uint8_t PendingVector = 0;
    uint8_t OwnerCore = 0;
    uint8_t DirState = 0;
    uint8_t LastAccess = 0;
    uint8_t PrefetchHint = 0;
    uint8_t MigrationCount = 0;
    uint8_t Reserved = 0;
    uint64_t DramBackAddr = 0;
    uint64_t Tag = 0;
};
#pragma pack(pop)

std::vector<uint8_t>& SkbDataVec() {
    static std::vector<uint8_t> Storage;
    if (Storage.empty()) {
        Storage.assign(GetSkbByteSize(), 0);
    }
    return Storage;
}

std::vector<SkbLineMeta>& SkbMetaVec() {
    static std::vector<SkbLineMeta> Storage;
    const size_t Lines = GetSkbByteSize() / SkbProfile::LineBytes;
    if (Storage.size() != Lines) {
        Storage.assign(Lines, SkbLineMeta{});
    }
    return Storage;
}

size_t SkbActiveLineCount() { return GetSkbByteSize() / SkbProfile::LineBytes; }
std::array<uint64_t, 8> SkbKBankBindLine = {};
std::array<bool, 8> SkbKBankBindActive = {};
uint8_t SkbActiveBindSlot = 0;
uint64_t SkbBindWind = 0;

void EnsureSkbStorage() {
    SkbDataVec();
    SkbMetaVec();
}

void ConfigureSkbStorage(size_t Bytes) {
    if (Bytes == 0 || Bytes > SkbProfile::SizeBytes ||
        (Bytes % SkbProfile::LineBytes) != 0) {
        throw std::runtime_error("Invalid SKB size");
    }
    SkbDataVec().assign(Bytes, 0);
    SkbMetaVec().assign(Bytes / SkbProfile::LineBytes, SkbLineMeta{});
}

namespace {

uint64_t ParseSkbLineImm(uint64_t Imm) {
    const uint64_t Domain = (Imm >> PhysMap::DomainShift) & PhysMap::DomainMask;
    if (Domain != SkbProfile::DomainNibble && Domain != 0) {
        throw std::runtime_error("SKB line address requires imm1[63:60] = 0010");
    }
    const uint64_t Line = (Imm >> 6) & 0x7FFFFULL;
    if (Line >= SkbActiveLineCount()) {
        throw std::out_of_range("SKB line index out of range");
    }
    return Line;
}

uint64_t LineByteOffset(const DecodedInsn& Insn) {
    if (Insn.Imm1Flag) {
        const uint64_t Domain = (Insn.Imm1 >> PhysMap::DomainShift) & PhysMap::DomainMask;
        if (Domain == SkbProfile::DomainNibble) {
            return Insn.Imm1 & (SkbProfile::LineBytes - 1);
        }
    }
    return static_cast<uint64_t>(Insn.Disp) & (SkbProfile::LineBytes - 1);
}

uint8_t* LineDataPtr(uint64_t Line) {
    EnsureSkbStorage();
    return SkbDataVec().data() + Line * SkbProfile::LineBytes;
}

uint64_t SkbReadWord(uint64_t Line, uint64_t Off) {
    uint64_t Word = 0;
    std::memcpy(&Word, LineDataPtr(Line) + Off, sizeof(Word));
    return SwapBE64(Word);
}

void SkbWriteWord(uint64_t Line, uint64_t Off, uint64_t Value) {
    Value = SwapBE64(Value);
    std::memcpy(LineDataPtr(Line) + Off, &Value, sizeof(Value));
}

void SetLineState(uint64_t Line, SkbProfile::LineState State) {
    auto& Meta = SkbMetaVec()[Line];
    Meta.State = static_cast<uint8_t>(State);
    Meta.DirState = static_cast<uint8_t>(State);
}

void TouchLine(uint64_t Line, SkbProfile::LineState State) {
    auto& Meta = SkbMetaVec()[Line];
    if (Meta.State == static_cast<uint8_t>(SkbProfile::LineState::Invalid)) {
        SetLineState(Line, State);
        Meta.OwnerMask = 1;
        Meta.SharerVector = 1;
        Meta.OwnerCore = 0;
    }
    Meta.LastAccess = static_cast<uint8_t>((Meta.LastAccess + 1) & 0xFF);
}

void EnsureExclusive(uint64_t Line) {
    auto& Meta = SkbMetaVec()[Line];
    if (Meta.State == static_cast<uint8_t>(SkbProfile::LineState::Invalid)) {
        std::memset(LineDataPtr(Line), 0, SkbProfile::LineBytes);
        SetLineState(Line, SkbProfile::LineState::Exclusive);
        Meta.OwnerMask = 1;
        Meta.SharerVector = 1;
        Meta.OwnerCore = 0;
        return;
    }
    if (Meta.State == static_cast<uint8_t>(SkbProfile::LineState::Shared)) {
        SetLineState(Line, SkbProfile::LineState::Exclusive);
        Meta.SharerVector = 1;
    }
}

constexpr uint8_t SsxRegCodeBase = 0x40;

uint8_t XRegSlotFromCode(uint8_t Code) {
    if (Code < SsxRegCodeBase || Code > SsxRegCodeBase + 15) {
        throw std::runtime_error("SKB 64-byte burst requires SSX register in rd");
    }
    return static_cast<uint8_t>(Code - SsxRegCodeBase);
}

} // namespace

bool Cpu::IsSkbPhysicalAddress(uint64_t Addr) {
    return PhysMap::CoversLocalSkb(Addr, 1);
}

uint64_t Cpu::ReadSkbPhysical(uint64_t Addr) const {
    const uint64_t Line = PhysMap::LocalSkbLineIndex(Addr);
    const uint64_t Off = PhysMap::LocalSkbByteInLine(Addr);
    uint64_t Value = 0;
    std::memcpy(&Value, SkbDataVec().data() + Line * SkbProfile::LineBytes + Off, sizeof(Value));
    return Value;
}

void Cpu::WriteSkbPhysical(uint64_t Addr, uint64_t Value) {
    const uint64_t Line = PhysMap::LocalSkbLineIndex(Addr);
    const uint64_t Off = PhysMap::LocalSkbByteInLine(Addr);
    std::memcpy(SkbDataVec().data() + Line * SkbProfile::LineBytes + Off, &Value, sizeof(Value));
    TouchLine(Line, SkbProfile::LineState::Modified);
    SetLineState(Line, SkbProfile::LineState::Modified);
}

uint64_t Cpu::ReadKPhysical(uint64_t Addr) const {
    const uint64_t Index = PhysMap::LocalKSlotIndex(Addr);
    const uint64_t Off = PhysMap::LocalKByteInSlot(Addr);
    uint64_t Value = 0;
    if (TryReadSkbBoundK(Index, Value)) {
        return Value;
    }
    if (Index >= KBank.size()) {
        throw std::out_of_range("K-Bank physical read out of range");
    }
    if (Off < 8) {
        return KBank[Index];
    }
    if (Off == 8 && Index < DaxMeta.size()) {
        const DaxTokenMeta& Meta = DaxMeta[Index];
        return (static_cast<uint64_t>(Meta.Valid) & 1ULL) |
               (static_cast<uint64_t>(Meta.Tag) << 8) |
               (static_cast<uint64_t>(Meta.RefCnt) << 16);
    }
    return 0;
}

void Cpu::WriteKPhysical(uint64_t Addr, uint64_t Value) {
    const uint64_t Index = PhysMap::LocalKSlotIndex(Addr);
    const uint64_t Off = PhysMap::LocalKByteInSlot(Addr);
    if (TryWriteSkbBoundK(Index, Value)) {
        return;
    }
    if (Index >= KBank.size()) {
        throw std::out_of_range("K-Bank physical write out of range");
    }
    if (Off < 8) {
        KBank[Index] = Value;
        return;
    }
    if (Off == 8 && Index < DaxMeta.size()) {
        DaxTokenMeta& Meta = DaxMeta[Index];
        Meta.Valid = (Value & 1) != 0;
        Meta.Tag = static_cast<uint8_t>((Value >> 8) & 0xFF);
        Meta.RefCnt = static_cast<uint8_t>((Value >> 16) & 0xFF);
    }
}

void Cpu::ResetSkb() {
    std::fill(SkbDataVec().begin(), SkbDataVec().end(), 0);
    SkbMetaVec().assign(SkbActiveLineCount(), SkbLineMeta{});
    SkbKBankBindLine.fill(0);
    SkbKBankBindActive.fill(false);
    SkbActiveBindSlot = 0;
    SkbBindWind = 0;
}

bool Cpu::TryReadSkbBoundK(uint64_t Index, uint64_t& Value) const {
    const uint64_t Ws = KBankProfile::EffectiveWindowSize(WinSz);
    for (uint8_t Slot = 0; Slot < SkbKBankBindActive.size(); ++Slot) {
        if (!SkbKBankBindActive[Slot]) {
            continue;
        }
        const uint64_t Base = static_cast<uint64_t>(Slot) * Ws;
        if (Index < Base || Index >= Base + Ws) {
            continue;
        }
        const uint64_t Line = SkbKBankBindLine[Slot];
        const uint64_t Off = (Index - Base) * 8;
        if (Off + 8 > SkbProfile::LineBytes) {
            continue;
        }
        Value = SkbReadWord(Line, Off);
        return true;
    }
    return false;
}

bool Cpu::TryWriteSkbBoundK(uint64_t Index, uint64_t Value) {
    const uint64_t Ws = KBankProfile::EffectiveWindowSize(WinSz);
    for (uint8_t Slot = 0; Slot < SkbKBankBindActive.size(); ++Slot) {
        if (!SkbKBankBindActive[Slot]) {
            continue;
        }
        const uint64_t Base = static_cast<uint64_t>(Slot) * Ws;
        if (Index < Base || Index >= Base + Ws) {
            continue;
        }
        const uint64_t Line = SkbKBankBindLine[Slot];
        const uint64_t Off = (Index - Base) * 8;
        if (Off + 8 > SkbProfile::LineBytes) {
            continue;
        }
        SkbWriteWord(Line, Off, Value);
        SetLineState(Line, SkbProfile::LineState::Modified);
        return true;
    }
    return false;
}

void Cpu::OpSkbLoad(const DecodedInsn& Insn) {
    if (!Insn.Imm1Flag) {
        throw std::runtime_error("SKB_LOAD requires imm1 line address");
    }
    const uint64_t Line = ParseSkbLineImm(Insn.Imm1);
    TouchLine(Line, SkbProfile::LineState::Shared);
    const uint64_t Off = LineByteOffset(Insn);
    uint8_t* LinePtr = LineDataPtr(Line);

    if (Insn.Opmode == SkbProfile::OpmodeSkbLine64) {
        if (Off != 0) {
            throw std::runtime_error("SKB_LOAD 64-byte burst requires line offset 0");
        }
        const uint8_t BaseSlot = XRegSlotFromCode(Insn.Rd);
        for (uint8_t Chunk = 0; Chunk < 4; ++Chunk) {
            Uint128 Value = 0;
            std::memcpy(&Value, LinePtr + Chunk * 16, 16);
            WriteXReg(*this, SsxRegCodeBase + BaseSlot + Chunk, Value);
        }
        return;
    }
    if (Insn.Opmode == SkbProfile::OpmodeSkbSsx16) {
        Uint128 Value = 0;
        std::memcpy(&Value, LinePtr + Off, 16);
        WriteXReg(*this, Insn.Rd, Value);
        return;
    }
    WriteGpr(Insn.Rd, SkbReadWord(Line, Off));
}

void Cpu::OpSkbStore(const DecodedInsn& Insn) {
    if (!Insn.Imm1Flag) {
        throw std::runtime_error("SKB_STORE requires imm1 line address");
    }
    const uint64_t Line = ParseSkbLineImm(Insn.Imm1);
    EnsureExclusive(Line);
    SetLineState(Line, SkbProfile::LineState::Modified);
    const uint64_t Off = LineByteOffset(Insn);
    uint8_t* LinePtr = LineDataPtr(Line);

    if (Insn.Opmode == SkbProfile::OpmodeSkbLine64) {
        if (Off != 0) {
            throw std::runtime_error("SKB_STORE 64-byte burst requires line offset 0");
        }
        const uint8_t BaseSlot = XRegSlotFromCode(Insn.Rd);
        for (uint8_t Chunk = 0; Chunk < 4; ++Chunk) {
            const Uint128 Value =
                ReadXReg(*this, SsxRegCodeBase + BaseSlot + Chunk);
            std::memcpy(LinePtr + Chunk * 16, &Value, 16);
        }
        return;
    }
    if (Insn.Opmode == SkbProfile::OpmodeSkbSsx16) {
        const Uint128 Value = ReadXReg(*this, Insn.Rd);
        std::memcpy(LinePtr + Off, &Value, 16);
        return;
    }
    SkbWriteWord(Line, Off, ReadGpr(Insn.Rd));
}

void Cpu::OpSkbZero(const DecodedInsn& Insn) {
    if (!Insn.Imm1Flag) {
        throw std::runtime_error("SKB_ZERO requires imm1 line address");
    }
    const uint64_t Line = ParseSkbLineImm(Insn.Imm1);
    std::memset(LineDataPtr(Line), 0, SkbProfile::LineBytes);
    SetLineState(Line, SkbProfile::LineState::Exclusive);
    SkbMetaVec()[Line].OwnerMask = 1;
}

void Cpu::OpSkbFlush(const DecodedInsn& Insn) {
    const uint64_t Line = ParseSkbLineImm(Insn.Imm1);
    auto& Meta = SkbMetaVec()[Line];
    if (Meta.State == static_cast<uint8_t>(SkbProfile::LineState::Modified) &&
        Meta.DramBackAddr != 0) {
        for (size_t Off = 0; Off < SkbProfile::LineBytes; Off += 8) {
            uint64_t Word = 0;
            std::memcpy(&Word, LineDataPtr(Line) + Off, sizeof(Word));
            Write64(Meta.DramBackAddr + Off, Word);
        }
    }
    SetLineState(Line, SkbProfile::LineState::Shared);
}

void Cpu::OpSkbInvalidate(const DecodedInsn& Insn) {
    const uint64_t Line = ParseSkbLineImm(Insn.Imm1);
    SkbMetaVec()[Line] = SkbLineMeta{};
    std::memset(LineDataPtr(Line), 0, SkbProfile::LineBytes);
}

void Cpu::OpSkbPin(const DecodedInsn& Insn, bool Pin) {
    const uint64_t Line = ParseSkbLineImm(Insn.Imm1);
    TouchLine(Line, SkbProfile::LineState::Pinned);
    auto& Meta = SkbMetaVec()[Line];
    if (Pin) {
        Meta.LockCount += 1;
        SetLineState(Line, SkbProfile::LineState::Pinned);
    } else if (Meta.LockCount > 0) {
        Meta.LockCount -= 1;
        if (Meta.LockCount == 0) {
            SetLineState(Line, SkbProfile::LineState::Exclusive);
        }
    }
}

void Cpu::OpSkbStatus(const DecodedInsn& Insn) {
    const uint64_t Line = ParseSkbLineImm(Insn.Imm1);
    WriteGpr(Insn.Rd, SkbMetaVec()[Line].State);
}

void Cpu::OpSkbBind(const DecodedInsn& Insn) {
    if (!Insn.Imm1Flag) {
        throw std::runtime_error("SKB_BIND requires imm1 line address");
    }
    const uint64_t Line = ParseSkbLineImm(Insn.Imm1);
    const uint8_t Slot = Insn.Rs & 0x7;
    SkbProfile::LineState BindState = SkbProfile::LineState::Shared;
    if (Insn.Opmode == SkbProfile::BindModeExclusive) {
        BindState = SkbProfile::LineState::Exclusive;
    } else if (Insn.Opmode == SkbProfile::BindModePinned) {
        BindState = SkbProfile::LineState::Pinned;
    }
    TouchLine(Line, BindState);
    SetLineState(Line, BindState);
    SkbKBankBindLine[Slot] = Line;
    SkbKBankBindActive[Slot] = true;
    SkbActiveBindSlot = Slot;
    SkbBindWind = static_cast<uint64_t>(Slot) * KBankProfile::EffectiveWindowSize(WinSz);
    Wind = SkbBindWind;
}

void Cpu::OpSkbUnbind(const DecodedInsn& Insn) {
    const uint8_t Slot = Insn.Rs & 0x7;
    SkbKBankBindActive[Slot] = false;
    if (SkbActiveBindSlot == Slot) {
        SkbActiveBindSlot = 0;
    }
}

void Cpu::OpSkbMove(const DecodedInsn& Insn) {
    if (!Insn.Imm1Flag || !Insn.Imm2Flag) {
        throw std::runtime_error("SKB_MOVE requires imm1 source and imm2 destination lines");
    }
    const uint64_t Src = ParseSkbLineImm(Insn.Imm1);
    const uint64_t DstLine = ParseSkbLineImm(Insn.Imm2);
    SkbMetaVec()[DstLine] = SkbMetaVec()[Src];
    SkbMetaVec()[DstLine].Tag = DstLine;
    std::memcpy(LineDataPtr(DstLine), LineDataPtr(Src), SkbProfile::LineBytes);
    SkbMetaVec()[Src] = SkbLineMeta{};
    std::memset(LineDataPtr(Src), 0, SkbProfile::LineBytes);
}

uint64_t Cpu::SkbAtomicReadWord(uint64_t Line, uint64_t Off) const {
    return SkbReadWord(Line, Off);
}

void Cpu::SkbAtomicWriteWord(uint64_t Line, uint64_t Off, uint64_t Value) {
    SkbWriteWord(Line, Off, Value);
    SetLineState(Line, SkbProfile::LineState::Modified);
}

void Cpu::OpSkbAtLoad(const DecodedInsn& Insn) {
    const uint64_t Line = ParseSkbLineImm(Insn.Imm1);
    const uint64_t Off = LineByteOffset(Insn);
    WriteGpr(Insn.Rd, SkbAtomicReadWord(Line, Off));
}

void Cpu::OpSkbAtStore(const DecodedInsn& Insn) {
    const uint64_t Line = ParseSkbLineImm(Insn.Imm1);
    const uint64_t Off = LineByteOffset(Insn);
    SkbAtomicWriteWord(Line, Off, ReadGpr(Insn.Rd));
}

void Cpu::OpSkbAtAdd(const DecodedInsn& Insn) {
    const uint64_t Line = ParseSkbLineImm(Insn.Imm1);
    const uint64_t Off = LineByteOffset(Insn);
    const uint64_t Old = SkbAtomicReadWord(Line, Off);
    const uint64_t New = Old + ReadGpr(Insn.Rd);
    SkbAtomicWriteWord(Line, Off, New);
    WriteGpr(Insn.Rd, Old);
}

void Cpu::OpSkbAtCas(const DecodedInsn& Insn) {
    const uint64_t Line = ParseSkbLineImm(Insn.Imm1);
    const uint64_t Off = LineByteOffset(Insn);
    const uint64_t Expected = ReadGpr(Insn.Rs);
    const uint64_t NewVal = ReadGpr(Insn.Rd);
    const uint64_t Old = SkbAtomicReadWord(Line, Off);
    if (Old == Expected) {
        SkbAtomicWriteWord(Line, Off, NewVal);
    }
    WriteGpr(Insn.Rd, Old);
}

void Cpu::OpSkbAtSwap(const DecodedInsn& Insn) {
    const uint64_t Line = ParseSkbLineImm(Insn.Imm1);
    const uint64_t Off = LineByteOffset(Insn);
    const uint64_t Old = SkbAtomicReadWord(Line, Off);
    SkbAtomicWriteWord(Line, Off, ReadGpr(Insn.Rd));
    WriteGpr(Insn.Rd, Old);
}

void Cpu::OpSkbAtReduce(const DecodedInsn& Insn) {
    const uint64_t Line = ParseSkbLineImm(Insn.Imm1);
    const uint64_t Off = LineByteOffset(Insn);
    const uint64_t Old = SkbAtomicReadWord(Line, Off);
    const uint64_t Operand = ReadGpr(Insn.Rd);
    const uint8_t Op = static_cast<uint8_t>(Insn.Rt & 0x3F);
    uint64_t NewVal = Old;
    switch (Op) {
        case SkbProfile::ReduceOpSum:
            NewVal = Old + Operand;
            break;
        case SkbProfile::ReduceOpMin:
            NewVal = Old < Operand ? Old : Operand;
            break;
        case SkbProfile::ReduceOpMax:
            NewVal = Old > Operand ? Old : Operand;
            break;
        case SkbProfile::ReduceOpAnd:
            NewVal = Old & Operand;
            break;
        case SkbProfile::ReduceOpOr:
            NewVal = Old | Operand;
            break;
        default:
            throw std::runtime_error("Unsupported SKBAT_REDUCE op");
    }
    SkbAtomicWriteWord(Line, Off, NewVal);
    WriteGpr(Insn.Rd, Old);
}

void Cpu::OpSkbBulk(const DecodedInsn& Insn, bool ToDram) {
    const uint64_t Tag = Insn.Rd;
    const uint64_t Count = static_cast<uint64_t>(Insn.Disp) & 0x0FFFFFFFULL;
    const uint64_t MemAddr = Insn.Imm1;
    for (uint64_t Index = 0; Index < Count; ++Index) {
        const uint64_t Line = Tag + Index;
        if (Line >= SkbActiveLineCount()) {
            break;
        }
        if (ToDram) {
            for (size_t Off = 0; Off < SkbProfile::LineBytes; Off += 8) {
                uint64_t Word = 0;
                std::memcpy(&Word, LineDataPtr(Line) + Off, sizeof(Word));
                Write64(MemAddr + Index * SkbProfile::LineBytes + Off, Word);
            }
            SetLineState(Line, SkbProfile::LineState::Shared);
        } else {
            for (size_t Off = 0; Off < SkbProfile::LineBytes; Off += 8) {
                const uint64_t Word =
                    Read64(MemAddr + Index * SkbProfile::LineBytes + Off);
                std::memcpy(LineDataPtr(Line) + Off, &Word, sizeof(Word));
            }
            SetLineState(Line, SkbProfile::LineState::Shared);
            SkbMetaVec()[Line].DramBackAddr = MemAddr + Index * SkbProfile::LineBytes;
            SkbMetaVec()[Line].Tag = Line;
        }
    }
}

void Cpu::OpSkbPrefetch(const DecodedInsn& Insn) {
    const uint64_t Line = ParseSkbLineImm(Insn.Imm1);
    TouchLine(Line, SkbProfile::LineState::Shared);
}

void Cpu::SkbSetLineHomeNode(uint64_t Line, uint8_t Node) {
    if (Line >= SkbActiveLineCount()) {
        return;
    }
    EnsureSkbStorage();
    SkbMetaVec()[Line].HomeNode = Node;
}

void Cpu::SkbSetLineFlags(uint64_t Line, uint8_t Flags) {
    if (Line >= SkbActiveLineCount()) {
        return;
    }
    EnsureSkbStorage();
    SkbMetaVec()[Line].Flags = Flags;
}

void Cpu::SkbReplicateLineToNode(uint8_t Node, uint64_t Line) {
    if (Node >= NumaProfile::MaxNodes || Line >= SkbActiveLineCount()) {
        return;
    }
    EnsureSkbStorage();
    for (uint64_t Off = 0; Off < SkbProfile::LineBytes; Off += 8) {
        const uint64_t Word = SkbReadWord(Line, Off);
        const uint64_t RemoteAddr =
            NumaProfile::RemoteSkbBaseAddress |
            (static_cast<uint64_t>(Node) << NumaProfile::NodeShift) | (Line << 6) | Off;
        WriteRemotePhysical(RemoteAddr, Word);
    }
}

void Cpu::ExecuteSkbOpcode(const DecodedInsn& Insn) {
    switch (Insn.Op) {
        case Opcode::SKB_LOAD:
            OpSkbLoad(Insn);
            break;
        case Opcode::SKB_STORE:
            OpSkbStore(Insn);
            break;
        case Opcode::SKB_BIND:
            OpSkbBind(Insn);
            break;
        case Opcode::SKB_UNBIND:
            OpSkbUnbind(Insn);
            break;
        case Opcode::SKB_FLUSH:
            OpSkbFlush(Insn);
            break;
        case Opcode::SKB_INVALIDATE:
            OpSkbInvalidate(Insn);
            break;
        case Opcode::SKB_PREFETCH:
            OpSkbPrefetch(Insn);
            break;
        case Opcode::SKB_ZERO:
            OpSkbZero(Insn);
            break;
        case Opcode::SKB_AT_LOAD:
            OpSkbAtLoad(Insn);
            break;
        case Opcode::SKB_AT_STORE:
            OpSkbAtStore(Insn);
            break;
        case Opcode::SKB_AT_ADD:
            OpSkbAtAdd(Insn);
            break;
        case Opcode::SKB_AT_CAS:
            OpSkbAtCas(Insn);
            break;
        case Opcode::SKB_AT_SWAP:
            OpSkbAtSwap(Insn);
            break;
        case Opcode::SKB_AT_REDUCE:
            OpSkbAtReduce(Insn);
            break;
        case Opcode::SKB_MOVE:
            OpSkbMove(Insn);
            break;
        case Opcode::SKB_PIN:
            OpSkbPin(Insn, true);
            break;
        case Opcode::SKB_UNPIN:
            OpSkbPin(Insn, false);
            break;
        case Opcode::SKB_BULK_LOAD:
            OpSkbBulk(Insn, false);
            break;
        case Opcode::SKB_BULK_STORE:
            OpSkbBulk(Insn, true);
            break;
        case Opcode::SKB_BULK_ZERO:
            OpSkbZero(Insn);
            break;
        case Opcode::SKB_STATUS:
            OpSkbStatus(Insn);
            break;
        default:
            throw std::runtime_error("Unknown SKB opcode");
    }
}
