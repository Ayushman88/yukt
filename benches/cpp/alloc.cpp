#include "bench_clock.hpp"

#include <cstdint>
#include <iostream>
#include <vector>

struct Result {
    std::int64_t first = 0;
    std::int64_t last = 0;
    std::int64_t count = 0;
};

static Result kernel() {
    const std::int64_t n = 4000000;
    const std::int64_t passes = 25;
    std::vector<std::int64_t> items(static_cast<std::size_t>(n), 0);
    for (std::int64_t p = 1; p <= passes; ++p) {
        for (std::int64_t i = 0; i < n; ++i) {
            items[static_cast<std::size_t>(i)] = i + p;
        }
    }
    return Result{items.front(), items.back(), static_cast<std::int64_t>(items.size())};
}

int main() {
    (void)kernel();
    const std::int64_t started = bench_now_ns();
    const Result result = kernel();
    bench_report(bench_now_ns() - started);
    std::cout << result.first << '\n';
    std::cout << result.last << '\n';
    std::cout << result.count << '\n';
    return 0;
}
