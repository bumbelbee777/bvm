#include <Bvm.h>
#include <cstddef>
#include <cstdint>
#include <string>

uint64_t RAM[0b1111010011000000];

enum Instructions : uint64_t {
    NOP = 0b1100100,
    HLT = 0b1101110,
    AND = 0b1111000,
    OR  = 0b10000010,
    NOT = 0b10001100,
    XOR = 0b10010110,
    ADD = 0b10100000,
    SUB = 0b10101010,
    MUL = 0b10110100,
    DIV = 0b10111110,
    CMP = 0b11000000,
    RSH = 0b11010010,
    LSH = 0b11100100,
    ROR = 0b11101110,
    ROL = 0b11110000,
    JMP = 0b111110000,
    JE  = 0b1001010000,
    JNE = 0b1001100000,
    JZ  = 0b1010000000,
    JNZ = 0b1010001000,
    PUSH= 0b110000100,
    POP = 0b110001110,
    LOAD= 0b110010000,
    STORE=0b110011000,
    IN	= 0b110100000,
    OUT = 0b110101000,
    CALL= 0b110110000,
    RET = 0b110111000
};

bool CPU::IsRegister(uint64_t Value) {
    return (Value == 0b0000 || Value == 0b0010 || Value == 0b0100 || Value == 0b0110 || Value == 0b1000 || Value == 0b1010 || Value == 0b1100 || Value == 0b1110);
}

uint64_t CPU::ReadRegister(uint64_t Register) {
    if(!CPU::IsRegister(Register)) return 0;
    switch (Register) {
        case 0b0000:
            return CPU::r0;
        case 0b0010:
            return CPU::r1;
        case 0b0100:
            return CPU::r2;
        case 0b0110:
            return CPU::r3;
        case 0b1000:
            return CPU::r4;
        case 0b1010:
            return CPU::r5;
        case 0b1100:
            return CPU::r6;
        case 0b1110:
            return CPU::r7;
    }
}

void CPU::WriteRegister(uint64_t Register, uint64_t Value) {
    if(!CPU::IsRegister(Register)) return;
    switch(Register) {
        case 0b0000:
            CPU::r0 = Value;
        case 0b0010:
            CPU::r1 = Value;
        case 0b0100:
            CPU::r2 = Value;
        case 0b0110:
            CPU::r3 = Value;
        case 0b1000:
            CPU::r4 = Value;
        case 0b1010:
            CPU::r5 = Value;
        case 0b1100:
            CPU::r6 = Value;
        case 0b1110:
            CPU::r7 = Value;
    }
}

void CPU::Fetch() {
    ic++;
}

uint64_t CPU::DecodeAndExecute(uint64_t Instruction) {
    switch(Instruction) {
        case NOP: {
			Fetch();
		}

		case HLT: {
			ic = 0;
			sp = 0;
			return 0;
		}

		case AND: {
			WriteRegister(ic + 1, (IsRegister(ic + 2) ? ReadRegister(ic + 2) & (IsRegister(ic + 3) ? ReadRegister(ic + 3) : ic + 3) : ic + 2 & (IsRegister(ic + 3) ? ReadRegister(ic + 3) : ic + 3)));
			Fetch();
		}

		case OR: {
			WriteRegister(ic + 1, (IsRegister(ic + 2) ? ReadRegister(ic + 2) | (IsRegister(ic + 3) ? ReadRegister(ic + 3) : ic + 3) : ic + 2 | (IsRegister(ic + 3) ? ReadRegister(ic + 3) : ic + 3)));
			Fetch();
		}

		case NOT: {
			WriteRegister(ic + 1, (IsRegister(ic + 2) ? ~(ReadRegister(ic + 2) ^ (IsRegister(ic + 3) ? ReadRegister(ic + 3) : ic + 3)) : ~(ic + 2 ^ (IsRegister(ic + 3) ? ReadRegister(ic + 3) : ic + 3))));
			Fetch();
		}

		case XOR: {
			WriteRegister(ic + 1, (IsRegister(ic + 2) ? ReadRegister(ic + 2) ^ (IsRegister(ic + 3) ? ReadRegister(ic + 3) : ic + 3) : ic + 2 ^ (IsRegister(ic + 3) ? ReadRegister(ic + 3) : ic + 3)));
			Fetch();
		}

		case ADD: {
			WriteRegister(ic + 1, (IsRegister(ic + 2) ? ReadRegister(ic + 2) + (IsRegister(ic + 3) ? ReadRegister(ic + 3) : ic + 3) : ic + 2 + (IsRegister(ic + 3) ? ReadRegister(ic + 3) : ic + 3)));
			Fetch();
		}

		case SUB: {
			WriteRegister(ic + 1, (IsRegister(ic + 2) ? ReadRegister(ic + 2) - (IsRegister(ic + 3) ? ReadRegister(ic + 3) : ic + 3) : ic + 2 - (IsRegister(ic + 3) ? ReadRegister(ic + 3) : ic + 3)));
			Fetch();
		}

		case MUL: {
			WriteRegister(ic + 1, (IsRegister(ic + 2) ? ReadRegister(ic + 2) * (IsRegister(ic + 3) ? ReadRegister(ic + 3) : ic + 3) : ic + 2 * (IsRegister(ic + 3) ? ReadRegister(ic + 3) : ic + 3)));
			Fetch();
		}

		case DIV: {
			WriteRegister(ic + 1, (IsRegister(ic + 2) ? ReadRegister(ic + 2) / (IsRegister(ic + 3) ? ReadRegister(ic + 3) : ic + 3) : ic + 2 / (IsRegister(ic + 3) ? ReadRegister(ic + 3) : ic + 3)));
			Fetch();
		}

		case CMP: {
			if(IsRegister(ic + 1) ? ReadRegister(ic + 1) : ic + 1 == IsRegister(ic + 2) ? ReadRegister(ic + 2) : ic + 2) fr |= (1ull << 1);
			Fetch();
		}

		case RSH: {
			WriteRegister(ic + 1, (IsRegister(ic + 2) ? ReadRegister(ic + 2) << (IsRegister(ic + 3) ? ReadRegister(ic + 3) : ic + 3) : (ic + 2) << (IsRegister(ic + 3) ? ReadRegister(ic + 3) : ic + 3)));
			Fetch();
		}

		case LSH: {
			WriteRegister(ic + 1, (IsRegister(ic + 2) ? ReadRegister(ic + 2) >> (IsRegister(ic + 3) ? ReadRegister(ic + 3) : ic + 3) : (ic + 2) >> (IsRegister(ic + 3) ? ReadRegister(ic + 3) : ic + 3)));
			Fetch();
		}

		case ROR: {
			Fetch();
		}

		case ROL: {
			Fetch();
		}

		case JMP: {
			Fetch();
		}

		case JE: {
			Fetch();
		}
    }
}