#include <Libvm.h>
#include <vector>
#include <iostream>

using namespace std;

namespace libvm {
    namespace RAM {
        uint8_t TotalRAM[0x70000]; //TODO: make RAM configurable by user, pobably a vector
        uint8_t ReadIndex(uint8_t Index) {
            if(Index == 0) {printf("error while reading index, exiting..."); return 1;}
            return TotalRAM[Index];
        }
    }
}