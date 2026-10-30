#pragma once

#include <chrono>
#include <cstdint>
#include <iostream>

template <typename T>
inline T bench_keep(T value) {
    asm volatile("" : "+r"(value));
    return value;
}

inline std::int64_t bench_now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

inline void bench_report(std::int64_t ns) {
    std::cerr << "compute_ns=" << ns << '\n';
}
