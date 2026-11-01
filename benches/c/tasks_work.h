#ifndef YUKT_BENCH_TASKS_WORK_H
#define YUKT_BENCH_TASKS_WORK_H

#include "task_sizes.h"

#include <stdint.h>

static int64_t yukt_bench_mix(int64_t seed, int64_t inner) {
    int64_t acc = seed + 1;
    for (int64_t i = 1; i <= inner; ++i) {
        acc = (acc * 17 + i) % YUKT_BENCH_MIX_MOD;
    }
    return acc;
}

static int64_t yukt_bench_work(int64_t n) {
    return yukt_bench_mix(n, YUKT_BENCH_TASK_INNER);
}

static int64_t yukt_bench_leaf(void) {
    return yukt_bench_mix(0, YUKT_BENCH_NEST_INNER);
}

static void yukt_bench_work_job(int64_t arg, int64_t* out) {
    *out = yukt_bench_work(arg);
}

#endif
