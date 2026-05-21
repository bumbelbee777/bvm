#define ARRAY_LEN 4
#if defined(__HYPERC__) && __HYPERC__ >= 4
#define USE_BONUS 1
#else
#define USE_BONUS 0
#endif

const int folded = (int)(1.5 + 2.5);

int sum_array(const int values[ARRAY_LEN]) {
    int total = 0;
    int i = 0;
    while (i < ARRAY_LEN) {
        total += values[i];
        i = i + 1;
    }
    return total;
}

int main(void) {
    const short bias = 2;
    const int nums[ARRAY_LEN] = {1, 2, 3, 4};
    float scale = 2.0f;
    double offset = 1.0;
    int base = sum_array(nums);
    int pick = (base > 8) ? 10 : 5;
    int bump = (int)(scale + offset);
    int bonus = USE_BONUS ? 1 : 0;
    return base + bias + pick + bump + bonus + folded;
}
