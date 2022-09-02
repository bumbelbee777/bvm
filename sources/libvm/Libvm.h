#pragma once

#include <cstddef>
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
        extern uint8_t d1;        //data register 1
        extern uint8_t d2;        //data register 2
        extern uint8_t sp;   //stack pointer
        extern uint8_t ic;   //instruction counter
        void StartEmulator(vector<uint8_t> ROM);
        void HaltEmulator(void);
        void ClockCycle(void);
    }

    namespace Display {
        const int ScreenHeight = 800;
        const int ScreenWidth = 600;
        void SetPixel();
    }

    namespace Device {
        struct Device {
            uint8_t Id;
            char name[64];
            void *state;
            void (*tick)(Device devices[129], struct Device *);
            uint8_t (*send)(Device devices[129], struct Device *);
            void (*receive)(Device devices[129], struct Device *, uint8_t Data);
            void (*destroy)(Device devices[129], struct Device *);
        };

        void AddDevice(Device devices[129], struct Device *);
        void RemoveDevice(Device devices[129], struct Device *);
        void RemoveDevices(Device devices [129], struct Device *);

        #define FOREACH_DEVICE(h, i, j)     \
            struct Device *j;               \
            for (i = 0, j = &(h)->devices[i];   \
            i < 129;                        \
            i++,j = &(h)->devices[i])       \

    }

    namespace FPU {
        float add(float x, float y);
        float sub(float x, float y);
        float mul(float x, float y);
        float div(float x, float y);
        float sqrt(float x);
        float pow(float x, float y);
    }

    namespace ROM {
        vector<uint8_t> Read(const char* FileName);
    }
}