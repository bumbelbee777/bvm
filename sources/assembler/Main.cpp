#include <fstream>
#include <iostream>
#include <Assembler.h>

using namespace std;
using namespace Libvm::Assembler;

int main(int argc, char **argv) {
    if(argc == 0 || argc < 1) {
        cerr << "bas: No arguments provided, exiting..." << endl;
        throw 1;
    } else {
        AssembleFile(argv[0], argv[1]);
        return 0;
    }
}