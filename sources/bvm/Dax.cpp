#include "DaxMailbox.h"
#include "DaxProfile.h"
#include "DaxToken.h"
#include "MatProfile.h"
#include "PhysMap.h"
#include "Shared.h"
#include "SkbProfile.h"
#include "SsxProfile.h"

#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Honeycomb;

namespace {

constexpr uint32_t DaxSubopShift = 24;
constexpr uint32_t DaxSubopMask = 0x3F;

uint8_t DaxSubop(const DecodedInsn& Insn) {
    return static_cast<uint8_t>((static_cast<uint32_t>(Insn.Disp) >> DaxSubopShift) & DaxSubopMask);
}

uint8_t DaxRel(const DecodedInsn& Insn) {
    return static_cast<uint8_t>(Insn.Disp & 7);
}

uint64_t DaxExtra(const DecodedInsn& Insn) {
    return static_cast<uint64_t>(Insn.Disp) & 0xFFFFFFULL;
}

bool CompareRel(int64_t Lhs, int64_t Rhs, DaxProfile::CompareRel Rel) {
    switch (Rel) {
        case DaxProfile::CompareRel::Eq: return Lhs == Rhs;
        case DaxProfile::CompareRel::Ne: return Lhs != Rhs;
        case DaxProfile::CompareRel::Lt: return Lhs < Rhs;
        case DaxProfile::CompareRel::Gt: return Lhs > Rhs;
        case DaxProfile::CompareRel::Le: return Lhs <= Rhs;
        case DaxProfile::CompareRel::Ge: return Lhs >= Rhs;
    }
    return false;
}

void StallInsn(Cpu& Vm, const DecodedInsn& Insn) {
    Vm.Ic = Insn.Address;
}

bool RequireValid(Cpu& Vm, uint64_t Index, const DecodedInsn& Insn) {
    if (DaxToken::IsValid(Vm, Index)) {
        return true;
    }
    StallInsn(Vm, Insn);
    return false;
}

uint32_t ActiveVectorLength(const Cpu& Vm) {
    uint32_t Vl = Vm.SsxVectorLength;
    if (Vl == 0) {
        Vl = SsxBaselineProfile::DefaultVectorLength;
    }
    return std::min(Vl, static_cast<uint32_t>(DaxProfile::MaxVectorLength));
}

uint64_t EvalBinaryInt(uint64_t Lhs, uint64_t Rhs, Opcode Op) {
    switch (Op) {
        case Opcode::DAX_ADD: return Lhs + Rhs;
        case Opcode::DAX_SUB: return Lhs - Rhs;
        case Opcode::DAX_MUL: return Lhs * Rhs;
        case Opcode::DAX_DIV:
            if (Rhs == 0) {
                throw std::runtime_error("DAX_DIV divide by zero");
            }
            return Lhs / Rhs;
        default: break;
    }
    throw std::runtime_error("DAX binary eval on non-arithmetic opcode");
}

uint64_t ParseSkbLineImm(uint64_t Imm) {
    const uint64_t Domain = (Imm >> PhysMap::DomainShift) & PhysMap::DomainMask;
    if (Domain != SkbProfile::DomainNibble && Domain != 0) {
        throw std::runtime_error("DAX SKB operand requires imm1[63:60] = 0010");
    }
    const uint64_t Line = (Imm >> 6) & 0x7FFFFULL;
    if (Line >= SkbProfile::LineCount) {
        throw std::out_of_range("SKB line index out of range");
    }
    return Line;
}

uint64_t SkbLinePhysAddr(uint64_t Line, uint64_t ByteOff) {
    return PhysMap::LocalSkbBaseAddress + Line * SkbProfile::LineBytes + ByteOff;
}

float TokenAsFp32(uint64_t Bits) {
    float Value = 0.0f;
    const uint32_t LaneBits = static_cast<uint32_t>(Bits);
    std::memcpy(&Value, &LaneBits, sizeof(Value));
    return Value;
}

uint64_t Fp32AsToken(float Value) {
    uint32_t Bits = 0;
    std::memcpy(&Bits, &Value, sizeof(Bits));
    return static_cast<uint64_t>(Bits);
}

MatMulDescriptor LoadMatDescriptor(const Cpu& Vm, uint64_t Address) {
    MatMulDescriptor Desc{};
    Desc.AddrA = Vm.Read64(Address);
    Desc.AddrB = Vm.Read64(Address + 8);
    Desc.AddrC = Vm.Read64(Address + 16);
    const uint64_t Meta = Vm.Read64(Address + 24);
    Desc.ElemBytes = static_cast<uint32_t>((Meta >> 48) & 0xFFFF);
    Desc.Rows = static_cast<uint32_t>((Meta >> 32) & 0xFFFF);
    Desc.Inner = static_cast<uint32_t>((Meta >> 16) & 0xFFFF);
    Desc.Cols = static_cast<uint32_t>(Meta & 0xFFFF);
    return Desc;
}

Conv2DDescriptor LoadConvDescriptor(const Cpu& Vm, uint64_t Address) {
    Conv2DDescriptor Desc{};
    Desc.AddrInput = Vm.Read64(Address);
    Desc.AddrKernel = Vm.Read64(Address + 8);
    Desc.AddrOutput = Vm.Read64(Address + 16);
    const uint64_t MetaIn = Vm.Read64(Address + 24);
    Desc.ElemBytes = static_cast<uint32_t>((MetaIn >> 48) & 0xFFFF);
    Desc.HeightIn = static_cast<uint32_t>((MetaIn >> 32) & 0xFFFF);
    Desc.WidthIn = static_cast<uint32_t>((MetaIn >> 16) & 0xFFFF);
    Desc.ChannelsIn = static_cast<uint32_t>(MetaIn & 0xFFFF);
    if (Desc.ChannelsIn == 0) {
        Desc.ChannelsIn = 1;
    }
    const uint64_t MetaKernel = Vm.Read64(Address + 32);
    Desc.KernelH = static_cast<uint32_t>((MetaKernel >> 16) & 0xFFFF);
    Desc.KernelW = static_cast<uint32_t>(MetaKernel & 0xFFFF);
    return Desc;
}

float ReadFp32Element(const Cpu& Vm, uint64_t Base, uint32_t Row, uint32_t Col,
                      uint32_t InnerDim) {
    const uint64_t Offset =
        static_cast<uint64_t>(Row) * static_cast<uint64_t>(InnerDim) +
        static_cast<uint64_t>(Col);
    const uint32_t Bits = Vm.Read32(Base + Offset * MatBaselineProfile::ElemBytesFp32);
    return TokenAsFp32(Bits);
}

std::vector<float> MultiplyMatrices(const Cpu& Vm, const MatMulDescriptor& Desc) {
    std::vector<float> Result(static_cast<size_t>(Desc.Rows) * Desc.Cols, 0.0f);
    for (uint32_t Row = 0; Row < Desc.Rows; ++Row) {
        for (uint32_t Col = 0; Col < Desc.Cols; ++Col) {
            float Sum = 0.0f;
            for (uint32_t Inner = 0; Inner < Desc.Inner; ++Inner) {
                const float Left = ReadFp32Element(Vm, Desc.AddrA, Row, Inner, Desc.Inner);
                const float Right = ReadFp32Element(Vm, Desc.AddrB, Inner, Col, Desc.Cols);
                Sum = std::fma(Left, Right, Sum);
            }
            Result[static_cast<size_t>(Row) * Desc.Cols + Col] = Sum;
        }
    }
    return Result;
}

bool RequireVectorStream(Cpu& Vm, uint64_t BaseIndex, uint32_t Vl, const DecodedInsn& Insn) {
    for (uint32_t Lane = 0; Lane < Vl; ++Lane) {
        const uint64_t Idx = (BaseIndex + Lane) & Vm.KMask;
        if (!DaxToken::IsValid(Vm, Idx)) {
            StallInsn(Vm, Insn);
            return false;
        }
    }
    return true;
}

void ProduceVectorLane(Cpu& Vm, uint64_t BaseIndex, uint32_t Lane, float Value, uint8_t Tag) {
    const uint64_t Idx = (BaseIndex + Lane) & Vm.KMask;
    DaxToken::Produce(Vm, Idx, Fp32AsToken(Value), Tag, 1);
}

float ReadVectorLane(const Cpu& Vm, uint64_t BaseIndex, uint32_t Lane) {
    const uint64_t Idx = (BaseIndex + Lane) & Vm.KMask;
    return TokenAsFp32(DaxToken::Data(Vm, Idx));
}

void ConsumeVectorStream(Cpu& Vm, uint64_t BaseIndex, uint32_t Vl) {
    for (uint32_t Lane = 0; Lane < Vl; ++Lane) {
        DaxToken::ConsumeSingle(Vm, (BaseIndex + Lane) & Vm.KMask);
    }
}

uint64_t AtomicApply(Cpu& Vm, uint64_t Addr, uint64_t Operand, DaxProfile::AtomicOp Op) {
    if ((Addr & 7ULL) != 0) {
        throw std::runtime_error("DAX_ATOMIC address must be 8-byte aligned");
    }
    const uint64_t Old = Vm.Read64(Addr);
    uint64_t NewVal = Old;
    switch (Op) {
        case DaxProfile::AtomicOp::Add: NewVal = Old + Operand; break;
        case DaxProfile::AtomicOp::Swap: NewVal = Operand; break;
        case DaxProfile::AtomicOp::And: NewVal = Old & Operand; break;
        case DaxProfile::AtomicOp::Or: NewVal = Old | Operand; break;
        case DaxProfile::AtomicOp::Xor: NewVal = Old ^ Operand; break;
    }
    Vm.Write64(Addr, NewVal);
    return Old;
}

unsigned CoreFromExtra(uint64_t Extra) {
    return static_cast<unsigned>((Extra >> 6) & 0x3F);
}

unsigned SlotFromExtra(uint64_t Extra) {
    return static_cast<unsigned>(Extra & 0x3F);
}

} // namespace

