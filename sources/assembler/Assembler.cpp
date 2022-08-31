#include <fstream>
#include <map>
#include <Assembler.h>
#include <iostream>
#include <cstdint>
#include <stdio.h>
#include <string>

using namespace std;

namespace libvm {
    namespace Assembler {
        map<string, uint8_t> OpCodes {
            {"hlt", 0x00},
            {"nop", 0x01},
            {"mov", 0x02},
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
            {"jnz", 0xC4},
            {"mov", 0xD0},
            {"in",  0xE0},
            {"out", 0xE1},
            {"push",0xF0},
            {"pop", 0xF1},
        };

        void HandleLabel(char* text) {
            cout << "Got label " << text << "." << endl;
            CurrentLabel = text;
        }

        void AssembleFile(string InputFile, string OutputFileName) {
            fstream InFile;
            string line;
            InFile.open(InputFile,ios::out);
            if(InFile.is_open()) {
                while (!InFile.eof()) {
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