#include <Bvm.h>
#include <cstdio>
#include <iostream>
#include <string>

using namespace std;
using namespace Bvm;

int main(int argc, char **argv) {
    try {
        CPU::StartEmulator(ROM::Read(argv[1]));
    }
    catch(int exception) { 
        switch(exception) {
            case 1:
                cerr << "Internal emulator error occured." << endl;
                return 1;
            default:
                cerr << "Internal emulator error occured." << endl;
                return 1;
        }
    }
    return 0;
}
