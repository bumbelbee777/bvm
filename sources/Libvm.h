#pragma once

#include <cstddef>
#include <vector>
#include <string>

using namespace std;

namespace libvm {
    namespace CPU {
        void Setd1Data(uint8_t Value);
        void Setd2Data(uint8_t Value);
        uint8_t Getd1Data(void);
        uint8_t Getd2Data(void);
        void SwapRegisters(void);
        void StartCPU(/*uint8_t RamSize*/);
        void HaltCPU(void);
        void ClockCycle(void);
    }

    namespace RAM {
        uint8_t ReadIndex(uint8_t Index); 
        uint8_t SetMemoryLocation(uint8_t Index, int Value);
    }

    namespace ROM {
        vector<uint8_t> Read(const char* FileName);
    }
}