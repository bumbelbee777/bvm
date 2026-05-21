#define FEATURE_FLAG 1
#define BASE 3

#ifdef FEATURE_FLAG
#define OFFSET 2
#else
#define OFFSET 0
#endif

import "lib/math.hc";

int apply_flags(int input) {
    int acc = input;
    acc |= 1;
    acc ^= 2;
    acc &= 15;
    acc += OFFSET;
    acc <<= 1;
    acc >>= 1;
    acc -= 1;
    acc *= 2;
    acc /= 2;
    acc %= 16;
    return acc;
}

int main(void) {
    int value = BASE;
    value = apply_flags(value);
    value += double_bits(mask_low(value));
    return value;
}
