#include "yukt_runtime.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

typedef struct YuktNet {
    atomic_int fd;
    atomic_int closed;
    int is_listener;
    int family;
    char host[256];
    int port;
} YuktNet;

static int yukt_net_load_fd(const YuktNet* net) {
    return atomic_load_explicit(&net->fd, memory_order_acquire);
}

static int yukt_net_load_closed(const YuktNet* net) {
    return atomic_load_explicit(&net->closed, memory_order_acquire);
}

static _Thread_local void* yukt_net_ptr = NULL;
static _Thread_local char* yukt_net_str = NULL;
static _Thread_local int64_t yukt_net_i64 = 0;
static _Thread_local int64_t yukt_net_st = 0;

static int yukt_net_port_numeric(const char* p) {
    if (p == NULL || p[0] == '\0') {
        return 0;
    }
    for (const char* c = p; *c != '\0'; ++c) {
        if (*c < '0' || *c > '9') {
            return 0;
        }
    }
    return 1;
}

static int yukt_net_parse(const char* s, char* host, size_t host_n, char* port, size_t port_n) {
    if (s == NULL || s[0] == '\0' || host == NULL || port == NULL || host_n == 0 || port_n == 0) {
        return -1;
    }
    const char* host_s = NULL;
    const char* port_s = NULL;
    size_t host_len = 0;
    if (s[0] == '[') {
        const char* rb = strchr(s, ']');
        if (rb == NULL || rb[1] != ':') {
            return -1;
        }
        host_s = s + 1;
        host_len = (size_t)(rb - host_s);
        port_s = rb + 2;
    } else {
        const char* colon = strrchr(s, ':');
        if (colon == NULL) {
            return -1;
        }
        host_s = s;
        host_len = (size_t)(colon - s);
        port_s = colon + 1;
    }
    if (host_len == 0 || host_len >= host_n) {
        return -1;
    }
    if (!yukt_net_port_numeric(port_s) || strlen(port_s) >= port_n) {
        return -1;
    }
    memcpy(host, host_s, host_len);
    host[host_len] = '\0';
    memcpy(port, port_s, strlen(port_s) + 1);
    return 0;
}

static void yukt_net_set_nosigpipe(int fd) {
#ifdef SO_NOSIGPIPE
    int one = 1;
    (void)setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#else
    (void)fd;
#endif
}

static void yukt_net_fill_from_sa(YuktNet* net, const struct sockaddr* sa) {
    if (net == NULL || sa == NULL) {
        return;
    }
    net->family = sa->sa_family;
    if (sa->sa_family == AF_INET) {
        const struct sockaddr_in* in = (const struct sockaddr_in*)sa;
        if (inet_ntop(AF_INET, &in->sin_addr, net->host, sizeof(net->host)) == NULL) {
            net->host[0] = '\0';
        }
        net->port = (int)ntohs(in->sin_port);
        return;
    }
    if (sa->sa_family == AF_INET6) {
        const struct sockaddr_in6* in6 = (const struct sockaddr_in6*)sa;
        if (inet_ntop(AF_INET6, &in6->sin6_addr, net->host, sizeof(net->host)) == NULL) {
            net->host[0] = '\0';
        }
        net->port = (int)ntohs(in6->sin6_port);
    }
}

static void yukt_net_fill_local(YuktNet* net) {
    if (net == NULL) {
        return;
    }
    const int fd = yukt_net_load_fd(net);
    if (fd < 0) {
        return;
    }
    struct sockaddr_storage ss;
    memset(&ss, 0, sizeof(ss));
    socklen_t len = (socklen_t)sizeof(ss);
    if (getsockname(fd, (struct sockaddr*)&ss, &len) != 0) {
        return;
    }
    yukt_net_fill_from_sa(net, (struct sockaddr*)&ss);
}

