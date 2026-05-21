#include "Shared.h"
#include "RegFile.h"
#include "SsxCrypto.h"
#include "SsxProfile.h"

#include <cmath>
#include <cstring>
#include <stdexcept>

using namespace Honeycomb;

namespace {

enum class SsxLaneArithmeticOp : uint8_t {
    Add,
    Sub,
    Mul,
    Div,
    Min,
    Max,
    Abs,
    Neg,
    FusedMultiplyAdd,
    FusedMultiplySubtract,
    FusedNegMultiplyAdd,
    FusedNegMultiplySubtract,
    Sqrt,
    Reciprocal,
    ReciprocalSqrt
};

uint64_t ResolveMemAddress(const DecodedInsn& Insn) {
    return Insn.Imm1Flag ? Insn.Imm1 : static_cast<uint64_t>(Insn.Disp);
}

uint32_t ActiveVectorLength(const Cpu& Vm) {
    if (Vm.SsxVectorLength == 0) {
        return SsxBaselineProfile::DefaultVectorLength;
    }
    return Vm.SsxVectorLength;
}

uint32_t ElementSizeBytes(uint8_t Opmode) {
    if (Opmode == OpmodeSsxFp32 || Opmode == OpmodeFp32Packed) {
        return 4;
    }
    return 8;
}

void RequireSsxFp32Opmode(uint8_t Opmode) {
    if (Opmode != OpmodeSsxFp32 && Opmode != OpmodeSsxGeneric &&
        Opmode != OpmodeFp32Packed) {
        throw std::runtime_error(
            "Baseline BVM scalable SSX ops require opmode FP32 (100) or generic (011)");
    }
}

float ReadFp32Lane(Uint128 Value, uint32_t LaneIndex) {
    const unsigned Shift = (3u - LaneIndex) * 32u;
    const uint32_t Bits = static_cast<uint32_t>(Value >> Shift);
    float Lane = 0.0f;
    std::memcpy(&Lane, &Bits, sizeof(Lane));
    return Lane;
}

void WriteFp32Lane(Uint128& Value, uint32_t LaneIndex, float Lane) {
    const unsigned Shift = (3u - LaneIndex) * 32u;
    const Uint128 Mask = (static_cast<Uint128>(0xFFFFFFFFULL) << Shift);
    uint32_t Bits = 0;
    std::memcpy(&Bits, &Lane, sizeof(Bits));
    Value = (Value & ~Mask) | (static_cast<Uint128>(Bits) << Shift);
}

size_t VectorMemoryBytes(const Cpu& Vm, uint8_t Opmode) {
    return static_cast<size_t>(ActiveVectorLength(Vm)) *
           static_cast<size_t>(ElementSizeBytes(Opmode));
}

void ExecuteLaneArithmetic(Cpu& Vm, const DecodedInsn& Insn, SsxLaneArithmeticOp Op) {
    RequireSsxFp32Opmode(Insn.Opmode);
    const uint32_t VectorLength = ActiveVectorLength(Vm);
    Uint128 Dest = ReadXReg(Vm, Insn.Rd);
    const Uint128 Src1 = ReadXReg(Vm, Insn.Rs);
    const Uint128 Src2 = ReadXReg(Vm, Insn.Rt);

    for (uint32_t Lane = 0; Lane < VectorLength; ++Lane) {
        const float A = ReadFp32Lane(Src1, Lane);
        const float B = ReadFp32Lane(Src2, Lane);
        switch (Op) {
            case SsxLaneArithmeticOp::Add:
                WriteFp32Lane(Dest, Lane, A + B);
                break;
            case SsxLaneArithmeticOp::Sub:
                WriteFp32Lane(Dest, Lane, A - B);
                break;
            case SsxLaneArithmeticOp::Mul:
                WriteFp32Lane(Dest, Lane, A * B);
                break;
            case SsxLaneArithmeticOp::Div:
                if (B == 0.0f) {
                    throw std::runtime_error("SSX VDIV division by zero");
                }
                WriteFp32Lane(Dest, Lane, A / B);
                break;
            case SsxLaneArithmeticOp::Min:
                WriteFp32Lane(Dest, Lane, std::fminf(A, B));
                break;
            case SsxLaneArithmeticOp::Max:
                WriteFp32Lane(Dest, Lane, std::fmaxf(A, B));
                break;
            case SsxLaneArithmeticOp::Abs:
                WriteFp32Lane(Dest, Lane, std::fabs(A));
                break;
            case SsxLaneArithmeticOp::Neg:
                WriteFp32Lane(Dest, Lane, -A);
                break;
            case SsxLaneArithmeticOp::FusedMultiplyAdd: {
                const float Acc = ReadFp32Lane(Dest, Lane);
                WriteFp32Lane(Dest, Lane, std::fma(A, B, Acc));
                break;
            }
            case SsxLaneArithmeticOp::FusedMultiplySubtract: {
                const float Acc = ReadFp32Lane(Dest, Lane);
                WriteFp32Lane(Dest, Lane, std::fma(A, B, -Acc));
                break;
            }
            case SsxLaneArithmeticOp::FusedNegMultiplyAdd: {
                const float Acc = ReadFp32Lane(Dest, Lane);
                WriteFp32Lane(Dest, Lane, std::fma(-A, B, Acc));
                break;
            }
            case SsxLaneArithmeticOp::FusedNegMultiplySubtract: {
                const float Acc = ReadFp32Lane(Dest, Lane);
                WriteFp32Lane(Dest, Lane, std::fma(-A, B, -Acc));
                break;
            }
            case SsxLaneArithmeticOp::Sqrt:
                WriteFp32Lane(Dest, Lane, std::sqrt(A));
                break;
            case SsxLaneArithmeticOp::Reciprocal:
                if (A == 0.0f) {
                    throw std::runtime_error("SSX VRCP division by zero");
                }
                WriteFp32Lane(Dest, Lane, 1.0f / A);
                break;
            case SsxLaneArithmeticOp::ReciprocalSqrt:
                if (A <= 0.0f) {
                    throw std::runtime_error("SSX VRSQRT invalid operand");
                }
                WriteFp32Lane(Dest, Lane, 1.0f / std::sqrt(A));
                break;
        }
    }
    WriteXReg(Vm, Insn.Rd, Dest);
}

uint32_t X128ShiftAmount(const DecodedInsn& Insn) {
    if (Insn.Imm1Flag) {
        return static_cast<uint32_t>(Insn.Imm1 & 0x7F);
    }
    return static_cast<uint32_t>(Insn.Rt & 0x3F);
}

} // namespace

