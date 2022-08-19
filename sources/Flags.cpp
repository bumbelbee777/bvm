#include <iostream>
#include <string>

using namespace std;

namespace libvm {
    namespace Flags {
        bool Verbose = false;

        void DebugPrint(string Message, ...) {
            if(Verbose) {
                cout << "[DEBUG]" << Message << endl;
            }
        }
    }
}