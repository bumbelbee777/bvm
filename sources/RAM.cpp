#pragma once

#include <Libvm.h>
#include <iostream>

using namespace libvm::Flags;
using namespace std;

namespace libvm {
    namespace RAM {
        uint8_t ReadIndex(uint8_t Index) {
            if(Index == NULL /*|| Index > RAMSize*/) {DebugPrint("error while reading index, exiting..."); return 1;}
        }
    }
}