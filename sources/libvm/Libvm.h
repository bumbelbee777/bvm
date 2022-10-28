#pragma once

#include <cstddef>
#include <cstdint>
#include <stdint.h>
#include <vector>
#include <string>

using namespace std;

namespace Libvm {
    namespace RAM {
        extern uint8_t TotalRAM[0x70000];
    }
    namespace CPU {
        extern bool IsRunning;
        extern uint8_t d1;   //data register 1
        extern uint8_t d2;   //data register 2
        extern uint8_t sp;   //stack pointer
        extern uint8_t ic;   //instruction counter
        extern uint8_t zf;   //zero flag
        void StartEmulator(vector<uint8_t> ROM);
        void HaltEmulator(void);
        void ClockCycle(void);
    }

    namespace Display {
    }

    namespace Device {
        class Device {
            public:
                uint8_t Id;
                char name[64];
                void *state;
                void *tick(Device devices[129]);
                uint8_t *send(Device devices[129]);
                void *receive(Device devices[129], uint8_t Data);
                void *destroy(Device devices[129]);
        };

        void AddDevice(Device devices[129]);
        void RemoveDevice(Device *devices[129], uint8_t Id);
        void RemoveDevices(Device devices [129]);


        #define FORDEVICES(s, i, d)             \
            usize i;                            \
            Device *d;                   \
            for(i = 0, d = &(s)->devices[i];    \
            i < 128;                            \
            i++,d = &(s)->devices[i])           \

    }

    namespace ROM {
        vector<uint8_t> Read(const char* FileName);
    }
}