static YuktNet* yukt_net_make(int fd, int is_listener, const struct sockaddr* peer) {
    YuktNet* net = (YuktNet*)calloc(1, sizeof(YuktNet));
    if (net == NULL) {
        close(fd);
        die("out of memory");
    }
    atomic_init(&net->fd, fd);
    atomic_init(&net->closed, 0);
    net->is_listener = is_listener;
    yukt_net_set_nosigpipe(fd);
    if (peer != NULL) {
        yukt_net_fill_from_sa(net, peer);
    } else {
        yukt_net_fill_local(net);
    }
    const int kind = is_listener ? YUKT_ALLOC_LISTENER : YUKT_ALLOC_CONN;
    yukt_track(kind, net, sizeof(YuktNet));
    return net;
}

static int yukt_net_open(const char* addr, int do_listen, YuktNet** out) {
    char host[256];
    char port[16];
    if (yukt_net_parse(addr, host, sizeof(host), port, sizeof(port)) != 0) {
        return 2;
    }
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    if (do_listen) {
        hints.ai_flags = AI_PASSIVE;
    }
    struct addrinfo* res = NULL;
    if (getaddrinfo(host, port, &hints, &res) != 0 || res == NULL) {
        if (res != NULL) {
            freeaddrinfo(res);
        }
        return 2;
    }
    int fd = -1;
    for (struct addrinfo* ai = res; ai != NULL; ai = ai->ai_next) {
        fd = (int)socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) {
            continue;
        }
        if (ai->ai_family == AF_INET6) {
            int one = 1;
            (void)setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &one, sizeof(one));
        }
        if (do_listen) {
            int one = 1;
            (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
            if (bind(fd, ai->ai_addr, ai->ai_addrlen) != 0 || listen(fd, SOMAXCONN) != 0) {
                close(fd);
                fd = -1;
                continue;
            }
            break;
        }
        if (connect(fd, ai->ai_addr, ai->ai_addrlen) != 0) {
            close(fd);
            fd = -1;
            continue;
        }
        break;
    }
    freeaddrinfo(res);
    if (fd < 0) {
        return 2;
    }
    *out = yukt_net_make(fd, do_listen, NULL);
    return 0;
}

void yukt_net_close(void* end) {
    YuktNet* net = (YuktNet*)end;
    if (net == NULL) {
        return;
    }
    int expected = 0;
    if (!atomic_compare_exchange_strong_explicit(&net->closed, &expected, 1, memory_order_acq_rel,
                                                memory_order_acquire)) {
        return;
    }
    const int fd = atomic_exchange_explicit(&net->fd, -1, memory_order_acq_rel);
    if (fd >= 0) {
        (void)shutdown(fd, SHUT_RDWR);
        close(fd);
    }
}

int64_t yukt_net_listen_status(const char* addr) {
    yukt_net_ptr = NULL;
    YuktNet* net = NULL;
    const int st = yukt_net_open(addr, 1, &net);
    yukt_net_st = st;
    if (st == 0) {
        yukt_net_ptr = net;
    }
    return yukt_net_st;
}

void* yukt_net_listen_unchecked(void) {
    void* p = yukt_net_ptr;
    yukt_net_ptr = NULL;
    return p;
}

int64_t yukt_net_connect_status(const char* addr) {
    yukt_net_ptr = NULL;
    YuktNet* net = NULL;
    const int st = yukt_net_open(addr, 0, &net);
    yukt_net_st = st;
    if (st == 0) {
        yukt_net_ptr = net;
    }
    return yukt_net_st;
}

void* yukt_net_connect_unchecked(void) {
    void* p = yukt_net_ptr;
    yukt_net_ptr = NULL;
    return p;
}

int64_t yukt_net_accept_status(void* listener) {
    yukt_net_ptr = NULL;
    YuktNet* ln = (YuktNet*)listener;
    if (ln == NULL || yukt_net_load_closed(ln) || yukt_net_load_fd(ln) < 0) {
        yukt_net_st = 1;
        return 1;
    }
    const int fd = yukt_net_load_fd(ln);
    if (fd < 0 || yukt_net_load_closed(ln)) {
        yukt_net_st = 1;
        return 1;
    }
    struct sockaddr_storage ss;
    int cfd;
    do {
        memset(&ss, 0, sizeof(ss));
        socklen_t slen = (socklen_t)sizeof(ss);
        cfd = (int)accept(fd, (struct sockaddr*)&ss, &slen);
    } while (cfd < 0 && errno == EINTR);
    if (yukt_net_load_closed(ln)) {
        if (cfd >= 0) {
            close(cfd);
        }
        yukt_net_st = 1;
        return 1;
    }
    if (cfd < 0) {
        yukt_net_st = 2;
        return 2;
    }
    YuktNet* conn = yukt_net_make(cfd, 0, (struct sockaddr*)&ss);
    yukt_net_ptr = conn;
    yukt_net_st = 0;
    return 0;
}

