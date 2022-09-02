#include <Libvm.h>
#include <cmath>

using namespace Libvm::CPU;

namespace Libvm {
    namespace FPU {
        float add(float x, float y) {
            return x + y;
        }

        float sub(float x, float y) {
            return x - y;
        }

        float mul(float x, float y ) {
            return x * y;
        }

        float div(float x, float y) {
            return x / y;
        }

        float sqrt(float x) {
            return sqrt(x);
        }

        float pow(float x, float y) {
            return pow(x, y);
        }

    }
}