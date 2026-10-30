#include "bench_clock.hpp"

#include <cstdint>
#include <iostream>
#include <thread>
#include <vector>

struct Result {
    std::int64_t checksum = 0;
};

static void row_range(std::int64_t* c, const std::int64_t* a, const std::int64_t* b, std::int64_t n,
                      std::int64_t i0, std::int64_t i1) {
    for (std::int64_t i = i0; i < i1; ++i) {
        for (std::int64_t j = 0; j < n; ++j) {
            std::int64_t total = 0;
            for (std::int64_t k = 0; k < n; ++k) {
                total += a[i * n + k] * b[k * n + j];
            }
            c[i * n + j] = total;
        }
    }
}

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
    unsigned workers = std::thread::hardware_concurrency();
    if (workers == 0) {
        workers = 1;
    }
    std::vector<std::thread> threads;
    threads.reserve(workers);
    const std::int64_t chunk = (n + workers - 1) / workers;
    for (unsigned t = 0; t < workers; ++t) {
        const std::int64_t i0 = static_cast<std::int64_t>(t) * chunk;
        std::int64_t i1 = i0 + chunk;
        if (i0 >= n) {
            break;
        }
        if (i1 > n) {
            i1 = n;
        }
        threads.emplace_back(row_range, c.data(), a.data(), b.data(), n, i0, i1);
    }
    for (auto& thread : threads) {
        thread.join();
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
