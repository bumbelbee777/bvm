#include "Shared.h"
#include "Machine.h"
#include "NumaProfile.h"
#include "SkbProfile.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <vector>

using namespace Honeycomb;

std::vector<std::vector<uint8_t>> gRemoteK;
std::vector<std::vector<uint8_t>> gRemoteSkb;
std::vector<std::vector<uint8_t>> gRemoteDram;

void ConfigureNumaStorage(uint32_t Nodes, size_t RemoteK, size_t RemoteSkb, size_t RemoteDram) {
    (void)Nodes;
    gRemoteK.assign(NumaProfile::MaxNodes, std::vector<uint8_t>(RemoteK, 0));
    gRemoteSkb.assign(NumaProfile::MaxNodes, std::vector<uint8_t>(RemoteSkb, 0));
    gRemoteDram.assign(NumaProfile::MaxNodes, std::vector<uint8_t>(RemoteDram, 0));
}

namespace {

constexpr uint32_t KAddrOffsetMask = 0x0FFFFFFFU;

uint64_t DispOffsetLow(const DecodedInsn& Insn) {
    return static_cast<uint64_t>(Insn.Disp) & KAddrOffsetMask;
}

void EnsureRemotePools() {
    if (gRemoteK.empty()) {
        ConfigureNumaStorage(GetActiveNumaNodeCount(), GetRemoteKBytes(), GetRemoteSkbBytes(),
                             GetRemoteDramBytes());
    }
}

std::vector<uint8_t>& RemoteKStorage(uint8_t Node) {
    EnsureRemotePools();
    return gRemoteK[Node];
}

std::vector<uint8_t>& RemoteSkbStorage(uint8_t Node) {
    EnsureRemotePools();
    return gRemoteSkb[Node];
}

std::vector<uint8_t>& RemoteDramStorage(uint8_t Node) {
    EnsureRemotePools();
    return gRemoteDram[Node];
}

uint8_t NodeFromImm(uint64_t Imm) {
    const uint8_t Node = NumaProfile::DecodeNode(Imm);
    if (Node >= NumaProfile::MaxNodes || !IsNumaNodeActive(Node)) {
        throw std::out_of_range("NUMA node id out of range or inactive");
    }
    if (GetActiveNumaNodeCount() <= 1 && Node > 0) {
        throw std::runtime_error("Remote NUMA disabled (use --numa-nodes > 1)");
    }
    return Node;
}

size_t RemoteSkbActiveLineCount() {
    return GetRemoteSkbBytes() / NumaProfile::RemoteSkbLineBytes;
}

uint64_t ParseRemoteSkbLineImm(uint64_t Imm) {
    const PhysMap::Domain Domain = PhysMap::DecodeDomain(Imm);
    if (Domain != PhysMap::Domain::RemoteSkb) {
        throw std::runtime_error("NUMA SKB operand requires imm1[63:60] = 0011");
    }
    const uint64_t Line = (NumaProfile::StripNodeOffset(Imm) >> 6) & 0x7FFFFULL;
    if (Line >= RemoteSkbActiveLineCount()) {
        throw std::out_of_range("Remote SKB line index out of range");
    }
    return Line;
}

uint64_t RemoteSkbWordOffset(uint64_t Line, uint64_t ByteOff) {
    return Line * NumaProfile::RemoteSkbLineBytes + ByteOff;
}

uint64_t ReadRemoteSkbWord(uint8_t Node, uint64_t Line, uint64_t Off) {
    auto& Pool = RemoteSkbStorage(Node);
    uint64_t Word = 0;
    const uint64_t Base = RemoteSkbWordOffset(Line, Off);
    std::memcpy(&Word, Pool.data() + Base, sizeof(Word));
    return SwapBE64(Word);
}

void WriteRemoteSkbWord(uint8_t Node, uint64_t Line, uint64_t Off, uint64_t Value) {
    auto& Pool = RemoteSkbStorage(Node);
    Value = SwapBE64(Value);
    const uint64_t Base = RemoteSkbWordOffset(Line, Off);
    std::memcpy(Pool.data() + Base, &Value, sizeof(Value));
}

uint64_t PackHintImm(uint64_t Type, uint64_t Line) {
    return (Type << 24) | ((Line & 0x7FFFFULL) << 6);
}

} // namespace

