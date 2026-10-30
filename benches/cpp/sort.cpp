#include "bench_clock.hpp"

#include <cstdint>
#include <iostream>
#include <utility>
#include <vector>

struct Result {
    std::int64_t first = 0;
    std::int64_t last = 0;
};

static Result kernel() {
    const std::int64_t n = 16000;
    std::vector<std::int64_t> items(static_cast<std::size_t>(n));
    for (std::int64_t i = 0; i < n; ++i) {
        items[static_cast<std::size_t>(i)] = (n - i) * 17 + i;
    }
    const std::int64_t last = n - 2;
    for (std::int64_t i = 0; i <= last; ++i) {
        std::int64_t min = i;
        for (std::int64_t j = i + 1; j < n; ++j) {
            if (items[static_cast<std::size_t>(j)] < items[static_cast<std::size_t>(min)]) {
                min = j;
            }
        }
        std::swap(items[static_cast<std::size_t>(i)], items[static_cast<std::size_t>(min)]);
    }
    return Result{items.front(), items.back()};
}

int main() {
    (void)kernel();
    const std::int64_t started = bench_now_ns();
    const Result result = kernel();
    bench_report(bench_now_ns() - started);
    std::cout << result.first << '\n';
    std::cout << result.last << '\n';
    return 0;
}
