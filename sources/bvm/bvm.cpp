#include <Libvm.h>
#include <cstdio>
#include <iostream>
#include <string>

using namespace std;
using namespace Libvm;

int main() {
    CPU::StartEmulator(ROM::Read("test.rom"));
    return 0;
}