void Cpu::ResetNuma() {
    for (std::size_t Node = 0; Node < NumaProfile::MaxNodes; ++Node) {
        std::fill(RemoteKStorage(static_cast<uint8_t>(Node)).begin(),
                  RemoteKStorage(static_cast<uint8_t>(Node)).end(), 0);
        std::fill(RemoteSkbStorage(static_cast<uint8_t>(Node)).begin(),
                  RemoteSkbStorage(static_cast<uint8_t>(Node)).end(), 0);
        std::fill(RemoteDramStorage(static_cast<uint8_t>(Node)).begin(),
                  RemoteDramStorage(static_cast<uint8_t>(Node)).end(), 0);
    }
    NumaRemoteWind.fill(0);
    NumaStreamTagDone.fill(true);
    NumaKBindNode.fill(0);
    NumaKBindLine.fill(0);
    NumaKBindActive.fill(false);
    StreamBindSlot.fill(0);
    StreamBindActive.fill(false);
    HintBarrierEpoch = 0;
    HintBarrierCommit = 0;
}

bool Cpu::TryReadNumaBoundK(uint64_t Index, uint64_t& Value) const {
    const uint64_t Ws = KBankProfile::EffectiveWindowSize(WinSz);
    for (uint8_t Slot = 0; Slot < NumaKBindActive.size(); ++Slot) {
        if (!NumaKBindActive[Slot]) {
            continue;
        }
        const uint64_t Base = static_cast<uint64_t>(Slot) * Ws;
        if (Index < Base || Index >= Base + Ws) {
            continue;
        }
        const uint64_t Off = (Index - Base) * 8;
        if (Off + 8 > NumaProfile::RemoteSkbLineBytes) {
            continue;
        }
        Value = ReadRemoteSkbWord(NumaKBindNode[Slot], NumaKBindLine[Slot], Off);
        return true;
    }
    return false;
}

bool Cpu::TryWriteNumaBoundK(uint64_t Index, uint64_t Value) {
    const uint64_t Ws = KBankProfile::EffectiveWindowSize(WinSz);
    for (uint8_t Slot = 0; Slot < NumaKBindActive.size(); ++Slot) {
        if (!NumaKBindActive[Slot]) {
            continue;
        }
        const uint64_t Base = static_cast<uint64_t>(Slot) * Ws;
        if (Index < Base || Index >= Base + Ws) {
            continue;
        }
        const uint64_t Off = (Index - Base) * 8;
        if (Off + 8 > NumaProfile::RemoteSkbLineBytes) {
            continue;
        }
        WriteRemoteSkbWord(NumaKBindNode[Slot], NumaKBindLine[Slot], Off, Value);
        return true;
    }
    return false;
}

uint64_t Cpu::ReadRemotePhysical(uint64_t Addr) const {
    const uint8_t Node = NodeFromImm(Addr);
    const uint64_t Offset = NumaProfile::StripNodeOffset(Addr);

    if (NumaProfile::CoversRemoteK(Addr, 8)) {
        uint64_t Word = 0;
        std::memcpy(&Word, RemoteKStorage(Node).data() + Offset, sizeof(Word));
        return SwapBE64(Word);
    }
    if (NumaProfile::CoversRemoteSkb(Addr, 8)) {
        const uint64_t Line = Offset / NumaProfile::RemoteSkbLineBytes;
        const uint64_t Off = Offset % NumaProfile::RemoteSkbLineBytes;
        return ReadRemoteSkbWord(Node, Line, Off);
    }
    if (NumaProfile::CoversRemoteDram(Addr, 8)) {
        uint64_t Word = 0;
        std::memcpy(&Word, RemoteDramStorage(Node).data() + Offset, sizeof(Word));
        return SwapBE64(Word);
    }
    throw std::out_of_range("Remote NUMA read out of range");
}

void Cpu::WriteRemotePhysical(uint64_t Addr, uint64_t Value) {
    const uint8_t Node = NodeFromImm(Addr);
    const uint64_t Offset = NumaProfile::StripNodeOffset(Addr);

    if (NumaProfile::CoversRemoteK(Addr, 8)) {
        Value = SwapBE64(Value);
        std::memcpy(RemoteKStorage(Node).data() + Offset, &Value, sizeof(Value));
        return;
    }
    if (NumaProfile::CoversRemoteSkb(Addr, 8)) {
        const uint64_t Line = Offset / NumaProfile::RemoteSkbLineBytes;
        const uint64_t Off = Offset % NumaProfile::RemoteSkbLineBytes;
        WriteRemoteSkbWord(Node, Line, Off, Value);
        return;
    }
    if (NumaProfile::CoversRemoteDram(Addr, 8)) {
        Value = SwapBE64(Value);
        std::memcpy(RemoteDramStorage(Node).data() + Offset, &Value, sizeof(Value));
        return;
    }
    throw std::out_of_range("Remote NUMA write out of range");
}

