#include "bench_clock.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int64_t kernel(void) {
    const int64_t n = 20000000;
    int64_t* items = (int64_t*)malloc((size_t)n * sizeof(int64_t));
    for (int64_t i = 0; i < n; ++i) {
        items[i] = i * 3 + 1;
    }
    int64_t total = 0;
    for (int64_t i = 0; i < n; ++i) {
        total += items[i];
    }
    free(items);
    return total;
}

int main(void) {
    (void)kernel();
    const int64_t started = bench_now_ns();
    const int64_t total = kernel();
    bench_report(bench_now_ns() - started);
    printf("%lld\n", (long long)total);
    return 0;
}
