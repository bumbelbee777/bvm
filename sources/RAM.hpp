#pragma once

#include <Flags.hpp>
#include <iostream>

using namespace libvm::Flags;
using namespace std;

namespace libvm {
    namespace RAM {
        uint8_t ReadIndex(uint8_t index) {
            if(index = NULL /*|| index > RAMSize*/) {DebugPrint("error while reading index, exiting..."); return 1;}
        }
    }
}