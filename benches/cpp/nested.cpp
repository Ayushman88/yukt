#include "bench_clock.hpp"

#include <cstdint>
#include <iostream>
#include <vector>

static std::int64_t kernel(std::int64_t n) {
    std::vector<std::int64_t> items(static_cast<std::size_t>(n), 0);
    const std::int64_t last = n - 1;
    for (std::int64_t i = 0; i <= last; ++i) {
        for (std::int64_t j = 0; j <= last; ++j) {
            items[static_cast<std::size_t>(j)] += i;
        }
    }
    std::int64_t checksum = 0;
    for (std::int64_t j = 0; j <= last; ++j) {
        checksum += items[static_cast<std::size_t>(j)];
    }
    return checksum;
}

int main(int argc, char** argv) {
    (void)argv;
    const std::int64_t n = 8000 + (argc - 1);
    (void)kernel(n);
    const std::int64_t started = bench_now_ns();
    const std::int64_t checksum = kernel(n);
    bench_report(bench_now_ns() - started);
    std::cout << checksum << '\n';
    return 0;
}
