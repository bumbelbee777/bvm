#include <Libvm.h>
#include <iostream>

using namespace libvm::Flags;
using namespace std;

namespace libvm {
    namespace RAM {
        uint8_t ReadIndex(long int Index) {
            if(Index == NULL /*|| Index > RAMSize*/) {DebugPrint("error while reading index, exiting..."); return 1;}
        }
    }
}