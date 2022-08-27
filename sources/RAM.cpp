#include <Libvm.h>
#include <vector>
#include <iostream>

using namespace std;

namespace libvm {
    namespace RAM {
        uint8_t ReadIndex(uint8_t Index) {
            if(Index == 0 /*|| Index > RAMSize*/) {printf("error while reading index, exiting..."); return 1;}
            return RAM[Index];
        }
    }
}