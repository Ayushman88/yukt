#include "yukt_runtime.h"

#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static int g_failed = 0;
static void expect(int cond, const char* msg) {
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", msg);
        g_failed = 1;
    }
}

static void drop_nop(void* p) { (void)p; }

static void* g_drop_arg = NULL;
static int g_drop_called = 0;

static void drop_capture(void* p) {
    g_drop_called = 1;
    g_drop_arg = p;
}

int main(int argc, char** argv) {
    if (argc > 1 && strcmp(argv[1], "double-receiver") == 0) {
        void* tx = yukt_sync_channel();
        void* rx = yukt_sync_receiver(tx);
        (void)rx;
        (void)yukt_sync_receiver(tx);
        return 1;
    }
    if (argc > 1 && strcmp(argv[1], "send-closed") == 0) {
        void* tx = yukt_sync_channel();
        void* rx = yukt_sync_receiver(tx);
        yukt_sync_close(tx);
        int64_t v = 1;
        yukt_sync_send(tx, &v, (int64_t)sizeof(v), drop_nop);
        return 1;
    }
    if (argc > 1 && strcmp(argv[1], "send-recv-closed") == 0) {
        void* tx = yukt_sync_channel();
        void* rx = yukt_sync_receiver(tx);
        yukt_sync_close(rx);
        int64_t v = 1;
        yukt_sync_send(tx, &v, (int64_t)sizeof(v), drop_nop);
        return 1;
    }
    void* tx = yukt_sync_channel();
    void* rx = yukt_sync_receiver(tx);
    int64_t v = 7;
    yukt_sync_send(tx, &v, (int64_t)sizeof(v), drop_nop);
    yukt_sync_close(tx);
    expect(yukt_sync_recv_status(rx) == 0, "first recv ok");
    int64_t out = 0;
    yukt_sync_recv_unchecked(&out, (int64_t)sizeof(out));
    expect(out == 7, "payload 7");
    expect(yukt_sync_recv_status(rx) == 1, "closed after drain");
    yukt_sync_close(rx);
    yukt_sync_close(rx);

    void* tx_drop = yukt_sync_channel();
    void* rx_drop = yukt_sync_receiver(tx_drop);
    int64_t queued = 7;
    yukt_sync_send(tx_drop, &queued, (int64_t)sizeof(queued), drop_capture);
    yukt_sync_close(rx_drop);
    expect(g_drop_called, "queued drop on recv close");
    expect(g_drop_arg != (void*)(intptr_t)7, "drop gets payload address not loaded 7");
    yukt_sync_close(tx_drop);

    yukt_runtime_release();
    return g_failed ? 1 : 0;
}
