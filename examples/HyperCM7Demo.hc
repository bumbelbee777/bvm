int read_checked(int* p) {
    if (p == nullptr) {
        return -1;
    }
    return *p;
}

int main(void) {
    int data[4] = {2, 4, 6, 8};
    int* base = data;
    int a = base[0];
    int b = base[1];
    int c = read_checked(nullptr);
    return a + b + c;
}
