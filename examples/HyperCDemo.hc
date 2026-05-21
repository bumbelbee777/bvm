int add(int a, int b) {
    return a + b;
}

int main(void) {
    int x = 5;
    int y = 7;
    if (x < y) {
        return add(x, y);
    }
    return 0;
}
