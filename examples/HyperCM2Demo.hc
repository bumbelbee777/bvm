unsigned accumulate(unsigned limit) {
    unsigned sum = 0;
    for (unsigned i = 0; i < limit; i = i + 1) {
        sum = sum + i;
    }
    return sum;
}

int main(void) {
    bool ok = true;
    char letter = 'A';
    signed bias = -1;
    long extra = 2;

    unsigned total = 0;
    while (total < 3) {
        total = total + 1;
    }

    if (!ok) {
        return 0;
    }
    if (letter != 65) {
        return 0;
    }

    return accumulate(5) + total + extra + bias;
}
