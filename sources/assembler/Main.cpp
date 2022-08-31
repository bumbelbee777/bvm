#include <corecrt_wstdio.h>
#include <fstream>
#include <iostream>
#include <Assembler.h>

using namespace std;

namespace libvm {
    namespace Assembler {
        int main(int argc, char **argv) {
            if(argc == 0 || argc < 1) {
                cerr << "bas: No arguments provided, exiting..." << endl;
                throw 1;
            } else {
                AssembleFile(argv[0], argv[1]);
            }
        }
    }
}