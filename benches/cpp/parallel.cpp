#include "bench_clock.hpp"

#include <cstdint>
#include <iostream>
#include <thread>
#include <vector>

static void fill_range(std::int64_t* items, std::int64_t begin, std::int64_t end) {
    for (std::int64_t i = begin; i < end; ++i) {
        items[i] = i * 3 + 1;
    }
}

static std::int64_t kernel() {
    const std::int64_t n = 20000000;
    std::vector<std::int64_t> items(static_cast<std::size_t>(n));
    unsigned workers = std::thread::hardware_concurrency();
    if (workers == 0) {
        workers = 1;
    }
    std::vector<std::thread> threads;
    threads.reserve(workers);
    const std::int64_t chunk = (n + workers - 1) / workers;
    for (unsigned t = 0; t < workers; ++t) {
        const std::int64_t begin = static_cast<std::int64_t>(t) * chunk;
        std::int64_t end = begin + chunk;
        if (begin >= n) {
            break;
        }
        if (end > n) {
            end = n;
        }
        threads.emplace_back(fill_range, items.data(), begin, end);
    }
    for (auto& thread : threads) {
        thread.join();
    }
    std::int64_t total = 0;
    for (std::int64_t i = 0; i < n; ++i) {
        total += items[static_cast<std::size_t>(i)];
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
