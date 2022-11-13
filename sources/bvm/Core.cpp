#include <iostream>
#include <cstdio>
#include <Libvm.h>
#include <stdint.h>
#include <cstring>
#include <cmath>
#include <vector>

using namespace std;
using namespace Libvm::RAM;

namespace Libvm {
    namespace RAM {
        uint8_t TotalRAM[0x70000];
    }

    namespace CPU {
        using namespace Libvm::RAM;
        bool IsRunning = false;
    
        uint8_t d1  = 0;        //data register 1
        uint8_t d2  = 0;        //data register 2
        uint8_t sp  = (uint8_t)0x1667;   //stack pointer
        uint8_t ic  = 0x0000;   //instruction counter
        uint8_t zf  = 0;        //zero flag

        void StartEmulator(vector<uint8_t> ROM) {
            if(IsRunning) {
                cerr <<"Tried to start CPU while it's running, exiting..." << endl;
                throw 1;
            }
            memset(TotalRAM, 0, 0x70000);
            for(int i = 0x000; i <= 0x1666; i++) {
                if(i - 0x0000 > ROM.size()) break;
                TotalRAM[i] = ROM[i - 0x0000];
            }
            IsRunning = true;
            cout << "Emulator is up and running!" << endl;
            ClockCycle();
        }

        void HaltEmulator(void) {
            if(!IsRunning) {
                cerr << "Tried to halt emulator while it's not running.";
                throw 1;
            }
            d1 = 0;
            d2 = 0;
            IsRunning = false;
            printf("Emulator halted!");
        }

        void ClockCycle(void) {
            if(!IsRunning) {
                cerr << "tried to cycle while CPU isn't running, exiting..." << endl;
                throw 1;
            }

            uint8_t CurrentInstruction = TotalRAM[ic];

            switch(CurrentInstruction) {
                case 0x00: { //hlt
                    printf("hlt");
                    HaltEmulator();
                    return;
                }

                case 0x01: { //nop
                    ic++;
                    printf("nop");
                    return;
                }

                case 0x02: { //mov
                    uint8_t x = RAM::TotalRAM[ic + 1], y = RAM::TotalRAM[ic + 2];
                    if(y > sizeof(RAM::TotalRAM)) {
                        cerr << "MOV tried to write to " << y << "which is beyond availble RAM, exiting..." << endl;
                        throw 1;
                    } else {
                        RAM::TotalRAM[x] = RAM::TotalRAM[y];
                    }
                    return;
                }

                case 0x03: { //and
                    d1 = d1 & d2;
                    ic++;
                    printf("and");
                    return;
                }

                case 0x04: { //xor
                    d1 = d1 ^ d2;
                    ic++;
                    printf("xor");
                    return;
                }

                case 0x05: { //add
                    d1 += d2;
                    ic++;
                    printf("add");
                    return;
                }

                case 0x06: { //sub
                    d1 -= d2;
                    ic++;
                    printf("sub");
                    return;
                }

                case 0x08: { //mul
                    d1 *= d2;
                    ic++;
                    printf("mul");
                    return;
                }

                case 0x09: { //div
                    d1 /= d2;
                    ic++;
                    printf("div");
                    return;
                }

                case 0x10: { //sqrt
                    sqrt(d1);
                    ic++;
                    printf("sqrt");
                    return;
                }

                case 0x11: { //pow
                    pow(d1, d2);
                    ic++;
                    printf("pow");
                    return;
                }

                case 0x12: { //jmp
                    ic += 1;
                    ic += 2;
                    printf("jmp");
                    return;
                }

                case 0x13: { //je
                    printf("je");
                    uint8_t Address = TotalRAM[ic + 1];
                    if(d1 == d2) {
                        ic = Address;
                    } else {
                        ic++;
                    }
                    return;
                }

                case 0x14: { //jne
                    printf("jne");
                    ic++;
                    return;
                }
                
                case 0x15: { //jz
                    printf("jz");
                    if((zf = 0)) {
                        uint8_t Address = TotalRAM[ic + 1];
                        ic = Address;
                    } else {
                        return;
                    }
                }

                case 0x16: { //jnz
                    printf("jnz");
                    if((zf != 0)) {
                        uint8_t Address = TotalRAM[ic + 1];
                        ic = Address;
                    } else {
                        return;
                    }
                }

                case 0x17: { //mov
                    uint8_t a = TotalRAM[ic + 1], b = TotalRAM[ic + 2];
                    TotalRAM[a] = TotalRAM[b]; //FIXME: idk what i'm doing
                    ic+=3;
                    printf("mov");
                    return;
                }

                default: {
                    cerr << "Got unkown instruction " << CurrentInstruction << ", exiting..." << endl;
                    throw 1;
                }
            }
        }
    }
}