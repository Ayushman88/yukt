#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "bench_clock.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

typedef struct {
    int64_t* items;
    int64_t begin;
    int64_t end;
} FillRange;

static void* fill_range(void* arg) {
    FillRange* range = (FillRange*)arg;
    for (int64_t i = range->begin; i < range->end; ++i) {
        range->items[i] = i * 3 + 1;
    }
    return NULL;
}

static int64_t kernel(void) {
    const int64_t n = 20000000;
    int64_t* items = (int64_t*)malloc((size_t)n * sizeof(int64_t));
#if defined(_SC_NPROCESSORS_ONLN)
    long workers = sysconf(_SC_NPROCESSORS_ONLN);
#else
    long workers = 1;
#endif
    if (workers < 1) {
        workers = 1;
    }
    pthread_t* threads = (pthread_t*)malloc((size_t)workers * sizeof(pthread_t));
    FillRange* ranges = (FillRange*)malloc((size_t)workers * sizeof(FillRange));
    const int64_t chunk = (n + workers - 1) / workers;
    int started = 0;
    for (long t = 0; t < workers; ++t) {
        const int64_t begin = t * chunk;
        if (begin >= n) {
            break;
        }
        int64_t end = begin + chunk;
        if (end > n) {
            end = n;
        }
        ranges[started].items = items;
        ranges[started].begin = begin;
        ranges[started].end = end;
        pthread_create(&threads[started], NULL, fill_range, &ranges[started]);
        started += 1;
    }
    for (int t = 0; t < started; ++t) {
        pthread_join(threads[t], NULL);
    }
    int64_t total = 0;
    for (int64_t i = 0; i < n; ++i) {
        total += items[i];
    }
    free(ranges);
    free(threads);
    free(items);
    return total;
}

int main(void) {
    (void)kernel();
    const int64_t started = bench_now_ns();
    const int64_t total = kernel();
    bench_report(bench_now_ns() - started);
    printf("%lld\n", (long long)total);
    return 0;
}
