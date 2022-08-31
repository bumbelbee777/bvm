#include <Libvm.h>
#include <cstddef>
#include <iostream>
#include <stdint.h>

using namespace std;

namespace libvm {
    namespace CPU {
        void mov(uint8_t x, uint8_t y) {
            if(y > sizeof(RAM::TotalRAM)) {
                cerr << "MOV tried to write to " << y << "which is beyond availble RAM, exiting..." << endl;
                throw 1;
            } else {
                RAM::TotalRAM[x] = RAM::TotalRAM[y];
            }
        }

        void inb(uint8_t Data, uint8_t Port) {}
        void outb(uint8_t Data, uint8_t Port) {}
        void poke(uint8_t Address) {}
        void peek(uint8_t Address) {}
    }
}