void Cpu::OpSsxLoad(const DecodedInsn& Insn) {
    const uint64_t Addr = ResolveMemAddress(Insn);
    const size_t Bytes = VectorMemoryBytes(*this, Insn.Opmode);
    if (Bytes > SsxBaselineProfile::FixedPackByteCount) {
        throw std::runtime_error("SSX load byte count exceeds 128-bit pack in baseline BVM");
    }
    Uint128 Value = 0;
    if (Bytes >= 16) {
        Value = Read128(Addr);
    } else if (Bytes >= 8) {
        Value = static_cast<Uint128>(Read64(Addr)) << 64;
    } else {
        Value = static_cast<Uint128>(Read32(Addr)) << 96;
    }
    WriteXReg(*this, Insn.Rd, Value);
}

void Cpu::OpSsxStore(const DecodedInsn& Insn) {
    const uint64_t Addr = ResolveMemAddress(Insn);
    const Uint128 Value = ReadXReg(*this, Insn.Rd);
    const size_t Bytes = VectorMemoryBytes(*this, Insn.Opmode);
    if (Bytes > SsxBaselineProfile::FixedPackByteCount) {
        throw std::runtime_error("SSX store byte count exceeds 128-bit pack in baseline BVM");
    }
    if (Bytes >= 16) {
        Write128(Addr, Value);
    } else if (Bytes >= 8) {
        Write64(Addr, static_cast<uint64_t>(Value >> 64));
    } else {
        Write32(Addr, static_cast<uint32_t>(Value >> 96));
    }
}

