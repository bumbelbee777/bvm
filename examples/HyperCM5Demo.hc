enum Op {
    Add = 1,
    Sub = 2,
    Mul = 3
};

int apply(int x, int code) {
    switch (code) {
        case Op::Add:
            return x + 10;
        case Op::Sub:
            return x - 5;
        case Op::Mul:
            return x * 2;
        default:
            return x;
    }
}

int main(void) {
    int a = (int)3.7f;
    float f = (float)42;
    char c = (char)1000;
    short s = (short)a;
    Op pick = Op::Mul;
    int r = apply(5, Op::Add);
    int r2 = apply(5, Op::Sub);
    int r3 = apply(5, pick);
    int r4 = apply(5, 99);
    return a + (int)f + (int)c + (int)s + (int)pick + r + r2 + r3 + r4;
}
