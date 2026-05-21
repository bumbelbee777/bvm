#include "Compact.h"
#include "CompactProfile.h"

#include <stdexcept>

using namespace Honeycomb;

DecodedInsn Honeycomb::DecodeCompactInsn(uint16_t Word, uint64_t Pc) {
    const uint8_t Op = static_cast<uint8_t>((Word >> 10) & 0x3F);
    const uint8_t Rd = static_cast<uint8_t>((Word >> 5) & 0x1F);
    const uint8_t Rs = static_cast<uint8_t>(Word & 0x1F);

    DecodedInsn Insn{};
    Insn.Address = Pc;
    Insn.CompactForm = true;
    Insn.Opmode = OpmodeInt64;

    switch (static_cast<CompactProfile::CompactOp>(Op)) {
        case CompactProfile::CompactOp::NOP:
            Insn.Op = Opcode::NOP;
            return Insn;
        case CompactProfile::CompactOp::ADD:
            Insn.Op = Opcode::ADD;
            Insn.Rd = Rd;
            Insn.Rs = Rd;
            Insn.Rt = Rs;
            return Insn;
        case CompactProfile::CompactOp::SUB:
            Insn.Op = Opcode::SUB;
            Insn.Rd = Rd;
            Insn.Rs = Rd;
            Insn.Rt = Rs;
            return Insn;
        case CompactProfile::CompactOp::MOV:
            Insn.Op = Opcode::COPY;
            Insn.Rd = Rd;
            Insn.Rs = Rs;
            return Insn;
        case CompactProfile::CompactOp::LDI: {
            Insn.Op = Opcode::ADD;
            Insn.Rd = Rd;
            Insn.Rs = 0;
            Insn.Imm1Flag = true;
            const int32_t Imm = CompactProfile::SignExt5(Rs);
            Insn.Imm1 = static_cast<uint64_t>(Imm);
            return Insn;
        }
        case CompactProfile::CompactOp::LW:
            Insn.Op = Opcode::LOAD;
            Insn.Rd = Rd;
            Insn.Rs = static_cast<uint8_t>(Reg::SP);
            Insn.Disp = CompactProfile::SignExt5(Rs) * 2;
            return Insn;
        case CompactProfile::CompactOp::SW:
            Insn.Op = Opcode::STORE;
            Insn.Rd = Rd;
            Insn.Rs = static_cast<uint8_t>(Reg::SP);
            Insn.Disp = CompactProfile::SignExt5(Rs) * 2;
            return Insn;
        case CompactProfile::CompactOp::BZ:
            Insn.Op = Opcode::JE;
            Insn.Rd = Rd;
            Insn.Disp = CompactProfile::SignExt5(Rs) * 2;
            return Insn;
        case CompactProfile::CompactOp::BNZ:
            Insn.Op = Opcode::JNE;
            Insn.Rd = Rd;
            Insn.Disp = CompactProfile::SignExt5(Rs) * 2;
            return Insn;
        case CompactProfile::CompactOp::JMP:
            Insn.Op = Opcode::JMP;
            Insn.Disp = CompactProfile::SignExt5(Rs) * 2;
            return Insn;
        case CompactProfile::CompactOp::CALL: {
            Insn.Op = Opcode::CALL;
            const int32_t SlotOffset = CompactProfile::SignExt10(Word & 0x3FF);
            Insn.Disp = SlotOffset * 2;
            return Insn;
        }
        case CompactProfile::CompactOp::RET:
            Insn.Op = Opcode::RET;
            return Insn;
        case CompactProfile::CompactOp::PUSH:
            Insn.Op = Opcode::PUSH;
            Insn.Rd = Rd;
            return Insn;
        case CompactProfile::CompactOp::POP:
            Insn.Op = Opcode::POP;
            Insn.Rd = Rd;
            return Insn;
        case CompactProfile::CompactOp::CMPI:
            Insn.Op = Opcode::CMP;
            Insn.Rs = Rd;
            Insn.Imm1Flag = true;
            Insn.Imm1 = static_cast<uint64_t>(CompactProfile::SignExt5(Rs));
            return Insn;
        case CompactProfile::CompactOp::MOVS:
            Insn.Op = Opcode::COPY;
            Insn.Rd = Rd;
            Insn.Rs = static_cast<uint8_t>(Reg::SP);
            return Insn;
        case CompactProfile::CompactOp::HLT:
            Insn.Op = Opcode::HLT;
            return Insn;
        default:
            throw std::runtime_error("Unknown compact opcode");
    }
}
