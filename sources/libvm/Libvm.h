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
        extern uint8_t d1;   //data register 1
        extern uint8_t d2;   //data register 2
        extern uint8_t sp;   //stack pointer
        extern uint8_t ic;   //instruction counter
        void StartEmulator(vector<uint8_t> ROM);
        void HaltEmulator(void);
        void ClockCycle(void);
    }

    namespace Display {
        extern const int ScreenHeight;
        extern const int ScreenWidth;
        void SetPixel();
        class Color {
            public:
                Color(uint8_t r, uint8_t g, uint8_t b);
                Color(uint8_t mono);
                Color(uint8_t mono, uint8_t r, uint8_t g, uint8_t b);
        };
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
        void RemoveDevice(Device *devices[129], uint8_t Id);
        void RemoveDevices(Device devices [129], struct Device *);


        #define FORDEVICES(s, i, d)             \
            usize i;                               \
            struct Device *d;                      \
            for (i = 0, d = &(s)->devices[i];   \
            i < 128;                \
            i++,d = &(s)->devices[i])   \

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