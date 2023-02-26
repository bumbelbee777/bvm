#include <cstdint>
#include <Shared.h>

void Xand(uint128_t Destination, uint128_t Immediate1, uint128_t Immediate2) {
    Destination = Immediate1 & Immediate2;
}