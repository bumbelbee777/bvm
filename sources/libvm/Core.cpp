#include <cstddef>
#include <cmath>
#include <iostream>
#include <cstdio>
#include <cstring>
#include <Libvm.h>
#include <stdint.h>
#include <stdio.h>

using namespace std;
using namespace Libvm::RAM;

namespace Libvm {
    namespace RAM {
        uint8_t TotalRAM[0x70000];
    }

    namespace CPU {
        using namespace libvm::RAM;
        bool IsRunning = false;
    
        uint8_t d1  = 0;        //data register 1
        uint8_t d2  = 0;        //data register 2
        float f1 = 0;           //floating point register 1
        float f2 = 0;           //floating point register 2
        uint8_t sp  = (uint8_t)0x1667;   //stack pointer
        uint8_t ic  = 0x0000;   //instruction counter

        void StartEmulator(vector<uint8_t> ROM) {
            if(IsRunning) {
                printf("Tried to start CPU while it's running, exiting...");
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
                cerr << "Tried to halt emulator while it's not running, exiting...";
                throw 1;
            }
            d1 = 0;
            d2 = 0;
            IsRunning = false;
            printf("Emulator halted!");
        }

        void ClockCycle(void) {
            if(!IsRunning) {
                printf("tried to cycle while CPU isn't running, exiting...");
                throw 1;
            }

            uint8_t CurrentInstruction = TotalRAM[ic];

            switch(CurrentInstruction) {
                case 0x00: //hlt
                    printf("hlt");
                    HaltEmulator();
                    return;

                case 0x01: //nop
                    ic++;
                    printf("nop");
                    return;

                case 0x02: //mov
                    uint8_t x = RAM::TotalRAM[ic + 1], y = RAM::TotalRAM[ic + 2];
                    if(y > sizeof(RAM::TotalRAM)) {
                        cerr << "MOV tried to write to " << y << "which is beyond availble RAM, exiting..." << endl;
                        throw 1;
                    } else {
                        RAM::TotalRAM[x] = RAM::TotalRAM[y];
                    }
                    return;

                case 0xA0: //and
                    d1 = d1 & d2;
                    ic++;
                    printf("and");
                    return;

                case 0xA1: //xor
                    d1 = d1 ^ d2;
                    ic++;
                    printf("xor");
                    return;

                case 0xB0: //add
                    d1 += d2;
                    ic++;
                    printf("add");
                    return;

                case 0xB1: //sub
                    d1 -= d2;
                    ic++;
                    printf("sub");
                    return;

                case 0xB2: //mul
                    d1 *= d2;
                    ic++;
                    printf("mul");
                    return;

                case 0xB3: //div
                    d1 /= d2;
                    ic++;
                    printf("div");
                    return;

                case 0xB4: //sqrt
                    sqrt(d1);
                    ic++;
                    printf("sqrt");
                    return;

                case 0xB5: //pow
                    pow(d1, d2);
                    ic++;
                    printf("pow");
                    return;

                case 0xC0: //jmp
                    uint8_t Address = TotalRAM[ic + 1] + TotalRAM[ic + 2];
                    ic = Address;
                    ic += 3;
                    printf("jmp");
                    return;

                case 0xC1: //je
                    printf("je");
                    ic++;
                    return;

                case 0xC2: //jne
                    printf("jne");
                    ic++;
                    return;
                
                case 0xC3: //jz
                    printf("jz");
                    ic++;
                    return;

                case 0xC4: //jnz
                    printf("jnz");
                    ic++;
                    return;

                case 0xD0: //mov
                    uint8_t a = TotalRAM[ic + 1], b = TotalRAM[ic + 2];
                    TotalRAM[a] = TotalRAM[b] //FIXME: idk what i'm doing
                    ic++;
                    printf("mov");
                    return;

                /*case 0xE0: //inb
                    ic++;
                    printf("inb");
                    return;

                case 0xE1: //outb
                    ic++;
                    printf("outb");
                    return;

                case 0xF0: //push
                    ic++;
                    printf("push");
                    return;

                case 0xF1: //pop
                    ic++;
                    printf("pop");
                    return;*/

                default:
                    cout << "Got unkown instruction " << CurrentInstruction << ", exiting..." << endl;
                    throw 1;
            }
        }
    }
}