void Cpu::OpStreamBind(const DecodedInsn& Insn) {
    const uint64_t Tag = Insn.Rt & 0x3F;
    const uint64_t Slot = Insn.Rs & 0x7;
    if (Tag >= StreamBindActive.size()) {
        throw std::out_of_range("STREAM tag out of range");
    }
    StreamBindSlot[Tag] = Slot;
    StreamBindActive[Tag] = true;
    Wind = Slot * KBankProfile::EffectiveWindowSize(WinSz);
    if (Tag < StreamTagDone.size()) {
        StreamTagDone[Tag] = true;
    }
}

void Cpu::OpStreamUnbind(const DecodedInsn& Insn) {
    const uint64_t Tag = Insn.Imm1Flag ? Insn.Imm1 : (Insn.Rt & 0x3F);
    if (Tag < StreamBindActive.size()) {
        StreamBindActive[Tag] = false;
    }
}

void Cpu::OpNumaBind(const DecodedInsn& Insn) {
    if (!Insn.Imm1Flag) {
        throw std::runtime_error("NUMABIND requires imm1 remote SKB line address");
    }
    const uint8_t Node = NodeFromImm(Insn.Imm1);
    const uint64_t Line = ParseRemoteSkbLineImm(Insn.Imm1);
    const uint8_t Slot = Insn.Rs & 0x7;
    NumaKBindNode[Slot] = Node;
    NumaKBindLine[Slot] = Line;
    NumaKBindActive[Slot] = true;
    Wind = static_cast<uint64_t>(Slot) * KBankProfile::EffectiveWindowSize(WinSz);
}

void Cpu::OpNumaStream(const DecodedInsn& Insn, bool ToRemote) {
    const uint64_t Tag = Insn.Rt & 0x3F;
    const uint64_t Len = DispOffsetLow(Insn);
    if (!Insn.Imm1Flag) {
        throw std::runtime_error("NUMASTREAM requires imm1 remote DRAM address");
    }
    const PhysMap::Domain Domain = PhysMap::DecodeDomain(Insn.Imm1);
    if (Domain != PhysMap::Domain::RemoteDram) {
        throw std::runtime_error("NUMASTREAM imm1 must use remote DRAM domain (0101)");
    }
    const uint8_t Node = NodeFromImm(Insn.Imm1);
    const uint64_t RemoteBase = NumaProfile::StripNodeOffset(Insn.Imm1);
    const uint64_t KBase = Wind;
    auto& Dram = RemoteDramStorage(Node);
    for (uint64_t Off = 0; Off < Len; Off += 8) {
        const uint64_t Ki = (KBase + (Off / 8)) & KMask;
        if (ToRemote) {
            const uint64_t Word = ReadKReg(Ki);
            if (RemoteBase + Off + 8 > Dram.size()) {
                throw std::out_of_range("NUMASTREAM remote DRAM overflow");
            }
            uint64_t Be = SwapBE64(Word);
            std::memcpy(Dram.data() + RemoteBase + Off, &Be, sizeof(Be));
        } else {
            uint64_t Word = 0;
            if (RemoteBase + Off + 8 > Dram.size()) {
                throw std::out_of_range("NUMASTREAM remote DRAM overflow");
            }
            std::memcpy(&Word, Dram.data() + RemoteBase + Off, sizeof(Word));
            WriteKReg(Ki, SwapBE64(Word));
        }
    }
    if (Tag < NumaStreamTagDone.size()) {
        NumaStreamTagDone[Tag] = true;
    }
    if (Tag < StreamTagDone.size()) {
        StreamTagDone[Tag] = true;
    }
}

void Cpu::OpNumaFence() {
    NumaStreamTagDone.fill(true);
    StreamTagDone.fill(true);
    HintBarrierCommit = HintBarrierEpoch;
}

