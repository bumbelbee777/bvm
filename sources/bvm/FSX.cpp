#include "Shared.h"
#include "FsxProfile.h"
#include "RegFile.h"

#include <cmath>
#include <cstring>
#include <stdexcept>

using namespace Honeycomb;

namespace {

double FsThirdOperand(const Cpu& Vm, const DecodedInsn& Insn) {
    if (Insn.Imm1Flag) {
        uint64_t Bits = Insn.Imm1;
        double Value = 0.0;
        std::memcpy(&Value, &Bits, sizeof(Value));
        return Value;
    }
    return ReadFsReg(Vm, Insn.Rt);
}

uint64_t ResolveMemAddress(const Cpu& Vm, const DecodedInsn& Insn) {
    if (Insn.Imm1Flag) {
        return Insn.Imm1;
    }
    uint64_t Addr = static_cast<uint64_t>(static_cast<int64_t>(Insn.Disp));
    if (Insn.Rs != 0) {
        Addr += Vm.ReadGpr(Insn.Rs);
    }
    if (Insn.Rt != 0) {
        Addr += Vm.ReadGpr(Insn.Rt);
    }
    return Addr;
}

uint64_t FsBitContainer(const Cpu& Vm, uint8_t Index) {
    double Value = ReadFsReg(Vm, Index);
    uint64_t Bits = 0;
    std::memcpy(&Bits, &Value, sizeof(Bits));
    return Bits;
}

void WriteFsFromBits(Cpu& Vm, uint8_t Index, uint64_t Bits) {
    double Value = 0.0;
    std::memcpy(&Value, &Bits, sizeof(Bits));
    WriteFsReg(Vm, Index, Value);
}

uint64_t FsShiftAmount(const Cpu& Vm, const DecodedInsn& Insn) {
    if (Insn.Imm1Flag) {
        return Insn.Imm1 & 63;
    }
    return static_cast<uint64_t>(ReadFsReg(Vm, Insn.Rt)) & 63;
}

} // namespace

void Cpu::OpFsxLoad(const DecodedInsn& Insn) {
    const uint64_t Addr = ResolveMemAddress(*this, Insn);
    const uint64_t Bits = Read64(Addr);
    double Value = 0.0;
    std::memcpy(&Value, &Bits, sizeof(Value));
    WriteFsReg(*this, Insn.Rd, Value);
}

void Cpu::OpFsxStore(const DecodedInsn& Insn) {
    const uint64_t Addr = ResolveMemAddress(*this, Insn);
    const double Value = ReadFsReg(*this, Insn.Rd);
    uint64_t Bits = 0;
    std::memcpy(&Bits, &Value, sizeof(Bits));
    Write64(Addr, Bits);
}

