#include "Shared.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

#if defined(_WIN64) && defined(_MSC_VER)
#include <intrin.h>
#endif

using namespace Honeycomb;

namespace {

inline void RequireAlignedWord(uint64_t Addr) {
    if ((Addr & 7ULL) != 0) {
        throw std::runtime_error("Atomic address must be 8-byte aligned");
    }
}

inline uint64_t ResolveAddrScalar(const DecodedInsn& Insn) {
    return Insn.Imm1Flag ? Insn.Imm1 : static_cast<uint64_t>(Insn.Disp);
}

constexpr uint64_t ReduceOpSum = 0;
constexpr uint64_t ReduceOpMin = 1;
constexpr uint64_t ReduceOpMax = 2;
constexpr uint64_t ReduceOpAnd = 3;
constexpr uint64_t ReduceOpOr = 4;
constexpr uint64_t ReduceOpXor = 5;

inline uint64_t ReduceMemValue(uint64_t Old, uint64_t Operand, uint64_t OpSel) {
    switch (OpSel) {
        case ReduceOpSum:
            return Old + Operand;
        case ReduceOpMin: {
            const int64_t A = static_cast<int64_t>(Old);
            const int64_t B = static_cast<int64_t>(Operand);
            const int64_t M = std::min(A, B);
            return static_cast<uint64_t>(M);
        }
        case ReduceOpMax: {
            const int64_t A = static_cast<int64_t>(Old);
            const int64_t B = static_cast<int64_t>(Operand);
            const int64_t M = std::max(A, B);
            return static_cast<uint64_t>(M);
        }
        case ReduceOpAnd:
            return Old & Operand;
        case ReduceOpOr:
            return Old | Operand;
        case ReduceOpXor:
            return Old ^ Operand;
        default:
            throw std::runtime_error("Unsupported ATREDUCE operation selector");
    }
}

inline uint64_t ReduceOpImmediate(const DecodedInsn& Insn) {
    // `ATREDUCE` uses `imm1` for the 64-bit address; selector comes from `disp` low bits.
    return static_cast<uint64_t>(Insn.Disp & 0xFFULL);
}

} // namespace

void Cpu::ExecuteAtomicOpcode(const DecodedInsn& Insn) {
    switch (Insn.Op) {
        case Opcode::ATCAS: {
            if (!Insn.Imm1Flag) {
                throw std::runtime_error("ATCAS requires address immediate (imm1)");
            }
            const uint64_t Addr = Insn.Imm1;
            RequireAlignedWord(Addr);
            const uint64_t Expected =
                Insn.Imm2Flag ? Insn.Imm2 : ReadGpr(Insn.Rs);
            const uint64_t NewVal = ReadGpr(Insn.Rd);
            const uint64_t Cur = Read64(Addr);
            if (Cur == Expected) {
                Write64(Addr, NewVal);
            } else {
                WriteGpr(Insn.Rd, Cur);
            }
            return;
        }
        case Opcode::ATCMP: {
            if (!Insn.Imm1Flag) {
                throw std::runtime_error("ATCMP requires address immediate");
            }
            const uint64_t Addr = Insn.Imm1;
            RequireAlignedWord(Addr);
            const int64_t MemVal =
                static_cast<int64_t>(Read64(Addr));
            const int64_t RegVal = static_cast<int64_t>(ReadGpr(Insn.Rd));
            UpdateCompareResult(MemVal - RegVal);
            return;
        }
        default:
            break;
    }

    const uint64_t Addr = ResolveAddrScalar(Insn);
    RequireAlignedWord(Addr);

    switch (Insn.Op) {
        case Opcode::ATAND: {
            const uint64_t Old = Read64(Addr);
            const uint64_t GprVal = ReadGpr(Insn.Rd);
            Write64(Addr, Old & GprVal);
            WriteGpr(Insn.Rd, Old);
            return;
        }
        case Opcode::ATOR: {
            const uint64_t Old = Read64(Addr);
            const uint64_t GprVal = ReadGpr(Insn.Rd);
            Write64(Addr, Old | GprVal);
            WriteGpr(Insn.Rd, Old);
            return;
        }
        case Opcode::ATXOR: {
            const uint64_t Old = Read64(Addr);
            const uint64_t GprVal = ReadGpr(Insn.Rd);
            Write64(Addr, Old ^ GprVal);
            WriteGpr(Insn.Rd, Old);
            return;
        }
        case Opcode::ATADD:
        case Opcode::ATFADD: {
            const uint64_t Old = Read64(Addr);
            const uint64_t GprVal = ReadGpr(Insn.Rd);
            Write64(Addr, Old + GprVal);
            WriteGpr(Insn.Rd, Old);
            return;
        }
        case Opcode::ATSUB:
        case Opcode::ATFSUB: {
            const uint64_t Old = Read64(Addr);
            const uint64_t GprVal = ReadGpr(Insn.Rd);
            Write64(Addr, Old - GprVal);
            WriteGpr(Insn.Rd, Old);
            return;
        }
        case Opcode::ATMUL: {
            const uint64_t Old = Read64(Addr);
            const uint64_t GprVal = ReadGpr(Insn.Rd);
            Write64(Addr, Old * GprVal);
            WriteGpr(Insn.Rd, Old);
            return;
        }
        case Opcode::ATDIV: {
            const uint64_t Old = Read64(Addr);
            const uint64_t GprVal = ReadGpr(Insn.Rd);
            const int64_t Num = static_cast<int64_t>(Old);
            const int64_t Den = static_cast<int64_t>(GprVal);
            if (Den == 0) {
                throw std::runtime_error("ATDIV divisor is zero");
            }
            if (Num == std::numeric_limits<int64_t>::min() && Den == -1) {
                throw std::runtime_error("ATDIV overflow");
            }
            const uint64_t New =
                static_cast<uint64_t>(Num / Den);
            Write64(Addr, New);
            WriteGpr(Insn.Rd, Old);
            return;
        }
        case Opcode::ATNOT: {
            const uint64_t Old = Read64(Addr);
            Write64(Addr, ~Old);
            return;
        }
        case Opcode::ATINC: {
            const uint64_t Old = Read64(Addr);
            Write64(Addr, Old + 1ULL);
            return;
        }
        case Opcode::ATDEC: {
            const uint64_t Old = Read64(Addr);
            Write64(Addr, Old - 1ULL);
            return;
        }
        case Opcode::ATSWAP: {
            const uint64_t Old = Read64(Addr);
            const uint64_t RegVal = ReadGpr(Insn.Rd);
            Write64(Addr, RegVal);
            WriteGpr(Insn.Rd, Old);
            return;
        }
        case Opcode::ATREDUCE: {
            const uint64_t Old = Read64(Addr);
            const uint64_t GprVal = ReadGpr(Insn.Rd);
            const uint64_t New = ReduceMemValue(Old, GprVal, ReduceOpImmediate(Insn));
            Write64(Addr, New);
            WriteGpr(Insn.Rd, Old);
            return;
        }
        default:
            throw std::runtime_error("Unimplemented atomic opcode");
    }
}
