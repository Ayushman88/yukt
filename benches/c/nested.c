#include "bench_clock.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int64_t kernel(int64_t n) {
    int64_t* items = (int64_t*)calloc((size_t)n, sizeof(int64_t));
    const int64_t last = n - 1;
    for (int64_t i = 0; i <= last; ++i) {
        for (int64_t j = 0; j <= last; ++j) {
            items[j] += i;
        }
    }
    int64_t checksum = 0;
    for (int64_t j = 0; j <= last; ++j) {
        checksum += items[j];
    }
    free(items);
    return checksum;
}

int main(int argc, char** argv) {
    (void)argv;
    const int64_t n = 8000 + (argc - 1);
    (void)kernel(n);
    const int64_t started = bench_now_ns();
    const int64_t checksum = kernel(n);
    bench_report(bench_now_ns() - started);
    printf("%lld\n", (long long)checksum);
    return 0;
}
