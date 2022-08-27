#include <cstddef>
#include <cstdio>
#include <Libvm.h>

using namespace std;
using namespace libvm::Flags;

namespace libvm {
    namespace CPU {
        bool IsRunning = false;
    
        uint8_t dreg1; //data register 1
        uint8_t dreg2; //data register 2
        int ic;        //instruction counter

        void SetDreg1Data(uint8_t Value) {
            printf("DREG1 has been set to %s", Value);
            dreg1 = Value;
        }

        void SetDreg2Data(uint8_t Value) {
            printf("DREG2 has been set to %s", Value);
            dreg2 = Value;
        }

        uint8_t GetDreg1Data(void) {
            return dreg1;
        }

        uint8_t GetDreg2Data(void) {
            return dreg2;
        }

        void SwapRegisters(void) {
            dreg1 = dreg2;
            dreg2 = dreg1;
            printf("Swapped DREG1 with DREG2");
        }

        void StartCPU(uint8_t RamSize) {
            dreg1 = 0;
            dreg2 = 0;
            IsRunning = true;
            printf("CPU is up and running!\n Availble RAM: %s", RamSize);
        }

        void HaltCPU(void) {
            dreg1 = 0;
            dreg2 = 0;
            IsRunning = false;
            printf("CPU halted!");
        }

        void ClockCycle(void) {
            if(!IsRunning) {
                printf("tried to cycle while CPU isn't running, exiting...");
                return;
            }

            uint8_t CurrentInstruction = RAM::TotalRAM[ic];

            switch(CurrentInstruction) {
                case 0x00: //hlt
                    printf("hlt");
                    HaltCPU();
                    return;

                case 0x01: //nop
                    printf("nop");
                    ic++;
                    return;

                default:
                    printf("Got unkown instruction, exiting...");
                    return;
            }
        }
    }
}