#pragma once

#include <cstddef>
#include <vector>
#include <string>

using namespace std;

namespace libvm {
    namespace CPU {
        void SetDreg1Data(uint8_t Value);
        void SetDreg2Data(uint8_t Value);
        uint8_t GetDreg1Data(void);
        uint8_t GetDreg2Data(void);
        void StartCPU(long int RamSize);
        void HaltCPU(void);
        void ClockCycle(void);
    }

    namespace Flags {
        void DebugPrint(string Message, ...);
    }

    namespace RAM {
       uint8_t ReadIndex(uint8_t Index); 
    }

    namespace ROM {
        vector<uint8_t> Read(const char* FileName);
    }
}