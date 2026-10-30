#define _POSIX_C_SOURCE 200809L

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

void yukt_main(void);

static int64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + (int64_t)ts.tv_nsec;
}

int main(void) {
    fflush(stdout);
    const int saved = dup(STDOUT_FILENO);
    const int devnull = open("/dev/null", O_WRONLY);
    if (saved >= 0 && devnull >= 0) {
        dup2(devnull, STDOUT_FILENO);
    }
    yukt_main();
    fflush(stdout);
    if (saved >= 0) {
        dup2(saved, STDOUT_FILENO);
        close(saved);
    }
    if (devnull >= 0) {
        close(devnull);
    }

    const int64_t started = now_ns();
    yukt_main();
    const int64_t elapsed = now_ns() - started;
    fprintf(stderr, "compute_ns=%lld\n", (long long)elapsed);
    return 0;
}
