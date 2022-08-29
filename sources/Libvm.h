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

    namespace ROM {
        vector<uint8_t> Read(const char* FileName);
    }
}