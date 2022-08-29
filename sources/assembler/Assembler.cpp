#include <map>
#include <iostream>
#include <cstdint>
#include <string>

using namespace std;

namespace libvm {
    namespace Assembler {
        map<string, uint8_t> OpCodes {
            {"hlt", 0x00},
            {"nop", 0x01},
            {"and", 0xA0},
            {"xor", 0xA1},
            {"add", 0xB0},
            {"sub", 0xB1},
            {"mul", 0xB2},
            {"div", 0xB3},
            {"sqrt",0xB4},
            {"pow", 0xB5},
            {"jmp", 0xC0},
            {"je",  0xC1},
            {"jne", 0xC2},
            {"jz",  0xC3},
            {"jnz", 0xC4}
        };
    }
}