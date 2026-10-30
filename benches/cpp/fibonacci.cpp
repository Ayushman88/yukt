#include "bench_clock.hpp"

#include <cstdint>
#include <iostream>

static std::int64_t fib(std::int64_t n) {
    if (n < 2) {
        return n;
    }
    std::int64_t a = 0;
    std::int64_t b = 1;
    for (std::int64_t i = 2; i <= n; ++i) {
        const std::int64_t next = a + b;
        a = b;
        b = next;
    }
    return b;
}

static std::int64_t kernel() {
    volatile std::int64_t arg = 45;
    std::int64_t acc = 0;
    for (std::int64_t t = 1; t <= 1500000; ++t) {
        acc += fib(arg);
    }
    return acc;
}

int main() {
    (void)kernel();
    const std::int64_t started = bench_now_ns();
    const std::int64_t acc = kernel();
    bench_report(bench_now_ns() - started);
    std::cout << acc << '\n';
    return 0;
}
