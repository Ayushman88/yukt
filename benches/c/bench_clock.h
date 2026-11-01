#ifndef YUKT_BENCH_CLOCK_H
#define YUKT_BENCH_CLOCK_H

#define _POSIX_C_SOURCE 200809L

#include <stdint.h>
#include <stdio.h>
#include <time.h>

static inline int64_t bench_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + (int64_t)ts.tv_nsec;
}

static inline void bench_report(int64_t ns) {
    fprintf(stderr, "compute_ns=%lld\n", (long long)ns);
}

#endif
