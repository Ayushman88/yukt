#include "yukt_runtime.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int g_failed = 0;
static int g_ipv6_skipped = 0;

static void expect(int cond, const char* msg) {
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", msg);
        g_failed = 1;
    }
}

static int setup_pair(void** ln_out, void** srv_out, void** cli_out) {
    if (yukt_net_listen_status("127.0.0.1:0") != 0) {
        return 0;
    }
    void* ln = yukt_net_listen_unchecked();
    if (ln == NULL) {
        return 0;
    }
    const int64_t port = yukt_net_port(ln);
    char addr[64];
    snprintf(addr, sizeof(addr), "127.0.0.1:%lld", (long long)port);
    if (yukt_net_connect_status(addr) != 0) {
        yukt_net_close(ln);
        return 0;
    }
    void* cli = yukt_net_connect_unchecked();
    if (yukt_net_accept_status(ln) != 0) {
        yukt_net_close(cli);
        yukt_net_close(ln);
        return 0;
    }
    void* srv = yukt_net_accept_unchecked();
    *ln_out = ln;
    *srv_out = srv;
    *cli_out = cli;
    return 1;
}

typedef struct AcceptArg {
    void* ln;
    int64_t st;
} AcceptArg;

static void* accept_thread(void* arg) {
    AcceptArg* a = (AcceptArg*)arg;
    a->st = yukt_net_accept_status(a->ln);
    return NULL;
}

static void* close_thread(void* end) {
    yukt_net_close(end);
    return NULL;
}

int main(int argc, char** argv) {
    if (argc > 1 && strcmp(argv[1], "bad-max") == 0) {
        void* ln = NULL;
        expect(yukt_net_listen_status("127.0.0.1:0") == 0, "listen");
        ln = yukt_net_listen_unchecked();
        (void)yukt_net_read_status(ln, 0);
        return 1;
    }
    expect(yukt_net_listen_status("not-an-addr") == 2, "malformed");
    expect(yukt_net_listen_status("127.0.0.1:0") == 0, "listen ephemeral");
    void* ln = yukt_net_listen_unchecked();
    expect(ln != NULL, "listener");
    int64_t port = yukt_net_port(ln);
    expect(port > 0, "ephemeral port");
    char* bound = yukt_net_addr(ln);
    expect(bound != NULL && strstr(bound, "127.0.0.1:") == bound, "addr prefix");
    yukt_net_close(ln);
    expect(yukt_net_accept_status(ln) == 1, "accept after close");

    void* pair_ln = NULL;
    void* srv = NULL;
    void* cli = NULL;
    expect(setup_pair(&pair_ln, &srv, &cli), "pair setup");
    expect(yukt_net_write_status(cli, "hello") == 0, "write hello");
    expect(yukt_net_write_unchecked() == 5, "wrote 5");
    expect(yukt_net_read_status(srv, 16) == 0, "read hello");
    char* got = yukt_net_read_unchecked();
    expect(got != NULL && strcmp(got, "hello") == 0, "hello bytes");
    yukt_net_close(cli);
    expect(yukt_net_read_status(srv, 16) == 1, "peer close read");
    yukt_net_close(srv);
    yukt_net_close(pair_ln);

    void* loc_ln = NULL;
    void* loc_srv = NULL;
    void* loc_cli = NULL;
    expect(setup_pair(&loc_ln, &loc_srv, &loc_cli), "local pair setup");
    yukt_net_close(loc_srv);
    expect(yukt_net_read_status(loc_srv, 16) == 1, "local close read");
    expect(yukt_net_write_status(loc_srv, "x") == 1, "local close write");
    yukt_net_close(loc_cli);
    yukt_net_close(loc_ln);

    const int64_t v6 = yukt_net_listen_status("[::1]:0");
    if (v6 == 2) {
        g_ipv6_skipped = 1;
        fprintf(stderr, "NOTE skip IPv6 [::1]:0 listen status 2\n");
    } else {
        expect(v6 == 0, "listen ipv6");
        void* ln6 = yukt_net_listen_unchecked();
        expect(ln6 != NULL, "ipv6 listener");
        const int64_t p6 = yukt_net_port(ln6);
        expect(p6 > 0, "ipv6 port");
        char* a6 = yukt_net_addr(ln6);
        char want6[64];
        snprintf(want6, sizeof(want6), "[::1]:%lld", (long long)p6);
        expect(a6 != NULL && strcmp(a6, want6) == 0, "ipv6 addr format");
        yukt_net_close(ln6);
    }

    expect(yukt_net_listen_status("localhost:0") == 0, "localhost bind");
    void* lnh = yukt_net_listen_unchecked();
    expect(lnh != NULL && yukt_net_port(lnh) > 0, "localhost port");
    yukt_net_close(lnh);

    expect(yukt_net_listen_status("127.0.0.1:0") == 0, "listen for blocked accept");
    void* blk = yukt_net_listen_unchecked();
    expect(blk != NULL, "blocked-accept listener");
    AcceptArg accept_arg;
    accept_arg.ln = blk;
    accept_arg.st = -1;
    pthread_t ath;
    expect(pthread_create(&ath, NULL, accept_thread, &accept_arg) == 0, "accept thread");
    usleep(50 * 1000);
    yukt_net_close(blk);
    expect(pthread_join(ath, NULL) == 0, "accept join");
    expect(accept_arg.st == 1, "blocked accept after close");

    expect(yukt_net_listen_status("127.0.0.1:0") == 0, "listen for double close");
    void* dc = yukt_net_listen_unchecked();
    expect(dc != NULL, "double-close listener");
    pthread_t c1;
    pthread_t c2;
    expect(pthread_create(&c1, NULL, close_thread, dc) == 0, "close thread 1");
    expect(pthread_create(&c2, NULL, close_thread, dc) == 0, "close thread 2");
    expect(pthread_join(c1, NULL) == 0, "close join 1");
    expect(pthread_join(c2, NULL) == 0, "close join 2");

    yukt_runtime_release();
    return g_failed ? 1 : 0;
}
