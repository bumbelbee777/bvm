#include <Libvm.h>
#include <cstddef>
#include <iostream>
#include <stdint.h>

using namespace std;

namespace Libvm {
    namespace CPU {
        void inb(uint8_t Data, uint8_t Port) {
            if(Data >= 128) {
                cerr << "Tried to INB availble ports, exiting..." << endl;
                throw 1;
            } else {

            }
        }

        void outb(uint8_t Data, uint8_t Port) {
            if(Data >= 128) {
                cerr << "Tried to OUTB beyond availble ports, exiting..." << endl;
                throw 1;
            } else {
                
            }
        }

        void poke(uint8_t Address) {

        }

        void peek(uint8_t Address) {

        }
    }
}