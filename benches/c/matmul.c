#include "bench_clock.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int64_t kernel(void) {
    const int64_t n = 512;
    const size_t cells = (size_t)(n * n);
    int64_t* a = (int64_t*)malloc(cells * sizeof(int64_t));
    int64_t* b = (int64_t*)malloc(cells * sizeof(int64_t));
    int64_t* c = (int64_t*)calloc(cells, sizeof(int64_t));
    for (int64_t i = 0; i < n; ++i) {
        for (int64_t j = 0; j < n; ++j) {
            a[i * n + j] = i + j;
            b[i * n + j] = i - j + 1;
        }
    }
    for (int64_t i = 0; i < n; ++i) {
        for (int64_t j = 0; j < n; ++j) {
            int64_t total = 0;
            for (int64_t k = 0; k < n; ++k) {
                total += a[i * n + k] * b[k * n + j];
            }
            c[i * n + j] = total;
        }
    }
    int64_t checksum = 0;
    for (int64_t i = 0; i < n * n; ++i) {
        checksum += c[i];
    }
    free(a);
    free(b);
    free(c);
    return checksum;
}

int main(void) {
    (void)kernel();
    const int64_t started = bench_now_ns();
    const int64_t checksum = kernel();
    bench_report(bench_now_ns() - started);
    printf("%lld\n", (long long)checksum);
    return 0;
}
