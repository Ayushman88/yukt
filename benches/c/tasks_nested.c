#include "bench_clock.h"
#include "task_pool.h"
#include "tasks_work.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int64_t nest_c(int64_t n);

static void nest_job(int64_t arg, int64_t* out) {
    *out = nest_c(arg);
}

static int64_t nest_c(int64_t n) {
    if (n < 1) {
        return yukt_bench_leaf();
    }
    int64_t out = 0;
    YuktBenchJob* job = yukt_bench_start(nest_job, n - 1, &out);
    return yukt_bench_wait(job) + 1;
}

typedef struct {
    int64_t n;
    int64_t* out;
} NestJob;

static int64_t nest_b(int64_t n);

static void* nest_thread(void* arg) {
    NestJob* job = (NestJob*)arg;
    *job->out = nest_b(job->n);
    return NULL;
}

static int64_t nest_b(int64_t n) {
    if (n < 1) {
        return yukt_bench_leaf();
    }
    int64_t inner = 0;
    NestJob job = {n - 1, &inner};
    pthread_t child;
    if (pthread_create(&child, NULL, nest_thread, &job) != 0) {
        abort();
    }
    pthread_join(child, NULL);
    return inner + 1;
}

static int64_t kernel_class_a(void) {
    return nest_c(YUKT_BENCH_NEST_DEPTH);
}

static int64_t kernel_class_b(void) {
    return nest_b(YUKT_BENCH_NEST_DEPTH);
}

static int64_t nest_seq(int64_t n) {
    if (n < 1) {
        return yukt_bench_leaf();
    }
    return nest_seq(n - 1) + 1;
}

static int64_t kernel_class_c(void) {
    return nest_seq(YUKT_BENCH_NEST_DEPTH);
}

int main(int argc, char** argv) {
    const char* cls = argc > 1 ? argv[1] : "A";
    int64_t (*kernel)(void) = kernel_class_a;
    if (cls[0] == 'B') {
        kernel = kernel_class_b;
    } else if (cls[0] == 'C') {
        kernel = kernel_class_c;
    } else {
        yukt_bench_pool_start();
    }
    (void)kernel();
    const int64_t started = bench_now_ns();
    const int64_t sum = kernel();
    bench_report(bench_now_ns() - started);
    if (cls[0] != 'B' && cls[0] != 'C') {
        yukt_bench_pool_join();
    }
    printf("%lld\n", (long long)sum);
    return 0;
}