void* yukt_net_accept_unchecked(void) {
    void* p = yukt_net_ptr;
    yukt_net_ptr = NULL;
    return p;
}

char* yukt_net_addr(void* listener) {
    YuktNet* net = (YuktNet*)listener;
    if (net == NULL) {
        return yukt_str_from_lit("");
    }
    yukt_net_fill_local(net);
    char buf[320];
    if (net->family == AF_INET6) {
        snprintf(buf, sizeof(buf), "[%s]:%d", net->host, net->port);
    } else {
        snprintf(buf, sizeof(buf), "%s:%d", net->host, net->port);
    }
    return yukt_str_from_lit(buf);
}

int64_t yukt_net_port(void* listener) {
    YuktNet* net = (YuktNet*)listener;
    if (net == NULL) {
        return 0;
    }
    yukt_net_fill_local(net);
    return (int64_t)net->port;
}

int64_t yukt_net_read_status(void* conn, int64_t max) {
    yukt_net_str = NULL;
    if (max <= 0) {
        die("invalid read size");
    }
    YuktNet* net = (YuktNet*)conn;
    if (net == NULL || yukt_net_load_closed(net) || yukt_net_load_fd(net) < 0) {
        yukt_net_st = 1;
        return 1;
    }
    const int fd = yukt_net_load_fd(net);
    if (fd < 0 || yukt_net_load_closed(net)) {
        yukt_net_st = 1;
        return 1;
    }
    const size_t nmax = (size_t)max;
    char* buf = (char*)malloc(nmax + 1);
    if (buf == NULL) {
        die("out of memory");
    }
    ssize_t n;
    do {
        n = recv(fd, buf, nmax, 0);
    } while (n < 0 && errno == EINTR);
    if (yukt_net_load_closed(net)) {
        free(buf);
        yukt_net_st = 1;
        return 1;
    }
    if (n < 0) {
        free(buf);
        yukt_net_st = 2;
        return 2;
    }
    if (n == 0) {
        free(buf);
        yukt_net_st = 1;
        return 1;
    }
    buf[n] = '\0';
    yukt_net_str = yukt_str_from_lit(buf);
    free(buf);
    yukt_net_st = 0;
    return 0;
}

char* yukt_net_read_unchecked(void) {
    char* s = yukt_net_str;
    yukt_net_str = NULL;
    if (s == NULL) {
        return yukt_str_from_lit("");
    }
    return s;
}

int64_t yukt_net_write_status(void* conn, const char* data) {
    yukt_net_i64 = 0;
    YuktNet* net = (YuktNet*)conn;
    if (net == NULL || yukt_net_load_closed(net) || yukt_net_load_fd(net) < 0) {
        yukt_net_st = 1;
        return 1;
    }
    if (data == NULL) {
        data = "";
    }
    const int fd = yukt_net_load_fd(net);
    if (fd < 0 || yukt_net_load_closed(net)) {
        yukt_net_st = 1;
        return 1;
    }
    const size_t len = strlen(data);
    size_t off = 0;
    while (off < len) {
        ssize_t n = send(fd, data + off, len - off, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0 || yukt_net_load_closed(net)) {
            yukt_net_st = yukt_net_load_closed(net) ? 1 : 2;
            return yukt_net_st;
        }
        off += (size_t)n;
    }
    yukt_net_i64 = (int64_t)len;
    yukt_net_st = 0;
    return 0;
}

int64_t yukt_net_write_unchecked(void) {
    int64_t n = yukt_net_i64;
    yukt_net_i64 = 0;
    return n;
}
