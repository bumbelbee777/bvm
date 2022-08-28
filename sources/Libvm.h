#pragma once

#include <cstddef>
#include <vector>
#include <string>

using namespace std;

namespace libvm {
    namespace CPU {
        void StartEmulator(vector<uint8_t> ROM);
        void HaltEmulator(void);
        void ClockCycle(void);
    }

    namespace RAM {
        uint8_t TotalRAM[0x70000];
    }

    namespace ROM {
        vector<uint8_t> Read(const char* FileName);
    }
}