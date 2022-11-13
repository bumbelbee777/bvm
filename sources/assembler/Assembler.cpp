#include <algorithm>
#include <fstream>
#include <map>
#include <Assembler.h>
#include <iostream>
#include <cstdint>
#include <stdio.h>
#include <string>

using namespace std;

namespace Libvm {
    namespace Assembler {
        map<string, uint8_t> OpCodes {
            {"hlt", 0x00},
            {"nop", 0x01},
            {"mov", 0x02},
            {"and", 0x03},
            {"xor", 0x04},
            {"add", 0x05},
            {"sub", 0x06},
            {"mul", 0x07},
            {"div", 0x08},
            {"sqrt",0x09},
            {"pow", 0x0A},
            {"jmp", 0x0B},
            {"je",  0x0C},
            {"jne", 0x0D},
            {"jz",  0x0E},
            {"jnz", 0x0F},
            {"mov", 0x10},
            {"in",  0x11},
            {"out", 0x12},
            {"push",0x13},
            {"pop", 0x14},
        };

        void AssembleFile(string InputFile, string OutputFileName) {
            fstream InFile;
            string line;
            InFile.open(InputFile,ios::out);
            if(InFile.is_open()) {
                while (!InFile.eof()) {
                    string line;
                    if(OpCodes.find(line) == OpCodes.end()) {
                        cerr << "Unknown expression " << line << ", skipping..." << endl;
                        continue;
                    } else {
                        //TODO: well do the actual assembling
                    }
                }
                InFile.close();
            }

            InFile.open(OutputFileName,ios::in);
            if (InFile.is_open()) {
                InFile.close();
            }
        }
    }
}