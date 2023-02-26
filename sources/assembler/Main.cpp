#include <cstring>
#include <fstream>
#include <iostream>
#include <Assembler.h>

using std::cerr;

int main(int argc, char **argv) {
    if(argc == 0 || argc < 1 || !strcmp(argv[2], "-o")) {
        cerr << "bas: No arguments provided." << '\n';
        return 1;
    } else {
        if(strcmp(argv[2], "-o")) {
            AssembleFile(argv[1], argv[3]);
            return 0;
        }
    }
}