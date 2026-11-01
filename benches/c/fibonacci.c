#include "bench_clock.h"

#include <stdint.h>
#include <stdio.h>

static int64_t fib(int64_t n) {
    if (n < 2) {
        return n;
    }
    int64_t a = 0;
    int64_t b = 1;
    for (int64_t i = 2; i <= n; ++i) {
        const int64_t next = a + b;
        a = b;
        b = next;
    }
    return b;
}

static int64_t kernel(void) {
    volatile int64_t arg = 45;
    int64_t acc = 0;
    for (int64_t t = 1; t <= 1500000; ++t) {
        acc += fib(arg);
    }
    return acc;
}

int main(void) {
    (void)kernel();
    const int64_t started = bench_now_ns();
    const int64_t acc = kernel();
    bench_report(bench_now_ns() - started);
    printf("%lld\n", (long long)acc);
    return 0;
}
