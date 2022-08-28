#include <cstddef>
#include <iostream>
#include <cstdio>
#include <Libvm.h>

using namespace std;
using namespace libvm::RAM;

namespace libvm {
    namespace CPU {
        bool IsRunning = false;
    
        uint8_t d1 = 0; //data register 1
        uint8_t d2 = 0; //data register 2
        uint8_t sp; //stack pointer
        uint8_t ic; //instruction counter

        void SetD1Data(uint8_t Value) {
            cout << "d1 has been set to " << Value << endl;
            d1 = Value;
        }

        void SetD2Data(uint8_t Value) {
            cout << "d2 has been set to " << Value << endl;
            d2 = Value;
        }

        uint8_t GetD1Data(void) {
            return d1;
        }

        uint8_t GetD2Data(void) {
            return d2;
        }

        void SwapRegisters(void) {
            d1 = d2;
            d2 = d1;
            printf("Swapped d1 with d2");
        }

        void StartCPU() {
            if(IsRunning) {
                printf("Tried to start CPU while it's running, exiting...");
                throw 1;
            }
            IsRunning = true;
            cout << "CPU is up and running!" << endl;
        }

        void HaltCPU(void) {
            if(!IsRunning) {
                printf("Tried to halt CPU while it's not running, exiting...");
                throw 1;
            }
            d1 = 0;
            d2 = 0;
            IsRunning = false;
            printf("CPU halted!");
        }

        void ClockCycle(void) {
            if(!IsRunning) {
                printf("tried to cycle while CPU isn't running, exiting...");
                throw 1;
            }

            uint8_t CurrentInstruction = ic; //FIXME: this is temporary

            switch(CurrentInstruction) {
                case 0x00: //hlt
                    printf("hlt");
                    HaltCPU();
                    return;

                case 0x01: //nop
                    printf("nop");
                    ic++;
                    return;

                case 0xA0: //and
                    printf("and");
                    ic++;
                    return;

                case 0xA1: //or
                    printf("or");
                    ic++;
                    return;

                case 0xA2: //not
                    printf("not");
                    ic++;
                    return;

                case 0xB0: //add
                    printf("add");
                    ic++;
                    return;

                case 0xB1: //sub
                    ic++;
                    return;

                case 0xB2: //mul
                    printf("mul");
                    ic++;
                    return;

                case 0xB3: //div
                    printf("div");
                    ic++;
                    return;

                case 0xB4: //sqrt
                    printf("sqrt");
                    ic++;
                    return;

                case 0xB5: //pow
                    printf("pow");
                    ic++;
                    return;

                case 0xC0:
                    printf("jmp");
                    ic++;
                    return;

                case 0xC1:
                    printf("je");
                    ic++;
                    return;

                case 0xC2:
                    printf("jne");
                    ic++;
                    return;
                
                case 0xC3:
                    printf("jz");
                    ic++;
                    return;

                default:
                    cout << "Got unkown instruction " << CurrentInstruction << ", exiting..." << endl;
                    throw 1;
            }
        }
    }
}