void Cpu::OpNumaAdv(const DecodedInsn& Insn) {
    const uint64_t NodeVal = Insn.Imm1Flag ? Insn.Imm1 : DispOffsetLow(Insn);
    const uint8_t Node = static_cast<uint8_t>(NodeVal & NumaProfile::NodeMask);
    if (Node >= NumaProfile::MaxNodes) {
        throw std::out_of_range("NUMAADV node out of range");
    }
    NumaRemoteWind[Node] =
        (NumaRemoteWind[Node] + KBankProfile::EffectiveWindowSize(WinSz)) & KMask;
}

void Cpu::OpNumaAtomic(const DecodedInsn& Insn) {
    if (!Insn.Imm1Flag) {
        throw std::runtime_error("NUMAATOMIC requires imm1 remote SKB line");
    }
    const uint8_t Node = NodeFromImm(Insn.Imm1);
    const uint64_t Line = ParseRemoteSkbLineImm(Insn.Imm1);
    const uint64_t Off = Insn.Imm2Flag ? (Insn.Imm2 & (NumaProfile::RemoteSkbLineBytes - 1))
                                       : (DispOffsetLow(Insn) & (NumaProfile::RemoteSkbLineBytes - 1));
    const uint64_t Old = ReadRemoteSkbWord(Node, Line, Off);
    const uint64_t New = Old + ReadGpr(Insn.Rd);
    WriteRemoteSkbWord(Node, Line, Off, New);
    WriteGpr(Insn.Rd, Old);
}

void Cpu::OpHintBarrier() {
    HintBarrierCommit = HintBarrierEpoch;
    NumaStreamTagDone.fill(true);
    StreamTagDone.fill(true);
}

void Cpu::OpHintInsn(const DecodedInsn& Insn) {
    if (!Insn.Imm1Flag) {
        return;
    }
    const uint64_t HintType = (Insn.Imm1 >> 24) & 0xFF;
    const uint64_t Target = (Insn.Imm1 >> 16) & 0xFF;
    const uint64_t Line = (Insn.Imm1 >> 6) & 0x7FFFFULL;

    DecodedInsn SkbInsn = Insn;
    SkbInsn.Imm1 = (static_cast<uint64_t>(PhysMap::Domain::LocalSkb) << PhysMap::DomainShift) |
                   (Line << 6);
    SkbInsn.Imm1Flag = true;

    if (HintType == NumaProfile::HintType::SkbPin) {
        OpSkbPin(SkbInsn, true);
    } else if (HintType == NumaProfile::HintType::SkbUnpin) {
        OpSkbPin(SkbInsn, false);
    } else if (HintType == NumaProfile::HintType::SkbTouch ||
               HintType == NumaProfile::HintType::SkbPrefetch) {
        OpSkbPrefetch(SkbInsn);
    } else if (HintType == NumaProfile::HintType::SkbEvict) {
        OpSkbInvalidate(SkbInsn);
    } else if (HintType == NumaProfile::HintType::FlushScope) {
        OpSkbFlush(SkbInsn);
    } else if (HintType == NumaProfile::HintType::Fence) {
        OpHintBarrier();
    } else if (HintType == NumaProfile::HintType::SkbMigrate) {
        SkbSetLineHomeNode(Line, static_cast<uint8_t>(Target & 0xFF));
    } else if (HintType == NumaProfile::HintType::NumaPlace) {
        SkbSetLineHomeNode(Line, static_cast<uint8_t>(Target & NumaProfile::NodeMask));
    } else if (HintType == NumaProfile::HintType::NumaAffinity) {
        SkbSetLineFlags(Line, static_cast<uint8_t>(Target & 0xFF));
    } else if (HintType == NumaProfile::HintType::NumaReplicate) {
        SkbReplicateLineToNode(static_cast<uint8_t>(Target & NumaProfile::NodeMask), Line);
    } else if (HintType >= 0x70 && HintType <= 0x72) {
        const uint64_t Count = Target ? Target : 1;
        for (uint64_t Index = 0; Index < Count && Index + Line < SkbProfile::LineCount; ++Index) {
            SkbInsn.Imm1 = PackHintImm(NumaProfile::HintType::SkbTouch, Line + Index);
            OpSkbPrefetch(SkbInsn);
        }
    } else if (HintType == NumaProfile::HintType::Acquire) {
        OpSkbPin(SkbInsn, true);
    } else if (HintType == NumaProfile::HintType::Release) {
        OpSkbPin(SkbInsn, false);
    }

    HintBarrierEpoch += 1;
}
