#include "bench_clock.hpp"

#include <cstdint>
#include <iostream>
#include <vector>

static std::int64_t kernel() {
    const std::int64_t n = 20000000;
    std::vector<std::int64_t> items(static_cast<std::size_t>(n));
    std::int64_t* data = items.data();
    for (std::int64_t i = 0; i < n; ++i) {
        data[i] = i * 3 + 1;
    }
    std::int64_t total = 0;
    for (std::int64_t i = 0; i < n; ++i) {
        total += data[i];
    }
    return total;
}

int main() {
    (void)kernel();
    const std::int64_t started = bench_now_ns();
    const std::int64_t total = kernel();
    bench_report(bench_now_ns() - started);
    std::cout << total << '\n';
    return 0;
}