void Cpu::ExecuteDaxOpcode(const DecodedInsn& Insn) {
    const uint64_t Kd = DaxToken::ResolveSlotIndex(*this, Insn.Rd);
    const uint64_t Ks = DaxToken::ResolveSlotIndex(*this, Insn.Rs);
    const uint64_t Kt = DaxToken::ResolveSlotIndex(*this, Insn.Rt);
    const uint64_t Ku = DaxToken::ResolveSlotIndex(*this, static_cast<uint8_t>(DaxExtra(Insn) & 0x3F));

    switch (Insn.Op) {
        case Opcode::DAX_TOKEN: {
            uint64_t Value = 0;
            if (Insn.Imm1Flag) {
                Value = Insn.Imm1;
            } else {
                Value = ReadGpr(Insn.Rs);
            }
            DaxToken::Produce(*this, Kd, Value, DaxSubop(Insn), 1);
            break;
        }
        case Opcode::DAX_CONSUME:
            DaxToken::Invalidate(*this, Kd);
            break;
        case Opcode::DAX_PEEK:
            if (!RequireValid(*this, Ks, Insn)) {
                return;
            }
            WriteGpr(Insn.Rd, DaxToken::Data(*this, Ks));
            break;
        case Opcode::DAX_REFILL: {
            if (!DaxToken::IsValid(*this, Kd)) {
                throw std::runtime_error("DAX_REFILL on invalid token");
            }
            const uint64_t Count = Insn.Imm1Flag ? Insn.Imm1 : ReadGpr(Insn.Rs);
            if (Count > 255) {
                throw std::runtime_error("DAX_REFILL refcnt out of range");
            }
            DaxToken::Meta(*this, Kd).RefCnt = static_cast<uint8_t>(Count);
            break;
        }
        case Opcode::DAX_VALID:
            WriteGpr(Insn.Rd, DaxToken::IsValid(*this, Ks) ? 1ULL : 0ULL);
            break;
        case Opcode::DAX_REFCNT:
            WriteGpr(Insn.Rd, DaxToken::Meta(*this, Ks).RefCnt);
            break;
        case Opcode::DAX_TAG:
            WriteGpr(Insn.Rd, DaxToken::Meta(*this, Ks).Tag);
            break;
        case Opcode::DAX_WAIT:
            if (!RequireValid(*this, Kd, Insn)) {
                return;
            }
            break;
        case Opcode::DAX_WAIT_ALL: {
            const uint64_t Mask = DaxExtra(Insn);
            for (unsigned Bit = 0; Bit < 24; ++Bit) {
                if (((Mask >> Bit) & 1) == 0) {
                    continue;
                }
                const uint64_t Idx = (Wind + Bit) & KMask;
                if (!DaxToken::IsValid(*this, Idx)) {
                    StallInsn(*this, Insn);
                    return;
                }
            }
            break;
        }
        case Opcode::DAX_WAIT_ANY: {
            const uint64_t Mask = DaxExtra(Insn);
            for (unsigned Bit = 0; Bit < 24; ++Bit) {
                if (((Mask >> Bit) & 1) == 0) {
                    continue;
                }
                const uint64_t Idx = (Wind + Bit) & KMask;
                if (DaxToken::IsValid(*this, Idx)) {
                    WriteGpr(Insn.Rd, Bit);
                    return;
                }
            }
            StallInsn(*this, Insn);
            return;
        }
        case Opcode::DAX_ADD:
        case Opcode::DAX_SUB:
        case Opcode::DAX_MUL:
        case Opcode::DAX_DIV: {
            if (!RequireValid(*this, Ks, Insn) || !RequireValid(*this, Kt, Insn)) {
                return;
            }
            const uint64_t Result =
                EvalBinaryInt(DaxToken::Data(*this, Ks), DaxToken::Data(*this, Kt), Insn.Op);
            DaxToken::Produce(*this, Kd, Result, 0, 1);
            DaxToken::ConsumeSingle(*this, Ks);
            DaxToken::ConsumeSingle(*this, Kt);
            break;
        }
        case Opcode::DAX_FMA: {
            if (!RequireValid(*this, Ks, Insn) || !RequireValid(*this, Kt, Insn) ||
                !RequireValid(*this, Ku, Insn)) {
                return;
            }
            const uint64_t Acc = DaxToken::Data(*this, Ku);
            const uint64_t Result = EvalBinaryInt(
                EvalBinaryInt(DaxToken::Data(*this, Ks), DaxToken::Data(*this, Kt), Opcode::DAX_MUL),
                Acc, Opcode::DAX_ADD);
            DaxToken::Produce(*this, Kd, Result, 0, 1);
            DaxToken::ConsumeSingle(*this, Ks);
            DaxToken::ConsumeSingle(*this, Kt);
            DaxToken::ConsumeSingle(*this, Ku);
            break;
        }
        case Opcode::DAX_CMP:
        case Opcode::DAX_CMPI: {
            if (!RequireValid(*this, Ks, Insn)) {
                return;
            }
            const int64_t Lhs = static_cast<int64_t>(DaxToken::Data(*this, Ks));
            int64_t Rhs = 0;
            if (Insn.Op == Opcode::DAX_CMPI) {
                Rhs = static_cast<int64_t>(Insn.Imm1);
            } else {
                if (!RequireValid(*this, Kt, Insn)) {
                    return;
                }
                Rhs = static_cast<int64_t>(DaxToken::Data(*this, Kt));
                DaxToken::ConsumeSingle(*this, Kt);
            }
            const uint64_t Out =
                CompareRel(Lhs, Rhs, static_cast<DaxProfile::CompareRel>(DaxRel(Insn))) ? 1ULL
                                                                                        : 0ULL;
            DaxToken::Produce(*this, Kd, Out, 0, 1);
            DaxToken::ConsumeSingle(*this, Ks);
            break;
        }
        case Opcode::DAX_SEL: {
            const uint64_t Kc = Ks;
            const uint64_t KTrue = Kt;
            const uint64_t KFalse = Ku;
            if (!RequireValid(*this, Kc, Insn) || !RequireValid(*this, KTrue, Insn) ||
                !RequireValid(*this, KFalse, Insn)) {
                return;
            }
            const uint64_t Pick =
                DaxToken::Data(*this, Kc) != 0 ? DaxToken::Data(*this, KTrue)
                                              : DaxToken::Data(*this, KFalse);
            DaxToken::Produce(*this, Kd, Pick, 0, 1);
            DaxToken::ConsumeSingle(*this, Kc);
            DaxToken::ConsumeSingle(*this, KTrue);
            DaxToken::ConsumeSingle(*this, KFalse);
            break;
        }
        case Opcode::DAX_ADDI:
        case Opcode::DAX_MULI: {
            if (!RequireValid(*this, Ks, Insn)) {
                return;
            }
            const Opcode Arith = (Insn.Op == Opcode::DAX_ADDI) ? Opcode::DAX_ADD : Opcode::DAX_MUL;
            const uint64_t Result = EvalBinaryInt(DaxToken::Data(*this, Ks), Insn.Imm1, Arith);
            DaxToken::Produce(*this, Kd, Result, 0, 1);
            DaxToken::ConsumeSingle(*this, Ks);
            break;
        }
        case Opcode::DAX_FORK: {
            if (!RequireValid(*this, Ks, Insn)) {
                return;
            }
            const uint64_t Kd2 = DaxToken::ResolveSlotIndex(*this, Insn.Rt);
            const uint64_t Value = DaxToken::Data(*this, Ks);
            const uint8_t Tag = DaxToken::Meta(*this, Ks).Tag;
            DaxToken::Produce(*this, Kd, Value, Tag, 1);
            DaxToken::Produce(*this, Kd2, Value, Tag, 1);
            DaxToken::BumpRef(*this, Ks, 2);
            break;
        }
        case Opcode::DAX_JOIN: {
            if (!RequireValid(*this, Ks, Insn) || !RequireValid(*this, Kt, Insn)) {
                return;
            }
            DaxToken::Produce(*this, Kd, DaxToken::Data(*this, Ks), DaxToken::Meta(*this, Ks).Tag, 1);
            DaxToken::ConsumeSingle(*this, Ks);
            DaxToken::ConsumeSingle(*this, Kt);
            break;
        }
        case Opcode::DAX_MERGE: {
            if (DaxToken::IsValid(*this, Ks)) {
                DaxToken::Produce(*this, Kd, DaxToken::Data(*this, Ks), DaxToken::Meta(*this, Ks).Tag, 1);
                DaxToken::ConsumeSingle(*this, Ks);
            } else if (DaxToken::IsValid(*this, Kt)) {
                DaxToken::Produce(*this, Kd, DaxToken::Data(*this, Kt), DaxToken::Meta(*this, Kt).Tag, 1);
                DaxToken::ConsumeSingle(*this, Kt);
            } else {
                StallInsn(*this, Insn);
            }
            break;
        }
        case Opcode::DAX_BRANCH: {
            if (!RequireValid(*this, Ks, Insn)) {
                return;
            }
            const bool Take = DaxToken::Data(*this, Ks) != 0;
            DaxToken::ConsumeSingle(*this, Ks);
            Ic = Take ? DaxExtra(Insn) : Insn.Imm1;
            break;
        }
        case Opcode::DAX_CALL: {
            if (!RequireValid(*this, Ks, Insn)) {
                return;
            }
            DaxToken::ConsumeSingle(*this, Ks);
            Push64(Ic);
            Ic = DaxExtra(Insn);
            break;
        }
        case Opcode::DAX_RET: {
            if (!RequireValid(*this, Ks, Insn)) {
                return;
            }
            DaxToken::ConsumeSingle(*this, Ks);
            Ic = Pop64();
            break;
        }
        case Opcode::DAX_SERIALIZE: {
            if (!RequireValid(*this, Ks, Insn)) {
                return;
            }
            DaxToken::Produce(*this, Kd, DaxToken::Data(*this, Ks), DaxToken::Meta(*this, Ks).Tag, 1);
            DaxToken::ConsumeSingle(*this, Ks);
            break;
        }
        case Opcode::DAX_LOOP: {
            const uint64_t Kcount = Ks;
            const uint64_t Kinit = Kt;
            const uint64_t Kbody = Ku;
            if (!RequireValid(*this, Kcount, Insn) || !RequireValid(*this, Kinit, Insn)) {
                return;
            }
            const uint64_t Count = DaxToken::Data(*this, Kcount);
            if (Count == 0) {
                DaxToken::ConsumeSingle(*this, Kcount);
                DaxToken::ConsumeSingle(*this, Kinit);
                break;
            }
            DaxToken::Produce(*this, Kbody, DaxToken::Data(*this, Kinit),
                              DaxToken::Meta(*this, Kinit).Tag, 1);
            DaxToken::ConsumeSingle(*this, Kinit);
            DaxToken::Produce(*this, Kcount, Count - 1, DaxToken::Meta(*this, Kcount).Tag, 1);
            DaxToken::ConsumeSingle(*this, Kcount);
            break;
        }
        case Opcode::DAX_LOAD: {
            uint64_t Addr = Insn.Imm1Flag ? Insn.Imm1 : ReadGpr(Insn.Rs);
            DaxToken::Produce(*this, Kd, Read64(Addr), 0, 1);
            break;
        }
        case Opcode::DAX_STORE: {
            if (!RequireValid(*this, Ks, Insn)) {
                return;
            }
            const uint64_t Addr = Insn.Imm1Flag ? Insn.Imm1 : ReadGpr(Insn.Rt);
            Write64(Addr, DaxToken::Data(*this, Ks));
            DaxToken::ConsumeSingle(*this, Ks);
            break;
        }
        case Opcode::DAX_ATOMIC: {
            if (!Insn.Imm1Flag) {
                throw std::runtime_error("DAX_ATOMIC requires imm1 address");
            }
            const uint64_t Operand =
                Insn.Imm2Flag ? Insn.Imm2 : (Insn.Rs <= 31 ? ReadGpr(Insn.Rs) : 0);
            const uint64_t Old =
                AtomicApply(*this, Insn.Imm1, Operand, static_cast<DaxProfile::AtomicOp>(DaxRel(Insn)));
            DaxToken::Produce(*this, Kd, Old, 0, 1);
            break;
        }
        case Opcode::DAX_PROMOTE: {
            if (!RequireValid(*this, Ks, Insn) || !Insn.Imm1Flag) {
                if (!Insn.Imm1Flag) {
                    throw std::runtime_error("DAX_PROMOTE requires imm1 SKB line");
                }
                return;
            }
            const uint64_t Line = ParseSkbLineImm(Insn.Imm1);
            const uint64_t Value = DaxToken::Data(*this, Ks);
            const uint8_t Tag = DaxToken::Meta(*this, Ks).Tag;
            Write64(SkbLinePhysAddr(Line, 0), Value);
            Write64(SkbLinePhysAddr(Line, 8), static_cast<uint64_t>(Tag) << 8 | 1ULL);
            DaxToken::ConsumeSingle(*this, Ks);
            break;
        }
        case Opcode::DAX_DEMOTE: {
            if (!Insn.Imm1Flag) {
                throw std::runtime_error("DAX_DEMOTE requires imm1 SKB line");
            }
            const uint64_t Line = ParseSkbLineImm(Insn.Imm1);
            const uint64_t MetaWord = Read64(SkbLinePhysAddr(Line, 8));
            const uint64_t Value = Read64(SkbLinePhysAddr(Line, 0));
            const uint8_t Tag = static_cast<uint8_t>((MetaWord >> 8) & 0xFF);
            DaxToken::Produce(*this, Kd, Value, Tag, 1);
            break;
        }
        case Opcode::DAX_STREAM: {
            if (!Insn.Imm1Flag) {
                throw std::runtime_error("DAX_STREAM requires imm1 SKB address");
            }
            const uint64_t Line = ParseSkbLineImm(Insn.Imm1);
            const uint64_t Len = DaxExtra(Insn);
            const uint64_t Words = std::min(Len / 8, static_cast<uint64_t>(DaxProfile::MaxStreamTokens));
            for (uint64_t Word = 0; Word < Words; ++Word) {
                const uint64_t Value = Read64(SkbLinePhysAddr(Line, Word * 8));
                DaxToken::Produce(*this, (Kd + Word) & KMask, Value, 0, 1);
            }
            break;
        }
        case Opcode::DAX_GATHER: {
            if (!Insn.Imm1Flag) {
                throw std::runtime_error("DAX_GATHER requires imm1 SKB address");
            }
            const uint64_t Ki = Kt;
            if (!RequireValid(*this, Ki, Insn)) {
                return;
            }
            const uint64_t Line = ParseSkbLineImm(Insn.Imm1);
            const uint64_t Index = DaxToken::Data(*this, Ki) & 0x7;
            const uint64_t Value = Read64(SkbLinePhysAddr(Line, Index * 8));
            DaxToken::Produce(*this, Kd, Value, 0, 1);
            DaxToken::ConsumeSingle(*this, Ki);
            break;
        }
        case Opcode::DAX_VADD:
        case Opcode::DAX_VMUL:
        case Opcode::DAX_VFMA: {
            const uint32_t Vl = ActiveVectorLength(*this);
            if (!RequireVectorStream(*this, Ks, Vl, Insn) ||
                !RequireVectorStream(*this, Kt, Vl, Insn)) {
                return;
            }
            if (Insn.Op == Opcode::DAX_VFMA && !RequireVectorStream(*this, Ku, Vl, Insn)) {
                return;
            }
            for (uint32_t Lane = 0; Lane < Vl; ++Lane) {
                const float A = ReadVectorLane(*this, Ks, Lane);
                const float B = ReadVectorLane(*this, Kt, Lane);
                float Out = 0.0f;
                if (Insn.Op == Opcode::DAX_VADD) {
                    Out = A + B;
                } else if (Insn.Op == Opcode::DAX_VMUL) {
                    Out = A * B;
                } else {
                    const float C = ReadVectorLane(*this, Ku, Lane);
                    Out = std::fma(A, B, C);
                }
                ProduceVectorLane(*this, Kd, Lane, Out, 0);
            }
            ConsumeVectorStream(*this, Ks, Vl);
            ConsumeVectorStream(*this, Kt, Vl);
            if (Insn.Op == Opcode::DAX_VFMA) {
                ConsumeVectorStream(*this, Ku, Vl);
            }
            break;
        }
        case Opcode::DAX_VLOAD: {
            const uint32_t Vl = ActiveVectorLength(*this);
            const uint64_t Addr = Insn.Imm1Flag ? Insn.Imm1 : ReadGpr(Insn.Rs);
            for (uint32_t Lane = 0; Lane < Vl; ++Lane) {
                const uint32_t Bits =
                    Read32(Addr + static_cast<uint64_t>(Lane) * SsxBaselineProfile::DefaultElementSizeBytes);
                DaxToken::Produce(*this, (Kd + Lane) & KMask, static_cast<uint64_t>(Bits), 0, 1);
            }
            break;
        }
        case Opcode::DAX_VSTORE: {
            const uint32_t Vl = ActiveVectorLength(*this);
            if (!RequireVectorStream(*this, Ks, Vl, Insn)) {
                return;
            }
            const uint64_t Addr = Insn.Imm1Flag ? Insn.Imm1 : ReadGpr(Insn.Rt);
            for (uint32_t Lane = 0; Lane < Vl; ++Lane) {
                const uint32_t Bits = static_cast<uint32_t>(DaxToken::Data(
                    *this, (Ks + Lane) & KMask));
                Write32(Addr + static_cast<uint64_t>(Lane) * SsxBaselineProfile::DefaultElementSizeBytes,
                        Bits);
            }
            ConsumeVectorStream(*this, Ks, Vl);
            break;
        }
        case Opcode::DAX_REDUCE: {
            if (!RequireValid(*this, Ks, Insn)) {
                return;
            }
            const uint64_t Partner = DaxToken::ResolveSlotIndex(
                *this, static_cast<uint8_t>((DaxExtra(Insn) >> 6) & 0x3F));
            if (!RequireValid(*this, Partner, Insn)) {
                return;
            }
            uint64_t Result = 0;
            const auto Op = static_cast<DaxProfile::ReduceOp>(DaxRel(Insn));
            const uint64_t A = DaxToken::Data(*this, Ks);
            const uint64_t B = DaxToken::Data(*this, Partner);
            switch (Op) {
                case DaxProfile::ReduceOp::Sum: Result = A + B; break;
                case DaxProfile::ReduceOp::Min: Result = A < B ? A : B; break;
                case DaxProfile::ReduceOp::Max: Result = A > B ? A : B; break;
            }
            DaxToken::Produce(*this, Kd, Result, 0, 1);
            DaxToken::ConsumeSingle(*this, Ks);
            DaxToken::ConsumeSingle(*this, Partner);
            break;
        }
        case Opcode::DAX_MATMUL: {
            if (!Insn.Imm1Flag) {
                throw std::runtime_error("DAX_MATMUL requires imm1 descriptor address");
            }
            MatMulDescriptor Desc = LoadMatDescriptor(*this, Insn.Imm1);
            if (Desc.ElemBytes != MatBaselineProfile::ElemBytesFp32) {
                throw std::runtime_error("DAX_MATMUL requires FP32 descriptor");
            }
            if (Desc.Rows == 0 || Desc.Inner == 0 || Desc.Cols == 0 ||
                Desc.Rows > MatBaselineProfile::MaxDim ||
                Desc.Inner > MatBaselineProfile::MaxDim ||
                Desc.Cols > MatBaselineProfile::MaxDim) {
                throw std::runtime_error("DAX_MATMUL dimensions out of baseline range");
            }
            const std::vector<float> Product = MultiplyMatrices(*this, Desc);
            for (size_t Index = 0; Index < Product.size(); ++Index) {
                DaxToken::Produce(*this, (Kd + Index) & KMask, Fp32AsToken(Product[Index]), 0, 1);
            }
            break;
        }
        case Opcode::DAX_CONV: {
            if (!Insn.Imm1Flag) {
                throw std::runtime_error("DAX_CONV requires imm1 descriptor address");
            }
            const Conv2DDescriptor Desc = LoadConvDescriptor(*this, Insn.Imm1);
            if (Desc.ElemBytes != MatBaselineProfile::ElemBytesFp32) {
                throw std::runtime_error("DAX_CONV requires FP32 descriptor");
            }
            const uint32_t HeightOut = Desc.HeightIn - Desc.KernelH + 1;
            const uint32_t WidthOut = Desc.WidthIn - Desc.KernelW + 1;
            size_t OutIndex = 0;
            for (uint32_t OutRow = 0; OutRow < HeightOut; ++OutRow) {
                for (uint32_t OutCol = 0; OutCol < WidthOut; ++OutCol) {
                    float Sum = 0.0f;
                    for (uint32_t Kh = 0; Kh < Desc.KernelH; ++Kh) {
                        for (uint32_t Kw = 0; Kw < Desc.KernelW; ++Kw) {
                            const float Input = ReadFp32Element(
                                *this, Desc.AddrInput, OutRow + Kh, OutCol + Kw, Desc.WidthIn);
                            const float Kernel = ReadFp32Element(
                                *this, Desc.AddrKernel, Kh, Kw, Desc.KernelW);
                            Sum = std::fma(Input, Kernel, Sum);
                        }
                    }
                    DaxToken::Produce(*this, (Kd + OutIndex) & KMask, Fp32AsToken(Sum), 0, 1);
                    ++OutIndex;
                }
            }
            break;
        }
        case Opcode::DAX_SCAN: {
            const uint32_t Vl = ActiveVectorLength(*this);
            if (!RequireVectorStream(*this, Ks, Vl, Insn)) {
                return;
            }
            const auto Op = static_cast<DaxProfile::ScanOp>(DaxRel(Insn));
            float Acc = 0.0f;
            for (uint32_t Lane = 0; Lane < Vl; ++Lane) {
                const float Value = ReadVectorLane(*this, Ks, Lane);
                switch (Op) {
                    case DaxProfile::ScanOp::Sum: Acc += Value; break;
                    case DaxProfile::ScanOp::Min:
                        Acc = (Lane == 0) ? Value : std::min(Acc, Value);
                        break;
                    case DaxProfile::ScanOp::Max:
                        Acc = (Lane == 0) ? Value : std::max(Acc, Value);
                        break;
                }
                ProduceVectorLane(*this, Kd, Lane, Acc, 0);
            }
            ConsumeVectorStream(*this, Ks, Vl);
            break;
        }
        case Opcode::DAX_SEND: {
            if (!RequireValid(*this, Ks, Insn)) {
                return;
            }
            const unsigned DstCore = CoreFromExtra(DaxExtra(Insn));
            const unsigned DstSlot = SlotFromExtra(DaxExtra(Insn));
            DaxMailbox::Send(DstCore, DstSlot, DaxToken::Data(*this, Ks),
                             DaxToken::Meta(*this, Ks).Tag);
            DaxToken::ConsumeSingle(*this, Ks);
            break;
        }
        case Opcode::DAX_RECV: {
            const unsigned SrcCore = CoreFromExtra(DaxExtra(Insn));
            const unsigned SrcSlot = SlotFromExtra(DaxExtra(Insn));
            uint64_t Data = 0;
            uint8_t Tag = 0;
            if (!DaxMailbox::TryReceive(SrcCore, SrcSlot, Data, Tag)) {
                StallInsn(*this, Insn);
                return;
            }
            DaxMailbox::Clear(SrcCore, SrcSlot);
            DaxToken::Produce(*this, Kd, Data, Tag, 1);
            break;
        }
        case Opcode::DAX_BCAST: {
            if (!RequireValid(*this, Ks, Insn)) {
                return;
            }
            const uint64_t Mask = DaxExtra(Insn);
            const uint64_t Value = DaxToken::Data(*this, Ks);
            const uint8_t Tag = DaxToken::Meta(*this, Ks).Tag;
            for (unsigned Core = 0; Core < DaxProfile::MaxSimulatedCores; ++Core) {
                if (((Mask >> Core) & 1) == 0) {
                    continue;
                }
                DaxMailbox::Send(Core, static_cast<unsigned>(Insn.Rt & 0x3F), Value, Tag);
            }
            DaxToken::ConsumeSingle(*this, Ks);
            break;
        }
        case Opcode::DAX_REDUCE_SCATTER: {
            if (!RequireValid(*this, Ks, Insn)) {
                return;
            }
            const uint64_t Mask = DaxExtra(Insn) >> 3;
            const auto Op = static_cast<DaxProfile::ReduceOp>(DaxRel(Insn));
            uint64_t Acc = DaxToken::Data(*this, Ks);
            for (unsigned Core = 0; Core < DaxProfile::MaxSimulatedCores; ++Core) {
                if (((Mask >> Core) & 1) == 0) {
                    continue;
                }
                uint64_t Remote = 0;
                uint8_t Tag = 0;
                const unsigned RemoteSlot = static_cast<unsigned>(Insn.Rt & 0x3F);
                if (!DaxMailbox::TryReceive(Core, RemoteSlot, Remote, Tag)) {
                    continue;
                }
                DaxMailbox::Clear(Core, RemoteSlot);
                switch (Op) {
                    case DaxProfile::ReduceOp::Sum: Acc += Remote; break;
                    case DaxProfile::ReduceOp::Min: Acc = Acc < Remote ? Acc : Remote; break;
                    case DaxProfile::ReduceOp::Max: Acc = Acc > Remote ? Acc : Remote; break;
                }
            }
            DaxToken::Produce(*this, Kd, Acc, 0, 1);
            DaxToken::ConsumeSingle(*this, Ks);
            for (unsigned Core = 0; Core < DaxProfile::MaxSimulatedCores; ++Core) {
                if (((Mask >> Core) & 1) == 0) {
                    continue;
                }
                DaxMailbox::Send(Core, static_cast<unsigned>(Insn.Rd & 0x3F), Acc, 0);
            }
            break;
        }
        default:
            throw std::runtime_error("Unknown DAX opcode");
    }
}
