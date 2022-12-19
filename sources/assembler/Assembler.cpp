#include <map>
#include <fstream>
#include <Assembler.h>
#include <sstream>
#include <iterator>
#include <iostream>
#include <cstdint>
#include <vector>
#include <string>

namespace Bas {
    namespace Assembler {
        enum Instructions {
            NOP = 0x00,
            HLT = 0x01,
            MOV = 0x02,
            AND = 0x03,
            OR  = 0x04,
            NOT = 0x05,
            XOR = 0x06,
            ADD = 0x07,
            SUB = 0x08,
            MUL = 0x09,
            DIV = 0x0A,
            SQRT = 0x0B,
            JMP = 0x0C,
            JE = 0x0D,
            JNE = 0x0E,
            JZ  = 0x0F,
            JNZ = 0x10,
            IN = 0x11,
            OUT = 0x12,
            PUSH = 0x13,
            POP = 0x14,
            RSH = 0x15,
            LSH = 0x16,
            LOAD = 0x17,
            STORE = 0x18,
            CALL = 0x19,
            RET = 0x20,
        };

        map<string, Instructions> OpCodes {
            {"hlt", HLT},
            {"nop", NOP},
            {"mov", MOV},
            {"and", AND},
            {"or",  OR},
            {"not", NOT},
            {"xor", XOR},
            {"add", ADD},
            {"sub", SUB},
            {"mul", MUL},
            {"div", DIV},
            {"sqrt",SQRT},
            {"jmp", JMP},
            {"je",  JE},
            {"jne", JNE},
            {"jz",  JZ},
            {"jnz", JNZ},
            {"in",  IN},
            {"out", OUT},
            {"push",PUSH},
            {"pop", POP},
            {"rsh", RSH},
            {"lsh", LSH},
            {"load", LOAD},
            {"store", STORE},
            {"call", CALL},
            {"ret", RET},
        };

        void AssembleFile(string InputFile, string OutputFileName) {
            std::ifstream FileIn(InputFile);
            std::ofstream FileOut(OutputFileName, std::ios::binary);
            string line;
            while(std::getline(FileIn, line)) { 
                std::vector<uint64_t> Code;
                std::map<string, int> Labels;
                int Address = 0;
                std::istringstream iss(line);
                vector<string> tokens{std::istream_iterator<string>(iss),
                              std::istream_iterator<string>()};
                if (tokens[0][tokens[0].size() - 1] == ':') {
                    string Label = tokens[0].substr(0, tokens[0].size() - 1);
                    Labels[Label] = Address;
                } else {
                    Address++;
                    if (OpCodes.count(tokens[0])) {
                        // Insert the opcode into the machine code
                        Code.push_back(OpCodes.at(tokens[0]));
                    } else {
                        // Print an error message if the instruction is invalid
                        cerr << "Error: invalid instruction '" << tokens[0] << "'" << endl;
                        throw 1;
                    }
                    FileIn.close();
                    FileOut.close();
                }
            }
        }
    }
}