void Cpu::OpSsxBroadcast(const DecodedInsn& Insn) {
    RequireSsxFp32Opmode(Insn.Opmode);
    float Scalar = 0.0f;
    if (Insn.Imm1Flag) {
        const uint32_t Bits = static_cast<uint32_t>(Insn.Imm1);
        std::memcpy(&Scalar, &Bits, sizeof(Scalar));
    } else {
        const uint32_t Bits = static_cast<uint32_t>(ReadGpr(Insn.Rs));
        std::memcpy(&Scalar, &Bits, sizeof(Scalar));
    }

    Uint128 Dest = 0;
    const uint32_t VectorLength = ActiveVectorLength(*this);
    for (uint32_t Lane = 0; Lane < VectorLength; ++Lane) {
        WriteFp32Lane(Dest, Lane, Scalar);
    }
    WriteXReg(*this, Insn.Rd, Dest);
}

void Cpu::ExecuteSsxOpcode(const DecodedInsn& Insn) {
    switch (Insn.Op) {
        case Opcode::XLOAD:
        case Opcode::VLOAD:
        case Opcode::XLOAD128:
            OpSsxLoad(Insn);
            return;
        case Opcode::XSTORE:
        case Opcode::VSTORE:
        case Opcode::XSTORE128:
            OpSsxStore(Insn);
            return;
        case Opcode::VBROADCAST:
            OpSsxBroadcast(Insn);
            return;
        case Opcode::XSWAP: {
            const Uint128 Left = ReadXReg(*this, Insn.Rd);
            const Uint128 Right = ReadXReg(*this, Insn.Rs);
            WriteXReg(*this, Insn.Rd, Right);
            WriteXReg(*this, Insn.Rs, Left);
            return;
        }
        default:
            break;
    }

    switch (Insn.Op) {
        case Opcode::XCOPY:
            WriteXReg(*this, Insn.Rd, ReadXReg(*this, Insn.Rs));
            return;
        case Opcode::XSWAP128: {
            const Uint128 Left = ReadXReg(*this, Insn.Rd);
            const Uint128 Right = ReadXReg(*this, Insn.Rs);
            WriteXReg(*this, Insn.Rd, Right);
            WriteXReg(*this, Insn.Rs, Left);
            return;
        }
        case Opcode::XNOT:
            WriteXReg(*this, Insn.Rd, ~ReadXReg(*this, Insn.Rs));
            return;
        case Opcode::XADD:
            WriteXReg(*this, Insn.Rd,
                      ReadXReg(*this, Insn.Rs) + ReadXReg(*this, Insn.Rt));
            return;
        case Opcode::XSUB:
            WriteXReg(*this, Insn.Rd,
                      ReadXReg(*this, Insn.Rs) - ReadXReg(*this, Insn.Rt));
            return;
        case Opcode::XMUL:
            WriteXReg(*this, Insn.Rd,
                      ReadXReg(*this, Insn.Rs) * ReadXReg(*this, Insn.Rt));
            return;
        case Opcode::XDIV: {
            const Uint128 Divisor = ReadXReg(*this, Insn.Rt);
            if (Divisor == 0) {
                throw std::runtime_error("SSX XDIV division by zero");
            }
            WriteXReg(*this, Insn.Rd,
                      ReadXReg(*this, Insn.Rs) / Divisor);
            return;
        }
        case Opcode::XMOD:
            WriteXReg(*this, Insn.Rd,
                      ReadXReg(*this, Insn.Rs) % ReadXReg(*this, Insn.Rt));
            return;
        case Opcode::XCMP: {
            const Uint128 Lhs = ReadXReg(*this, Insn.Rs);
            const Uint128 Rhs = ReadXReg(*this, Insn.Rt);
            int64_t Diff = 0;
            if (Lhs < Rhs) {
                Diff = -1;
            } else if (Lhs > Rhs) {
                Diff = 1;
            }
            UpdateCompareResult(Diff);
            return;
        }
        case Opcode::XAND:
            WriteXReg(*this, Insn.Rd,
                      ReadXReg(*this, Insn.Rs) & ReadXReg(*this, Insn.Rt));
            return;
        case Opcode::XXOR:
            WriteXReg(*this, Insn.Rd,
                      ReadXReg(*this, Insn.Rs) ^ ReadXReg(*this, Insn.Rt));
            return;
        case Opcode::XRSH: {
            const uint32_t Amount = X128ShiftAmount(Insn);
            WriteXReg(*this, Insn.Rd, ReadXReg(*this, Insn.Rs) >> Amount);
            return;
        }
        case Opcode::XLSH: {
            const uint32_t Amount = X128ShiftAmount(Insn);
            WriteXReg(*this, Insn.Rd, ReadXReg(*this, Insn.Rs) << Amount);
            return;
        }
        case Opcode::XROR: {
            const uint32_t Amount = X128ShiftAmount(Insn) & 127;
            const Uint128 Src = ReadXReg(*this, Insn.Rs);
            WriteXReg(*this, Insn.Rd, (Src >> Amount) | (Src << (128 - Amount)));
            return;
        }
        case Opcode::XROL: {
            const uint32_t Amount = X128ShiftAmount(Insn) & 127;
            const Uint128 Src = ReadXReg(*this, Insn.Rs);
            WriteXReg(*this, Insn.Rd, (Src << Amount) | (Src >> (128 - Amount)));
            return;
        }
        case Opcode::VADD:
            ExecuteLaneArithmetic(*this, Insn, SsxLaneArithmeticOp::Add);
            return;
        case Opcode::VSUB:
            ExecuteLaneArithmetic(*this, Insn, SsxLaneArithmeticOp::Sub);
            return;
        case Opcode::VMUL:
            ExecuteLaneArithmetic(*this, Insn, SsxLaneArithmeticOp::Mul);
            return;
        case Opcode::VDIV:
            ExecuteLaneArithmetic(*this, Insn, SsxLaneArithmeticOp::Div);
            return;
        case Opcode::VMIN:
            ExecuteLaneArithmetic(*this, Insn, SsxLaneArithmeticOp::Min);
            return;
        case Opcode::VMAX:
            ExecuteLaneArithmetic(*this, Insn, SsxLaneArithmeticOp::Max);
            return;
        case Opcode::VABS: {
            DecodedInsn Unary = Insn;
            Unary.Rt = Insn.Rs;
            ExecuteLaneArithmetic(*this, Unary, SsxLaneArithmeticOp::Abs);
            return;
        }
        case Opcode::VNEG: {
            DecodedInsn Unary = Insn;
            Unary.Rt = Insn.Rs;
            ExecuteLaneArithmetic(*this, Unary, SsxLaneArithmeticOp::Neg);
            return;
        }
        case Opcode::VFMA:
            ExecuteLaneArithmetic(*this, Insn, SsxLaneArithmeticOp::FusedMultiplyAdd);
            return;
        case Opcode::VFMSUB:
            ExecuteLaneArithmetic(*this, Insn, SsxLaneArithmeticOp::FusedMultiplySubtract);
            return;
        case Opcode::VFNMADD:
            ExecuteLaneArithmetic(*this, Insn, SsxLaneArithmeticOp::FusedNegMultiplyAdd);
            return;
        case Opcode::VFNMSUB:
            ExecuteLaneArithmetic(*this, Insn, SsxLaneArithmeticOp::FusedNegMultiplySubtract);
            return;
        case Opcode::VSQRT: {
            DecodedInsn Unary = Insn;
            Unary.Rt = Insn.Rs;
            ExecuteLaneArithmetic(*this, Unary, SsxLaneArithmeticOp::Sqrt);
            return;
        }
        case Opcode::VRCP: {
            DecodedInsn Unary = Insn;
            Unary.Rt = Insn.Rs;
            ExecuteLaneArithmetic(*this, Unary, SsxLaneArithmeticOp::Reciprocal);
            return;
        }
        case Opcode::VRSQRT: {
            DecodedInsn Unary = Insn;
            Unary.Rt = Insn.Rs;
            ExecuteLaneArithmetic(*this, Unary, SsxLaneArithmeticOp::ReciprocalSqrt);
            return;
        }
        case Opcode::XCRYPT:
        case Opcode::XDECRYPT:
        case Opcode::XGEN:
            SsxCrypto::Execute(*this, Insn);
            return;
        default:
            throw std::runtime_error("Unimplemented SSX opcode in baseline BVM");
    }
}
