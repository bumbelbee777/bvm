module math;

int double_bits(int value) {
    return value << 1;
}

int mask_low(int value) {
    return value & 15;
}
