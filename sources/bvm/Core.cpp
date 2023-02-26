#include <Shared.h>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <bits/stdc++.h>
#include <sys/types.h>

uint64_t RAM[0b1111010011000000];

uint8_t Ports[32];

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
    CMP = 0b11001000,
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
    CALL= 0b110101110,
    RET = 0b110111000,
    XAND= 0b111000010,
    OR128=0b111001100,
    XNOT= 0b111011110,
    XXOR= 0b111100000,
    XADD= 0b111101010,
    XSUB= 0b111110100,
    XMUL= 0b111111110,
    XDIV= 0b1000001000,
    XLOAD=0b1000010010,
    XSTORE=0b1000011100
};

bool CPU::IsRegister(uint64_t Value) {
    return (Value == 0b0000 || Value == 0b0010 || Value == 0b0100 || Value == 0b0110 || Value == 0b1000 || Value == 0b1010 || 
            Value == 0b1100 || Value == 0b1110 || Value == 0b1010111000 || Value == 0b1011001110 || Value == 0b1011100100 || Value == 0b1011111010);
}

uint64_t CPU::ReadRegister(uint64_t Register) {
    if(!CPU::IsRegister(Register)) return 0;
    switch (Register) {
        case 0b0000:
            return this->r0;
        case 0b0010:
            return this->r1;
        case 0b0100:
            return this->r2;
        case 0b0110:
            return this->r3;
        case 0b1000:
            return this->r4;
        case 0b1010:
            return this->r5;
        case 0b1100:
            return this->r6;
        case 0b1110:
            return this->r7;
        case 0b1111:
            return this->sp;
        case 0b1001011100111011110100110101101100011111001001010000000110000111:
            return this->ptb;
        case 0b1010111000:
            return this->x0;
        case 0b1011001110:
            return this->x1;
        case 0b1011100100:
            return this->x2;
        case 0b1011111010:
            return this->x3;
    }
	return 0;
}

void CPU::WriteRegister(uint64_t Register, uint64_t Value) {
    if(!CPU::IsRegister(Register)) return;
    switch(Register) {
        case 0b1001110010101010100101100000001110001111000001011101111000000111:
            this->r0 = Value;
        case 0b0011010111001110111110111110101110101101011101111011001001101110:
            this->r1 = Value;
        case 0b1101110111000111011011011100111110110011011011111101111000100001:
            this->r2 = Value;
        case 0b1010000111001101111010000110100000101101010110111010011111001100:
            this->r3 = Value;
        case 0b1100000101001110111110001010010000100001010101101100010110101111:
            this->r4 = Value;
        case 0b0011110111001101101011011110110100101100111101111010011001010001:
            this->r5 = Value;
        case 0b1011101101110110001010001111101100110000011000110011101100001011:
            this->r6 = Value;
        case 0b1010100101110101111101010110011001011110111100110101011011111011:
            this->r7 = Value;
        case 0b1100011000100100111110101010110101110011010011000011010110101011:
            this->sp = Value;
        case 0b1001011100111011110100110101101100011111001001010000000110000111:
            this->ptb = Value;
    }
}

void CPU::WriteRegister(uint64_t Register, uint128_t Value) {
    if(!CPU::IsRegister(Register)) return;
    switch(Register) {
        case 0b1101100011001110001110001101100111110001111010000010111100001111:
            this->x0 = Value;
        case 0b0011110011010111101100110111011111000011101100110011110001101101:
            this->x1 = Value;
        case 0b1101100010011010110110111101111101010110001110011110011010010111:
            this->x2 = Value;
        case 0b1111010110011011101111110001010111111001111100111110011001011110:
            this->x3 = Value;
    }
}

void CPU::Fetch() {
    ic += sizeof(uint64_t);
}

void CPU::Fetch(int Cycles) {
    ic += Cycles * sizeof(uint64_t);
}

void CPU::Decode(uint64_t Instruction) {
    Execute(Instruction);
}

uint64_t CPU::Get(int Cycles) {
    return IsRegister(ic + Cycles * sizeof(uint64_t)) ? ReadRegister(ic + Cycles * sizeof(uint64_t)) : ic + Cycles * sizeof(uint64_t);
}

void CPU::Execute(uint64_t Instruction) {
    switch (Instruction) {
        case NOP: {
            Fetch();
        }

        case HLT: {
            this->ic = 0;
            this->sp = 0;
            printf("CPU halted!");
            return;
        }

        case AND: {
            And(RAM[Get(1)], RAM[Get(2)], RAM[Get(3)]);
        }

        case OR: {
            Or(RAM[Get(1)], RAM[Get(2)], RAM[ic]);
        }

        case NOT: {
            Not(RAM[Get(1)], RAM[Get(2)], RAM[Get(3)]);
        }

        case XOR: {
            Xor(RAM[Get(1)], RAM[Get(2)], RAM[Get(3)]);
        }

        case ADD: {
            Add(RAM[Get(1)], RAM[Get(2)], RAM[Get(3)]);
        }

        case SUB: {
            Sub(RAM[Get(1)], RAM[Get(2)], RAM[Get(3)]);
        }

        case MUL: {
            Mul(RAM[Get(1)], RAM[Get(2)], RAM[Get(3)]);
        }

        case DIV: {
            Div(RAM[Get(1)], RAM[Get(2)], RAM[Get(3)]);
        }

        case CMP: {
            Cmp(RAM[Get(1)], RAM[Get(2)]);
        }

        case RSH: {
            Rsh(RAM[Get(1)], RAM[Get(2)], RAM[Get(3)]);
        }

        case LSH: {
            Lsh(RAM[Get(1)], RAM[Get(2)], RAM[Get(3)]);
        }

        case ROR: {
            Ror(RAM[Get(1)], RAM[Get(2)], RAM[Get(3)]);
        }

        case ROL: {
            Rol(RAM[Get(1)], RAM[Get(2)], RAM[Get(3)]);
        }

        case JMP: {
            Jmp(RAM[Get(1)]);
        }

        case JE: {
            Je(RAM[Get(1)]);
        }

        case JNE: {
            Jne(RAM[Get(1)]);
        }

        case JZ: {
            Jz(RAM[Get(1)]);
        }

        case JNZ: {
            Jnz(RAM[Get(1)]);
        }

        case PUSH: {
            Push(RAM[Get(1)]);
        }

        case POP: {
            Pop(RAM[Get(1)]);
        }

        case IN: {
            In(RAM[Get(1)], RAM[Get(2)]);
        }

        case OUT: {
            Out(RAM[Get(1)], RAM[Get(2)]);
        }

        case CALL: {
            Call(RAM[Get(1)]);
        }

        case RET: {
            Ret();
        }

        default:
            printf("Invalid instruction %lu.", Instruction);
            return;
    }
}