#ifndef YUKT_BENCH_TASK_POOL_H
#define YUKT_BENCH_TASK_POOL_H

#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "task_sizes.h"

#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

typedef void (*yukt_bench_job_fn)(int64_t arg, int64_t* out);

typedef struct {
    yukt_bench_job_fn fn;
    int64_t arg;
    int64_t* out;
    int done;
    pthread_mutex_t mu;
    pthread_cond_t cv;
} YuktBenchJob;

typedef struct YuktBenchNode {
    YuktBenchJob* job;
    struct YuktBenchNode* next;
} YuktBenchNode;

static pthread_mutex_t yukt_bench_qmu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t yukt_bench_qcv = PTHREAD_COND_INITIALIZER;
static YuktBenchNode* yukt_bench_qhead = NULL;
static YuktBenchNode* yukt_bench_qtail = NULL;
static pthread_t yukt_bench_threads[YUKT_BENCH_POOL_CAP];
static int yukt_bench_nthreads = 0;
static int yukt_bench_stop = 0;

static void* yukt_bench_worker(void* arg) {
    (void)arg;
    for (;;) {
        pthread_mutex_lock(&yukt_bench_qmu);
        while (!yukt_bench_stop && yukt_bench_qhead == NULL) {
            pthread_cond_wait(&yukt_bench_qcv, &yukt_bench_qmu);
        }
        if (yukt_bench_stop && yukt_bench_qhead == NULL) {
            pthread_mutex_unlock(&yukt_bench_qmu);
            return NULL;
        }
        YuktBenchNode* node = yukt_bench_qhead;
        yukt_bench_qhead = node->next;
        if (yukt_bench_qhead == NULL) {
            yukt_bench_qtail = NULL;
        }
        pthread_mutex_unlock(&yukt_bench_qmu);
        node->job->fn(node->job->arg, node->job->out);
        pthread_mutex_lock(&node->job->mu);
        node->job->done = 1;
        pthread_cond_broadcast(&node->job->cv);
        pthread_mutex_unlock(&node->job->mu);
        free(node);
    }
}

static int yukt_bench_ncpu(void) {
#if defined(_SC_NPROCESSORS_ONLN)
    long n = sysconf(_SC_NPROCESSORS_ONLN);
#else
    long n = 1;
#endif
    if (n < 1) {
        n = 1;
    }
    if (n > YUKT_BENCH_POOL_CAP) {
        n = YUKT_BENCH_POOL_CAP;
    }
    return (int)n;
}

static void yukt_bench_pool_start(void) {
    if (yukt_bench_nthreads > 0) {
        return;
    }
    yukt_bench_stop = 0;
    const int n = yukt_bench_ncpu();
    for (int t = 0; t < n; ++t) {
        if (pthread_create(&yukt_bench_threads[t], NULL, yukt_bench_worker, NULL) != 0) {
            break;
        }
        yukt_bench_nthreads = t + 1;
    }
}

static void yukt_bench_pool_join(void) {
    pthread_mutex_lock(&yukt_bench_qmu);
    yukt_bench_stop = 1;
    pthread_cond_broadcast(&yukt_bench_qcv);
    pthread_mutex_unlock(&yukt_bench_qmu);
    for (int t = 0; t < yukt_bench_nthreads; ++t) {
        pthread_join(yukt_bench_threads[t], NULL);
    }
    yukt_bench_nthreads = 0;
}

static YuktBenchJob* yukt_bench_start(yukt_bench_job_fn fn, int64_t arg, int64_t* out) {
    YuktBenchJob* job = (YuktBenchJob*)calloc(1, sizeof(YuktBenchJob));
    if (job == NULL) {
        abort();
    }
    job->fn = fn;
    job->arg = arg;
    job->out = out;
    pthread_mutex_init(&job->mu, NULL);
    pthread_cond_init(&job->cv, NULL);
    YuktBenchNode* node = (YuktBenchNode*)malloc(sizeof(YuktBenchNode));
    if (node == NULL) {
        abort();
    }
    node->job = job;
    node->next = NULL;
    pthread_mutex_lock(&yukt_bench_qmu);
    if (yukt_bench_qtail == NULL) {
        yukt_bench_qhead = node;
        yukt_bench_qtail = node;
    } else {
        yukt_bench_qtail->next = node;
        yukt_bench_qtail = node;
    }
    pthread_cond_signal(&yukt_bench_qcv);
    pthread_mutex_unlock(&yukt_bench_qmu);
    return job;
}

/* Claim a still-queued job and run it on the waiter. Matches YUKT's
   inline wait of a Queued task so nested start/wait cannot fill the
   locked worker set and deadlock. */
static int yukt_bench_try_claim(YuktBenchJob* job) {
    pthread_mutex_lock(&yukt_bench_qmu);
    YuktBenchNode* prev = NULL;
    YuktBenchNode* node = yukt_bench_qhead;
    while (node != NULL) {
        if (node->job == job) {
            if (prev == NULL) {
                yukt_bench_qhead = node->next;
            } else {
                prev->next = node->next;
            }
            if (yukt_bench_qtail == node) {
                yukt_bench_qtail = prev;
            }
            pthread_mutex_unlock(&yukt_bench_qmu);
            job->fn(job->arg, job->out);
            pthread_mutex_lock(&job->mu);
            job->done = 1;
            pthread_cond_broadcast(&job->cv);
            pthread_mutex_unlock(&job->mu);
            free(node);
            return 1;
        }
        prev = node;
        node = node->next;
    }
    pthread_mutex_unlock(&yukt_bench_qmu);
    return 0;
}

static int64_t yukt_bench_wait(YuktBenchJob* job) {
    if (!yukt_bench_try_claim(job)) {
        pthread_mutex_lock(&job->mu);
        while (!job->done) {
            pthread_cond_wait(&job->cv, &job->mu);
        }
        pthread_mutex_unlock(&job->mu);
    }
    const int64_t value = job->out != NULL ? *job->out : 0;
    pthread_mutex_destroy(&job->mu);
    pthread_cond_destroy(&job->cv);
    free(job);
    return value;
}

#endif
