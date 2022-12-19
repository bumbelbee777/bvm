#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include <string>

using namespace std;

namespace Bvm {
    namespace RAM {
        extern uint64_t TotalRAM[0x70000];
    }
    namespace CPU {
        extern bool IsRunning;
        extern uint64_t r1;   //register 1
        extern uint64_t d2;   //register 2
        extern uint64_t r3;  //register 3
        extern uint64_t sp;   //stack pointer
        extern uint64_t ic;   //instruction counter
        extern uint64_t zf;   //zero flag
        void StartEmulator(vector<uint64_t> ROM);
        void HaltEmulator(void);
        void ClockCycle(void);
    }
    namespace ROM {
        vector<uint64_t> Read(const char* FileName);
    }
}