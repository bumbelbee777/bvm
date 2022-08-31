#pragma once

#include <cstddef>
#include <stdint.h>
#include <vector>
#include <string>

using namespace std;

namespace libvm {
    namespace RAM {
        extern uint8_t TotalRAM[0x70000];
    }
    namespace CPU {
        void StartEmulator(vector<uint8_t> ROM);
        void HaltEmulator(void);
        void ClockCycle(void);
        void mov(uint8_t x, uint8_t y);
    }

    namespace ROM {
        vector<uint8_t> Read(const char* FileName);
    }
}