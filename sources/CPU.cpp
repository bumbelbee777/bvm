#include <cstddef>
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
            DebugPrint("DREG1 has been set to %s", Value);
            dreg1 = Value;
        }

        void SetDreg2Data(uint8_t Value) {
            DebugPrint("DREG2 has been set to %s", Value);
            dreg2 = Value;
        }

        uint8_t GetDreg1Data() {
            return dreg1;
        }

        uint8_t GetDreg2Data() {
            return dreg2;
        }

        void StartCPU(long int RamSize) {
            dreg1 = 0;
            dreg2 = 0;
            IsRunning = true;
            DebugPrint("CPU is up and running!\n Availble RAM: %s", RamSize);
        }

        void HaltCPU(void) {
            dreg1 = 0;
            dreg2 = 0;
            IsRunning = false;
            DebugPrint("CPU halted!");
        }

        void ClockCycle(void) {
            if(!IsRunning) {
                DebugPrint("tried to cycle while CPU isn't running");
                return;
            }

            CurrentInstruction = ic; //FIXME: add actual RAM

            switch(CurrentInstruction) {
                case 0x00: //hlt
                    HaltCPU();
            }
        }
    }
}