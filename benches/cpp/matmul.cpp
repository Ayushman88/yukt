#include "bench_clock.hpp"

#include <cstdint>
#include <iostream>
#include <vector>

struct Result {
    std::int64_t checksum = 0;
};

static Result kernel() {
    const std::int64_t n = 512;
    std::vector<std::int64_t> a(static_cast<std::size_t>(n * n));
    std::vector<std::int64_t> b(static_cast<std::size_t>(n * n));
    std::vector<std::int64_t> c(static_cast<std::size_t>(n * n), 0);
    for (std::int64_t i = 0; i < n; ++i) {
        for (std::int64_t j = 0; j < n; ++j) {
            a[static_cast<std::size_t>(i * n + j)] = i + j;
            b[static_cast<std::size_t>(i * n + j)] = i - j + 1;
        }
    }
    for (std::int64_t i = 0; i < n; ++i) {
        for (std::int64_t j = 0; j < n; ++j) {
            std::int64_t total = 0;
            for (std::int64_t k = 0; k < n; ++k) {
                total += a[static_cast<std::size_t>(i * n + k)] * b[static_cast<std::size_t>(k * n + j)];
            }
            c[static_cast<std::size_t>(i * n + j)] = total;
        }
    }
    Result result;
    for (std::int64_t i = 0; i < n * n; ++i) {
        result.checksum += c[static_cast<std::size_t>(i)];
    }
    return result;
}

int main() {
    (void)kernel();
    const std::int64_t started = bench_now_ns();
    const Result result = kernel();
    bench_report(bench_now_ns() - started);
    std::cout << result.checksum << '\n';
    return 0;
}
