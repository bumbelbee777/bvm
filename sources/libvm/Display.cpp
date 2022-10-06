#include <Libvm.h>
#include <stdint.h>

namespace Libvm {
    namespace Display {
        const int ScreenHeight = 800;
        const int ScreenWidth = 600;

        void SetPixel(unsigned x, unsigned y) {
            if(x >= ScreenWidth || y >= ScreenHeight) {
                throw 1;
            } else {

            }
        }

        Color::Color(uint8_t r, uint8_t g, uint8_t b) {
            
        }
    }
}