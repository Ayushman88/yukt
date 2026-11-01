#include "yukt_runtime.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failed = 0;

static void expect(int cond, const char* msg) {
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", msg);
        g_failed = 1;
    }
}

static void* worker_read(void* arg) {
    (void)arg;
    int64_t status = 0;
    (void)yukt_io_read_line(&status);
    return NULL;
}

int main(int argc, char** argv) {
    char* prog[] = {(char*)"prog", NULL};
    yukt_io_set_args(1, prog);
    yukt_origin_init();
    if (argc > 1 && strcmp(argv[1], "worker-stdin") == 0) {
        pthread_t th;
        pthread_create(&th, NULL, worker_read, NULL);
        pthread_join(th, NULL);
        fprintf(stderr, "worker returned without trap\n");
        return 1;
    }
    expect(yukt_is_origin_thread() == 1, "origin thread should be self after init");
    expect(strcmp(yukt_io_program(), "prog") == 0, "program name");
    return g_failed ? 1 : 0;
}
