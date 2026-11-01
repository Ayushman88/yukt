#include "bench_clock.h"
#include "task_pool.h"
#include "tasks_work.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int64_t kernel_class_a(void) {
    int64_t outs[YUKT_BENCH_TASK_N];
    YuktBenchJob* jobs[YUKT_BENCH_TASK_N];
    for (int i = 0; i < YUKT_BENCH_TASK_N; ++i) {
        jobs[i] = yukt_bench_start(yukt_bench_work_job, i, &outs[i]);
    }
    int64_t sum = 0;
    for (int i = YUKT_BENCH_TASK_N - 1; i >= 0; --i) {
        sum += yukt_bench_wait(jobs[i]);
    }
    return sum;
}

static void* thread_job(void* arg) {
    const int64_t n = (int64_t)(intptr_t)arg;
    int64_t* slot = (int64_t*)malloc(sizeof(int64_t));
    *slot = yukt_bench_work(n);
    return slot;
}

static int64_t kernel_class_b(void) {
    pthread_t threads[YUKT_BENCH_TASK_N];
    for (int i = 0; i < YUKT_BENCH_TASK_N; ++i) {
        pthread_create(&threads[i], NULL, thread_job, (void*)(intptr_t)i);
    }
    int64_t sum = 0;
    for (int i = YUKT_BENCH_TASK_N - 1; i >= 0; --i) {
        void* slot = NULL;
        pthread_join(threads[i], &slot);
        sum += *(int64_t*)slot;
        free(slot);
    }
    return sum;
}

static int64_t kernel_class_c(void) {
    int64_t sum = 0;
    for (int i = 0; i < YUKT_BENCH_TASK_N; ++i) {
        sum += yukt_bench_work(i);
    }
    return sum;
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
