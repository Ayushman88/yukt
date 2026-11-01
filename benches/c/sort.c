#include "bench_clock.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static void kernel(int64_t* first, int64_t* last) {
    const int64_t n = 16000;
    int64_t* items = (int64_t*)malloc((size_t)n * sizeof(int64_t));
    for (int64_t i = 0; i < n; ++i) {
        items[i] = (n - i) * 17 + i;
    }
    const int64_t end = n - 2;
    for (int64_t i = 0; i <= end; ++i) {
        int64_t min = i;
        for (int64_t j = i + 1; j < n; ++j) {
            if (items[j] < items[min]) {
                min = j;
            }
        }
        const int64_t tmp = items[i];
        items[i] = items[min];
        items[min] = tmp;
    }
    *first = items[0];
    *last = items[n - 1];
    free(items);
}

int main(void) {
    int64_t first = 0;
    int64_t last = 0;
    kernel(&first, &last);
    const int64_t started = bench_now_ns();
    kernel(&first, &last);
    bench_report(bench_now_ns() - started);
    printf("%lld\n%lld\n", (long long)first, (long long)last);
    return 0;
}
