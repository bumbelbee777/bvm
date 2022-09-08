#include <list>
#include <string>

using namespace std;

namespace Libvm {
    namespace Assembler {
        enum ArgumentType {
            Label,
            Register,
            Address,
            Number,
        };

        void AssembleFile(string InputFile, string OutputFileName);
    }
}