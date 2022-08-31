#include <list>
#include <string>

using namespace std;

namespace libvm {
    namespace Assembler {
        enum ArgumentType {
            Label,
            Register,
            Address,
            Number,
        };

        string CurrentLabel = ".default";

        void AssembleFile(string InputFile, string OutputFileName);
    }
}