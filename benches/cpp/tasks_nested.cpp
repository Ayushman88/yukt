#include "bench_clock.hpp"

#include <cstdint>
#include <iostream>
#include <string>
#include <thread>

static constexpr std::int64_t kDepth = 8;
static constexpr std::int64_t kInner = 500000;
static constexpr std::int64_t kMod = 1000003;

static std::int64_t leaf() {
    std::int64_t acc = 1;
    for (std::int64_t i = 1; i <= kInner; ++i) {
        acc = (acc * 17 + i) % kMod;
    }
    return acc;
}

static std::int64_t nest_b(std::int64_t n) {
    if (n < 1) {
        return leaf();
    }
    std::int64_t inner = 0;
    std::thread child([&inner, n]() { inner = nest_b(n - 1); });
    child.join();
    return inner + 1;
}

static std::int64_t nest_c(std::int64_t n) {
    if (n < 1) {
        return leaf();
    }
    return nest_c(n - 1) + 1;
}

int main(int argc, char** argv) {
    const std::string cls = argc > 1 ? argv[1] : "C";
    auto* kernel = cls == "B" ? +[]() { return nest_b(kDepth); } : +[]() { return nest_c(kDepth); };
    (void)kernel();
    const std::int64_t started = bench_now_ns();
    const std::int64_t sum = kernel();
    bench_report(bench_now_ns() - started);
    std::cout << sum << '\n';
    return 0;
}
