#include "yukt_runtime.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>

static int g_handler_ran = 0;

static void handler(const char* method, const char* path, const char* query, void* headers,
                    const char* body, int64_t* out_status, void** out_headers, char** out_body) {
    (void)method;
    (void)path;
    (void)query;
    (void)headers;
    (void)body;
    g_handler_ran = 1;
    *out_status = 200;
    *out_headers = NULL;
    *out_body = (char*)"";
    fprintf(stderr, "handler-called\n");
}

static void* serve_thread(void* ln) {
    yukt_http_serve_on(ln, handler);
    return NULL;
}

int main(void) {
    if (yukt_net_listen_status("127.0.0.1:0") != 0) {
        return 1;
    }
    void* ln = yukt_net_listen_unchecked();
    char* addr = yukt_net_addr(ln);
    pthread_t th;
    pthread_create(&th, NULL, serve_thread, ln);
    if (yukt_net_connect_status(addr) != 0) {
        return 1;
    }
    void* c = yukt_net_connect_unchecked();
    if (yukt_net_write_status(c, "NOPE\r\n\r\n") != 0) {
        return 1;
    }
    if (yukt_net_read_status(c, 64) != 0) {
        fprintf(stderr, "no response\n");
        return 1;
    }
    char* got = yukt_net_read_unchecked();
    yukt_net_close(c);
    yukt_net_close(ln);
    pthread_join(th, NULL);
    if (got == NULL || strstr(got, "400") == NULL) {
        fprintf(stderr, "got %s\n", got ? got : "(null)");
        return 1;
    }
    if (g_handler_ran) {
        fprintf(stderr, "handler ran\n");
        return 1;
    }
    return 0;
}
