#include <map>
#include <fstream>
#include <Assembler.h>
#include <sstream>
#include <iterator>
#include <iostream>
#include <cstdint>
#include <vector>
#include <string>

using std::map, std::string, std::vector, std::cerr, std::endl, std::stringstream;

enum Instructions : uint64_t {
    NOP = 0b1100100,
    HLT = 0b1101110,
    AND = 0b1111000,
    OR = 0b10000010,
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
    JE = 0b1001010000,
    JNE = 0b1001100000,
    JZ = 0b1010000000,
    JNZ = 0b1010001000,
    PUSH = 0b110000100,
    POP = 0b110001110,
    LOAD = 0b110010000,
    STORE = 0b110011000,
    IN = 0b110100000,
    OUT = 0b110101000,
    CALL = 0b110101110,
    RET = 0b110111000,
    XAND = 0b111000010,
    OR128 = 0b111001100,
    XNOT = 0b111011110,
    XXOR = 0b111100000,
    XADD = 0b111101010,
    XSUB = 0b111110100,
    XMUL = 0b111111110,
    XDIV = 0b1000001000,
    XLOAD = 0b1000010010,
    XSTORE = 0b1000011100,
};

map<string, Instructions> OpCodes {
    {"nop", NOP},
    {"hlt", HLT},
    {"and", AND},
    {"or",  OR},
    {"not", NOT},
    {"xor", XOR},
    {"add", ADD},
    {"sub", SUB},
    {"mul", MUL},
    {"div", DIV},
    {"cmp", CMP},
	{"rsh", RSH},
	{"lsh", LSH},
	{"ror", ROR},
	{"rol", ROL},
	{"jmp", JMP},
    {"je",  JE},
    {"jne", JNE},
    {"jz",  JZ},
    {"jnz", JNZ},
    {"push",PUSH},
    {"pop", POP},
    {"load", LOAD},
    {"store", STORE},
	{"in", IN},
	{"out", OUT},
    {"call", CALL},
    {"ret", RET},
	{"xand", XAND},
	{"or128", OR128},
	{"xnot", XNOT},
	{"xxor", XXOR},
	{"xadd", XADD},
	{"xsub", XSUB},
	{"xmul", XMUL},
	{"xdiv", XDIV}
};

vector<string> split(string str, char delimiter) {
    vector<string> internal;
    stringstream ss(str);
    string tok;

    while (getline(ss, tok, delimiter)) {
        internal.push_back(tok);
    }

    return internal;
}

string Trim(string String) {
    size_t first = String.find_first_not_of(' ');
    size_t last = String.find_last_not_of(' ');
    return String.substr(first, (last - first + 1));
}

uint64_t ParseRegister(string Register) {
    if (Register == "r0") {
        return 0b1001110010101010100101100000001110001111000001011101111000000111;
    } else if (Register == "r1") {
        return 0b0011010111001110111110111110101110101101011101111011001001101110;
    } else if (Register == "r2") {
        return 0b1101110111000111011011011100111110110011011011111101111000100001;
    } else if (Register == "r3") {
        return 0b1010000111001101111010000110100000101101010110111010011111001100;
    } else if (Register == "r4") {
        return 0b1100000101001110111110001010010000100001010101101100010110101111;
    } else if (Register == "r5") {
        return 0b0011110111001101101011011110110100101100111101111010011001010001;
    } else if (Register == "r6") {
        return 0b1011101101110110001010001111101100110000011000110011101100001011;
    } else if (Register == "r7") {
        return 0b1010100101110101111101010110011001011110111100110101011011111011;
    } else if (Register == "sp") {
        return 0b1100011000100100111110101010110101110011010011000011010110101011;
    } else if (Register == "x0") {
        return 0b1101100011001110001110001101100111110001111010000010111100001111;
    } else if (Register == "x1") {
        return 0b0011110011010111101100110111011111000011101100110011110001101101;
    } else if (Register == "x2") {
        return 0b1101100010011010110110111101111101010110001110011110011010010111;
    } else if (Register == "x3") {
        return 0b1111010110011011101111110001010111111001111100111110011001011110;
    } else {
        return 0;
    }
}

uint64_t Encode(string operation, vector<string> operands) {
    Instructions instr = OpCodes[operation];
    uint64_t Encoded = instr << 48;
    uint64_t Register;

    switch (operands.size()) {
        case 0:
            break;
        case 1:
            Register = ParseRegister(operands[0]);
            Encoded |= Register;
            break;
        
        case 2:
            Register = ParseRegister(operands[0]);
            Encoded |= Register << 16;

            if (operands[1][0] == 'r') {
                Register = ParseRegister(operands[1]);
                Encoded |= Register;
            } else {
                uint64_t Value = stoi(operands[1], nullptr, 0);
                Encoded |= Value & 0xffff;
            }

            break;

        case 3: 
			break;
        default:
            Encoded = 0;
            break;
    }

    return Encoded;
}

vector<uint64_t> BuildROM(vector<uint64_t> Encoded_instructions) {
    vector<uint64_t> ROM;
    while (Encoded_instructions.size() < (1 << 20)) {
        Encoded_instructions.push_back(0);
    }
}