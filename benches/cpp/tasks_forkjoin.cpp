#include "bench_clock.hpp"

#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

static constexpr int kN = 16;
static constexpr std::int64_t kInner = 500000;
static constexpr std::int64_t kMod = 1000003;

static std::int64_t work(std::int64_t n) {
    std::int64_t acc = n + 1;
    for (std::int64_t i = 1; i <= kInner; ++i) {
        acc = (acc * 17 + i) % kMod;
    }
    return acc;
}

static std::int64_t class_b() {
    std::vector<std::int64_t> outs(static_cast<std::size_t>(kN));
    std::vector<std::thread> threads;
    threads.reserve(static_cast<std::size_t>(kN));
    for (int i = 0; i < kN; ++i) {
        threads.emplace_back([&outs, i]() { outs[static_cast<std::size_t>(i)] = work(i); });
    }
    for (int i = kN - 1; i >= 0; --i) {
        threads[static_cast<std::size_t>(i)].join();
    }
    std::int64_t sum = 0;
    for (int i = kN - 1; i >= 0; --i) {
        sum += outs[static_cast<std::size_t>(i)];
    }
    return sum;
}

static std::int64_t class_c() {
    std::int64_t sum = 0;
    for (int i = 0; i < kN; ++i) {
        sum += work(i);
    }
    return sum;
}

int main(int argc, char** argv) {
    const std::string cls = argc > 1 ? argv[1] : "C";
    auto* kernel = cls == "B" ? class_b : class_c;
    (void)kernel();
    const std::int64_t started = bench_now_ns();
    const std::int64_t sum = kernel();
    bench_report(bench_now_ns() - started);
    std::cout << sum << '\n';
    return 0;
}
