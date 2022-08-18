#pragma once

#include <cstddef>
#include <Flags.hpp>

using namespace std;
using namespace libvm::Flags;

namespace libvm {
    class CPU {
        public:
            uint8_t dreg1; //data register 1
            uint8_t dreg2; //data register 2

            void SetDreg1Data(uint8_t Value) {
                DebugPrint("DREG1 has been set to %s", Value);
                dreg1 = Value;
            }

            void SetDreg2Data(uint8_t Value) {
                DebugPrint("DREG2 has been set to %s", Value);
                dreg2 = Value;
            }

            uint8_t GetDreg1Data(void) {
                return dreg1;
            }

            uint8_t GetDreg2Data(void) {
                return dreg2;
            }

            void StartCPU(uint8_t RamSize) {
                dreg1 = 0;
                dreg2 = 0;
                IsRunning = true;
                DebugPrint("CPU is up and running!");
            }

            void HaltCPU(void) {
                dreg1 = 0;
                dreg2 = 0;
                IsRunning = false;
                DebugPrint("CPU halted!");
            }

        private:
            bool IsRunning = false; 
    };
}