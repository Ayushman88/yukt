#include "yukt_runtime.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int fail = 0;
#define REQUIRE(c)                                                                                 \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c);                            \
            fail = 1;                                                                              \
        }                                                                                          \
    } while (0)

static atomic_int g_runs;
static atomic_int g_flag;

static void write_seven(void* args, void* result) {
    (void)args;
    atomic_fetch_add(&g_runs, 1);
    if (result != NULL) {
        memcpy(result, &(int64_t){7}, sizeof(int64_t));
    }
}

static void write_void(void* args, void* result) {
    (void)args;
    (void)result;
    atomic_store(&g_flag, 1);
}

static void slow_seven(void* args, void* result) {
    (void)args;
    usleep(20000);
    atomic_fetch_add(&g_runs, 1);
    if (result != NULL) {
        memcpy(result, &(int64_t){7}, sizeof(int64_t));
    }
}

static void* waiter_thread(void* task) {
    int64_t out = 0;
    yukt_task_wait(task, &out);
    return (void*)(intptr_t)out;
}

static void* worker_thread(void* task) {
    yukt_task_worker_try(task);
    return NULL;
}

static void* alloc_stress(void* arg) {
    (void)arg;
    for (int i = 0; i < 1000; ++i) {
        char* s = yukt_str_from_lit("hello");
        YuktList* list = yukt_list_zeros(4);
        void* map = yukt_map_new();
        yukt_map_set_i64(map, "k", i);
        yukt_release(s);
        yukt_release(list);
        yukt_release(map);
    }
    return NULL;
}

int main(int argc, char** argv) {
    if (argc > 1 && strcmp(argv[1], "double-wait") == 0) {
        atomic_store(&g_runs, 0);
        void* task = yukt_task_start(write_seven, NULL, sizeof(int64_t), 8);
        int64_t out = 0;
        yukt_task_wait(task, &out);
        yukt_task_wait(task, &out);
        fprintf(stderr, "FAIL second wait did not abort\n");
        return 0;
    }

    {
        atomic_store(&g_runs, 0);
        void* task = yukt_task_start(write_seven, NULL, sizeof(int64_t), 8);
        int64_t out = 0;
        yukt_task_wait(task, &out);
        REQUIRE(out == 7);
        REQUIRE(atomic_load(&g_runs) == 1);
    }

    {
        atomic_store(&g_flag, 0);
        void* task = yukt_task_start(write_void, NULL, 0, 1);
        yukt_task_wait(task, NULL);
        REQUIRE(atomic_load(&g_flag) == 1);
    }

    {
        atomic_store(&g_runs, 0);
        void* task = yukt_task_start(slow_seven, NULL, sizeof(int64_t), 8);
        pthread_t worker;
        pthread_t waiter;
        pthread_create(&worker, NULL, worker_thread, task);
        pthread_create(&waiter, NULL, waiter_thread, task);
        pthread_join(worker, NULL);
        void* waited = NULL;
        pthread_join(waiter, &waited);
        REQUIRE((intptr_t)waited == 7);
        REQUIRE(atomic_load(&g_runs) == 1);
    }

    {
        pthread_t threads[8];
        for (int t = 0; t < 8; ++t) {
            pthread_create(&threads[t], NULL, alloc_stress, NULL);
        }
        for (int t = 0; t < 8; ++t) {
            pthread_join(threads[t], NULL);
        }
        REQUIRE(yukt_debug_live_allocs() == 0);
    }

    yukt_task_shutdown();
    yukt_runtime_release();
    REQUIRE(yukt_debug_live_allocs() == 0);
    return fail ? EXIT_FAILURE : EXIT_SUCCESS;
}
