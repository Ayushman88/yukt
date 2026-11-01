#include "bench_clock.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static void kernel(int64_t* first, int64_t* last, int64_t* count) {
    const int64_t n = 4000000;
    const int64_t passes = 25;
    int64_t* items = (int64_t*)calloc((size_t)n, sizeof(int64_t));
    for (int64_t p = 1; p <= passes; ++p) {
        for (int64_t i = 0; i < n; ++i) {
            items[i] = i + p;
        }
    }
    *first = items[0];
    *last = items[n - 1];
    *count = n;
    free(items);
}

int main(void) {
    int64_t first = 0;
    int64_t last = 0;
    int64_t count = 0;
    kernel(&first, &last, &count);
    const int64_t started = bench_now_ns();
    kernel(&first, &last, &count);
    bench_report(bench_now_ns() - started);
    printf("%lld\n%lld\n%lld\n", (long long)first, (long long)last, (long long)count);
    return 0;
}
