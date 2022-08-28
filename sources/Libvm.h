#pragma once

#include <cstddef>
#include <vector>
#include <string>

using namespace std;

namespace libvm {
    namespace CPU {
        void SetD1Data(uint8_t Value);
        void SetD2Data(uint8_t Value);
        uint8_t GetD1Data(void);
        uint8_t GetD2Data(void);
        void SwapRegisters(void);
        void StartCPU(/*uint8_t RamSize*/);
        void HaltCPU(void);
        void ClockCycle(void);
    }

    namespace RAM {
        uint8_t TotalRAM[0x70000];
    }

    namespace ROM {
        vector<uint8_t> Read(const char* FileName);
    }
}