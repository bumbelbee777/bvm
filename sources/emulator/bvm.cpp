#include <Libvm.h>
#include <cstdio>
#include <iostream>

using namespace std;
using namespace libvm;

int main(int argc, char *argv[]) {
    if(argc == 0) {
        cout << "bvm: No arguments provided." << endl;
    } else {
        ROM::Read(argv[0]);
        CPU::StartEmulator();
    }
}