void Cpu::ExecuteFsxOpcode(const DecodedInsn& Insn) {
    if (Insn.Opmode != OpmodeFp64 && Insn.Opmode != OpmodeInt64 &&
        Insn.Opmode != OpmodeFp32Packed) {
        throw std::runtime_error(
            "FSX ops require opmode FP64 (001), FP32 packed (010), or integer default");
    }

    switch (Insn.Op) {
        case Opcode::FADD:
            WriteFsReg(*this, Insn.Rd,
                       ReadFsReg(*this, Insn.Rs) + FsThirdOperand(*this, Insn));
            break;
        case Opcode::FSUB:
            WriteFsReg(*this, Insn.Rd,
                       ReadFsReg(*this, Insn.Rs) - FsThirdOperand(*this, Insn));
            break;
        case Opcode::FMUL:
            WriteFsReg(*this, Insn.Rd,
                       ReadFsReg(*this, Insn.Rs) * FsThirdOperand(*this, Insn));
            break;
        case Opcode::FDIV: {
            const double Rhs = FsThirdOperand(*this, Insn);
            if (Rhs == 0.0) {
                throw std::runtime_error("FSX FDIV division by zero");
            }
            WriteFsReg(*this, Insn.Rd, ReadFsReg(*this, Insn.Rs) / Rhs);
            break;
        }
        case Opcode::FMOD: {
            const double Rhs = FsThirdOperand(*this, Insn);
            if (Rhs == 0.0) {
                throw std::runtime_error("FSX FMOD division by zero");
            }
            WriteFsReg(*this, Insn.Rd, std::fmod(ReadFsReg(*this, Insn.Rs), Rhs));
            break;
        }
        case Opcode::FMA:
            WriteFsReg(*this, Insn.Rd,
                       std::fma(ReadFsReg(*this, Insn.Rs), FsThirdOperand(*this, Insn),
                                ReadFsReg(*this, Insn.Rd)));
            break;
        case Opcode::FCOPY:
            WriteFsReg(*this, Insn.Rd, ReadFsReg(*this, Insn.Rs));
            break;
        case Opcode::FSQRT: {
            const double Value = ReadFsReg(*this, Insn.Rs);
            if (Value < 0.0) {
                throw std::runtime_error("FSX FSQRT invalid operand");
            }
            WriteFsReg(*this, Insn.Rd, std::sqrt(Value));
            break;
        }
        case Opcode::FABS:
            WriteFsReg(*this, Insn.Rd, std::fabs(ReadFsReg(*this, Insn.Rs)));
            break;
        case Opcode::FNEG:
            WriteFsReg(*this, Insn.Rd, -ReadFsReg(*this, Insn.Rs));
            break;
        case Opcode::RECIP: {
            const double Value = ReadFsReg(*this, Insn.Rs);
            if (Value == 0.0) {
                throw std::runtime_error("FSX RECIP division by zero");
            }
            WriteFsReg(*this, Insn.Rd, 1.0 / Value);
            break;
        }
        case Opcode::RSQRT: {
            const double Value = ReadFsReg(*this, Insn.Rs);
            if (Value <= 0.0) {
                throw std::runtime_error("FSX RSQRT invalid operand");
            }
            WriteFsReg(*this, Insn.Rd, 1.0 / std::sqrt(Value));
            break;
        }
        case Opcode::FCMP: {
            const double Lhs = ReadFsReg(*this, Insn.Rs);
            const double Rhs = FsThirdOperand(*this, Insn);
            int64_t Diff = 0;
            if (Lhs < Rhs) {
                Diff = -1;
            } else if (Lhs > Rhs) {
                Diff = 1;
            }
            UpdateCompareResult(Diff);
            break;
        }
        case Opcode::FRSH: {
            const uint64_t Amount = FsShiftAmount(*this, Insn);
            uint64_t Bits = FsBitContainer(*this, Insn.Rs);
            Bits >>= Amount;
            WriteFsFromBits(*this, Insn.Rd, Bits);
            break;
        }
        case Opcode::FLSH: {
            const uint64_t Amount = FsShiftAmount(*this, Insn);
            uint64_t Bits = FsBitContainer(*this, Insn.Rs);
            Bits <<= Amount;
            WriteFsFromBits(*this, Insn.Rd, Bits);
            break;
        }
        case Opcode::FROR: {
            const uint64_t Amount = FsShiftAmount(*this, Insn);
            uint64_t Bits = FsBitContainer(*this, Insn.Rs);
            Bits = (Bits >> Amount) | (Bits << (64 - Amount));
            WriteFsFromBits(*this, Insn.Rd, Bits);
            break;
        }
        case Opcode::FROL: {
            const uint64_t Amount = FsShiftAmount(*this, Insn);
            uint64_t Bits = FsBitContainer(*this, Insn.Rs);
            Bits = (Bits << Amount) | (Bits >> (64 - Amount));
            WriteFsFromBits(*this, Insn.Rd, Bits);
            break;
        }
        case Opcode::FLOAD:
            OpFsxLoad(Insn);
            break;
        case Opcode::FSTORE:
            OpFsxStore(Insn);
            break;
        default:
            throw std::runtime_error("Unimplemented FSX opcode in baseline BVM");
    }
}
