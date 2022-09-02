#include <Libvm.h>
#include <cstdio>
#include <iostream>

using namespace std;
using namespace Libvm;

int main(int argc, char *argv) {
    if(argc == 0 || argc < 1) {
        cout << "bvm: No arguments provided." << endl;
        return 0;
    } else {
        CPU::StartEmulator(ROM::Read(argv[1]));
    }
}