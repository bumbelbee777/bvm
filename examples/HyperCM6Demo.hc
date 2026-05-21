void swap(int* a, int* b) {
    int tmp = *a;
    *a = *b;
    *b = tmp;
}

int sum_via_ptr(int* arr, int count) {
    int sum = 0;
    int i = 0;
    while (i < count) {
        sum = sum + arr[i];
        i = i + 1;
    }
    return sum;
}

int main(void) {
    int x = 10;
    int y = 20;
    swap(&x, &y);

    int nums[3] = {1, 2, 3};
    int* p = nums;
    int via_ptr = *p;
    int second = *(p + 1);
    int sum = sum_via_ptr(nums, 3);

    int null_check = 0;
    int* null_ptr = nullptr;
    if (null_ptr == nullptr) {
        null_check = 1;
    }

    return x + y + via_ptr + second + sum + null_check;
}
