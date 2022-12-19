#include <fstream>
#include <iostream>
#include <Assembler.h>

using namespace std;
using namespace Bas::Assembler;

int main(int argc, char **argv) {
    if(argc == 0 || argc < 1) {
        cerr << "bas: No arguments provided." << endl;
        return 1;
    } else {
        AssembleFile(argv[1], argv[2]);
        return 0;
    }
}