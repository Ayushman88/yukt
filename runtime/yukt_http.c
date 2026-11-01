#include "yukt_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char* data;
    size_t len;
    size_t cap;
} YuktHttpBuf;

static _Thread_local int64_t yukt_http_code = 0;
static _Thread_local void* yukt_http_headers = NULL;
static _Thread_local char* yukt_http_body = NULL;

static void yukt_http_buf_init(YuktHttpBuf* buf) {
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
}

static void yukt_http_buf_free(YuktHttpBuf* buf) {
    free(buf->data);
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
}

static void yukt_http_buf_grow(YuktHttpBuf* buf, size_t need) {
    if (need <= buf->cap) {
        return;
    }
    size_t cap = buf->cap == 0 ? 256 : buf->cap;
    while (cap < need) {
        if (cap > ((size_t)-1) / 2) {
            die("out of memory");
        }
        cap *= 2;
    }
    char* data = (char*)realloc(buf->data, cap);
    if (data == NULL) {
        die("out of memory");
    }
    buf->data = data;
    buf->cap = cap;
}

static void yukt_http_buf_append(YuktHttpBuf* buf, const char* src, size_t n) {
    yukt_http_buf_grow(buf, buf->len + n + 1);
    if (n > 0 && src != NULL) {
        memcpy(buf->data + buf->len, src, n);
    }
    buf->len += n;
    buf->data[buf->len] = '\0';
}

static void yukt_http_clear_stash(void) {
    yukt_http_code = 0;
    if (yukt_http_headers != NULL) {
        yukt_release(yukt_http_headers);
        yukt_http_headers = NULL;
    }
    if (yukt_http_body != NULL) {
        yukt_release(yukt_http_body);
        yukt_http_body = NULL;
    }
}

static void yukt_http_close_conn(void* conn) {
    if (conn == NULL) {
        return;
    }
    yukt_net_close(conn);
    yukt_release(conn);
}

static int yukt_http_ascii_ieq(const char* a, const char* b, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        char ca = a[i];
        char cb = b[i];
        if (ca >= 'A' && ca <= 'Z') {
            ca = (char)(ca - 'A' + 'a');
        }
        if (cb >= 'A' && cb <= 'Z') {
            cb = (char)(cb - 'A' + 'a');
        }
        if (ca != cb) {
            return 0;
        }
    }
    return 1;
}

static void yukt_http_lower_ascii(char* s) {
    if (s == NULL) {
        return;
    }
    for (; *s != '\0'; ++s) {
        if (*s >= 'A' && *s <= 'Z') {
            *s = (char)(*s - 'A' + 'a');
        }
    }
}

static char* yukt_http_trim(char* s) {
    if (s == NULL) {
        return s;
    }
    while (*s == ' ' || *s == '\t') {
        ++s;
    }
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r')) {
        s[n - 1] = '\0';
        --n;
    }
    return s;
}

static int yukt_http_te_chunked(const char* value) {
    const char* v = value;
    while (v != NULL && *v != '\0') {
        while (*v == ' ' || *v == '\t' || *v == ',') {
            ++v;
        }
        if (*v == '\0') {
            break;
        }
        const char* start = v;
        while (*v != '\0' && *v != ',' && *v != ';' && *v != ' ' && *v != '\t') {
            ++v;
        }
        const size_t n = (size_t)(v - start);
        if (n == 7 && yukt_http_ascii_ieq(start, "chunked", 7)) {
            return 1;
        }
        while (*v != '\0' && *v != ',') {
            ++v;
        }
    }
    return 0;
}

static int yukt_http_parse_nonneg(const char* s, int64_t* out) {
    if (s == NULL || *s == '\0' || out == NULL) {
        return -1;
    }
    int64_t v = 0;
    for (const char* p = s; *p != '\0'; ++p) {
        if (*p < '0' || *p > '9') {
            return -1;
        }
        if (v > (INT64_MAX - (*p - '0')) / 10) {
            return -1;
        }
        v = v * 10 + (*p - '0');
    }
    *out = v;
    return 0;
}

static int yukt_http_parse_port(const char* s, size_t n, int* port) {
    if (s == NULL || n == 0 || port == NULL) {
        return -1;
    }
    int v = 0;
    for (size_t i = 0; i < n; ++i) {
        if (s[i] < '0' || s[i] > '9') {
            return -1;
        }
        if (v > (65535 - (s[i] - '0')) / 10) {
            return -1;
        }
        v = v * 10 + (s[i] - '0');
    }
    if (v <= 0) {
        return -1;
    }
    *port = v;
    return 0;
}

static char* yukt_http_find_header_end(const char* data, size_t len) {
    if (data == NULL || len < 4) {
        return NULL;
    }
    for (size_t i = 0; i + 3 < len; ++i) {
        if (data[i] == '\r' && data[i + 1] == '\n' && data[i + 2] == '\r' && data[i + 3] == '\n') {
            return (char*)(data + i);
        }
    }
    return NULL;
}

static int yukt_http_read_more(void* conn, YuktHttpBuf* buf) {
    const int64_t st = yukt_net_read_status(conn, 4096);
    if (st != 0) {
        return (int)st;
    }
    char* chunk = yukt_net_read_unchecked();
    const size_t n = chunk == NULL ? 0 : strlen(chunk);
    yukt_http_buf_append(buf, chunk, n);
    if (chunk != NULL) {
        yukt_release(chunk);
    }
    return 0;
}

static int yukt_http_parse_url(const char* url, char* host, size_t host_n, int* port, int* ipv6, char* target,
                              size_t target_n, char* addr, size_t addr_n, char* host_hdr, size_t host_hdr_n) {
    if (url == NULL || host == NULL || port == NULL || ipv6 == NULL || target == NULL || addr == NULL ||
        host_hdr == NULL) {
        return -1;
    }
    if (strncmp(url, "https://", 8) == 0) {
        return -1;
    }
    if (strchr(url, '#') != NULL) {
        return -1;
    }
    if (strncmp(url, "http://", 7) != 0) {
        return -1;
    }
    const char* p = url + 7;
    const char* slash = strchr(p, '/');
    const char* qmark = strchr(p, '?');
    const char* auth_end = p + strlen(p);
    if (slash != NULL && slash < auth_end) {
        auth_end = slash;
    }
    if (qmark != NULL && qmark < auth_end) {
        auth_end = qmark;
    }
    const char* at = strchr(p, '@');
    if (at != NULL && at < auth_end) {
        return -1;
    }
    const char* host_s = p;
    size_t host_len = 0;
    *ipv6 = 0;
    *port = 80;
    if (p[0] == '[') {
        const char* rb = memchr(p, ']', (size_t)(auth_end - p));
        if (rb == NULL || rb == p + 1) {
            return -1;
        }
        host_s = p + 1;
        host_len = (size_t)(rb - host_s);
        *ipv6 = 1;
        if (rb + 1 < auth_end) {
            if (rb[1] != ':') {
                return -1;
            }
            if (yukt_http_parse_port(rb + 2, (size_t)(auth_end - (rb + 2)), port) != 0) {
                return -1;
            }
        }
    } else {
        const char* colon = memchr(p, ':', (size_t)(auth_end - p));
        if (colon != NULL) {
            host_len = (size_t)(colon - p);
            if (yukt_http_parse_port(colon + 1, (size_t)(auth_end - (colon + 1)), port) != 0) {
                return -1;
            }
        } else {
            host_len = (size_t)(auth_end - p);
        }
    }
    if (host_len == 0 || host_len >= host_n) {
        return -1;
    }
    memcpy(host, host_s, host_len);
    host[host_len] = '\0';

    if (*auth_end == '\0') {
        if (target_n < 2) {
            return -1;
        }
        target[0] = '/';
        target[1] = '\0';
    } else if (*auth_end == '?') {
        if (snprintf(target, target_n, "/%s", auth_end) < 0 || target[0] == '\0') {
            return -1;
        }
    } else {
        if (strlen(auth_end) >= target_n) {
            return -1;
        }
        memcpy(target, auth_end, strlen(auth_end) + 1);
    }

    if (*ipv6) {
        if (snprintf(addr, addr_n, "[%s]:%d", host, *port) < 0) {
            return -1;
        }
        if (*port == 80) {
            if (snprintf(host_hdr, host_hdr_n, "[%s]", host) < 0) {
                return -1;
            }
        } else if (snprintf(host_hdr, host_hdr_n, "[%s]:%d", host, *port) < 0) {
            return -1;
        }
    } else {
        if (snprintf(addr, addr_n, "%s:%d", host, *port) < 0) {
            return -1;
        }
        if (*port == 80) {
            if (snprintf(host_hdr, host_hdr_n, "%s", host) < 0) {
                return -1;
            }
        } else if (snprintf(host_hdr, host_hdr_n, "%s:%d", host, *port) < 0) {
            return -1;
        }
    }
    return 0;
}

static int yukt_http_parse_status_line(char* line, int64_t* code) {
    if (line == NULL || code == NULL) {
        return -1;
    }
    if (strncmp(line, "HTTP/", 5) != 0) {
        return -1;
    }
    char* sp = strchr(line, ' ');
    if (sp == NULL) {
        return -1;
    }
    while (*sp == ' ') {
        ++sp;
    }
    if (*sp < '0' || *sp > '9') {
        return -1;
    }
    int64_t v = 0;
    while (*sp >= '0' && *sp <= '9') {
        if (v > (INT64_MAX - (*sp - '0')) / 10) {
            return -1;
        }
        v = v * 10 + (*sp - '0');
        ++sp;
    }
    *code = v;
    return 0;
}

static int yukt_http_fill_headers(char* header_text, void* map, int* chunked, int* have_cl, int64_t* cl) {
    char* line = header_text;
    *chunked = 0;
    *have_cl = 0;
    *cl = 0;
    while (line != NULL && *line != '\0') {
        char* nl = strstr(line, "\r\n");
        if (nl == line) {
            break;
        }
        if (nl != NULL) {
            *nl = '\0';
        }
        char* colon = strchr(line, ':');
        if (colon == NULL || colon == line) {
            return -1;
        }
        *colon = '\0';
        char* key = yukt_http_trim(line);
        char* value = yukt_http_trim(colon + 1);
        if (key[0] == '\0') {
            return -1;
        }
        yukt_http_lower_ascii(key);
        if (strcmp(key, "transfer-encoding") == 0 && yukt_http_te_chunked(value)) {
            *chunked = 1;
        }
        if (strcmp(key, "content-length") == 0) {
            int64_t n = 0;
            if (yukt_http_parse_nonneg(value, &n) == 0) {
                *have_cl = 1;
                *cl = n;
            } else {
                *have_cl = 0;
            }
        }
        yukt_map_set_cstr(map, key, value);
        if (nl == NULL) {
            break;
        }
        line = nl + 2;
    }
    return 0;
}

int64_t yukt_http_get_status(const char* url) {
    yukt_http_clear_stash();
    char host[256];
    char target[4096];
    char addr[320];
    char host_hdr[320];
    int port = 80;
    int ipv6 = 0;
    host[0] = '\0';
    target[0] = '\0';
    addr[0] = '\0';
    host_hdr[0] = '\0';
    if (yukt_http_parse_url(url, host, sizeof(host), &port, &ipv6, target, sizeof(target), addr, sizeof(addr),
                            host_hdr, sizeof(host_hdr)) != 0) {
        return 2;
    }

    const int64_t cst = yukt_net_connect_status(addr);
    if (cst != 0) {
        return 2;
    }
    void* conn = yukt_net_connect_unchecked();
    if (conn == NULL) {
        return 2;
    }

    char req[4608];
    if (snprintf(req, sizeof(req), "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", target, host_hdr) <
        0) {
        yukt_http_close_conn(conn);
        return 2;
    }
    if (yukt_net_write_status(conn, req) != 0) {
        (void)yukt_net_write_unchecked();
        yukt_http_close_conn(conn);
        return 2;
    }
    (void)yukt_net_write_unchecked();

    YuktHttpBuf buf;
    yukt_http_buf_init(&buf);
    char* header_end = NULL;
    for (;;) {
        header_end = yukt_http_find_header_end(buf.data, buf.len);
        if (header_end != NULL) {
            break;
        }
        const int st = yukt_http_read_more(conn, &buf);
        if (st != 0) {
            yukt_http_buf_free(&buf);
            yukt_http_close_conn(conn);
            return 2;
        }
    }

    const size_t header_bytes = (size_t)(header_end - buf.data);
    char* headers_copy = (char*)malloc(header_bytes + 1);
    if (headers_copy == NULL) {
        die("out of memory");
    }
    memcpy(headers_copy, buf.data, header_bytes);
    headers_copy[header_bytes] = '\0';

    char* line_end = strstr(headers_copy, "\r\n");
    if (line_end == NULL) {
        free(headers_copy);
        yukt_http_buf_free(&buf);
        yukt_http_close_conn(conn);
        return 2;
    }
    *line_end = '\0';
    int64_t code = 0;
    if (yukt_http_parse_status_line(headers_copy, &code) != 0) {
        free(headers_copy);
        yukt_http_buf_free(&buf);
        yukt_http_close_conn(conn);
        return 2;
    }

    void* map = yukt_map_new();
    int chunked = 0;
    int have_cl = 0;
    int64_t cl = 0;
    if (yukt_http_fill_headers(line_end + 2, map, &chunked, &have_cl, &cl) != 0 || chunked) {
        yukt_release(map);
        free(headers_copy);
        yukt_http_buf_free(&buf);
        yukt_http_close_conn(conn);
        return 2;
    }
    free(headers_copy);

    const size_t body_off = header_bytes + 4;
    YuktHttpBuf body;
    yukt_http_buf_init(&body);
    if (body_off < buf.len) {
        yukt_http_buf_append(&body, buf.data + body_off, buf.len - body_off);
    }
    yukt_http_buf_free(&buf);

    int fail = 0;
    if (have_cl) {
        while ((int64_t)body.len < cl) {
            const int64_t remain = cl - (int64_t)body.len;
            int64_t max = remain;
            if (max > 4096) {
                max = 4096;
            }
            const int64_t st = yukt_net_read_status(conn, max);
            if (st != 0) {
                fail = 1;
                break;
            }
            char* chunk = yukt_net_read_unchecked();
            const size_t n = chunk == NULL ? 0 : strlen(chunk);
            yukt_http_buf_append(&body, chunk, n);
            if (chunk != NULL) {
                yukt_release(chunk);
            }
        }
        if (!fail && (int64_t)body.len > cl) {
            body.len = (size_t)cl;
            if (body.data != NULL) {
                body.data[body.len] = '\0';
            }
        }
    } else {
        for (;;) {
            const int st = yukt_http_read_more(conn, &body);
            if (st == 1) {
                break;
            }
            if (st != 0) {
                fail = 1;
                break;
            }
        }
    }

    yukt_http_close_conn(conn);
    if (fail) {
        yukt_release(map);
        yukt_http_buf_free(&body);
        return 2;
    }

    yukt_http_code = code;
    yukt_http_headers = map;
    yukt_http_body = yukt_str_from_lit(body.data == NULL ? "" : body.data);
    yukt_http_buf_free(&body);
    return 0;
}

int64_t yukt_http_get_code(void) {
    const int64_t code = yukt_http_code;
    yukt_http_code = 0;
    return code;
}

void* yukt_http_get_headers(void) {
    void* map = yukt_http_headers;
    yukt_http_headers = NULL;
    if (map == NULL) {
        return yukt_map_new();
    }
    return map;
}

char* yukt_http_get_body(void) {
    char* body = yukt_http_body;
    yukt_http_body = NULL;
    if (body == NULL) {
        return yukt_str_from_lit("");
    }
    return body;
}

static void yukt_http_write_try(void* conn, const char* data) {
    if (conn == NULL || data == NULL) {
        return;
    }
    if (yukt_net_write_status(conn, data) != 0) {
        (void)yukt_net_write_unchecked();
        return;
    }
    (void)yukt_net_write_unchecked();
}

static void yukt_http_write_400(void* conn) {
    yukt_http_write_try(conn, "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
}

static int yukt_http_is_content_length(const char* key) {
    if (key == NULL) {
        return 0;
    }
    const size_t n = strlen(key);
    return n == 14 && yukt_http_ascii_ieq(key, "content-length", 14);
}

static const char* yukt_http_reason(int64_t status) {
    switch (status) {
        case 200:
            return "OK";
        case 201:
            return "Created";
        case 204:
            return "No Content";
        case 301:
            return "Moved Permanently";
        case 302:
            return "Found";
        case 400:
            return "Bad Request";
        case 404:
            return "Not Found";
        case 405:
            return "Method Not Allowed";
        case 500:
            return "Internal Server Error";
        default:
            return "OK";
    }
}

static int yukt_http_parse_request_line(char* line, char** method, char** path, char** query) {
    if (line == NULL || method == NULL || path == NULL || query == NULL) {
        return -1;
    }
    char* p = line;
    while (*p == ' ') {
        ++p;
    }
    if (*p == '\0') {
        return -1;
    }
    *method = p;
    while (*p != '\0' && *p != ' ') {
        ++p;
    }
    if (*p != ' ') {
        return -1;
    }
    *p++ = '\0';
    while (*p == ' ') {
        ++p;
    }
    char* target = p;
    while (*p != '\0' && *p != ' ') {
        ++p;
    }
    if (*p != ' ') {
        return -1;
    }
    *p++ = '\0';
    while (*p == ' ') {
        ++p;
    }
    if (strncmp(p, "HTTP/1.1", 8) != 0) {
        return -1;
    }
    p += 8;
    if (*p != '\0' && *p != ' ' && *p != '\r' && *p != '\t') {
        return -1;
    }
    if ((*method)[0] == '\0') {
        return -1;
    }
    char* qmark = strchr(target, '?');
    if (qmark != NULL) {
        *qmark = '\0';
        *query = qmark + 1;
    } else {
        *query = "";
    }
    if (target[0] == '\0') {
        *path = "/";
    } else {
        *path = target;
    }
    return 0;
}

typedef struct {
    YuktHttpBuf* buf;
} YuktHttpHdrAcc;

static void yukt_http_acc_header(const char* key, const char* value, void* ctx) {
    YuktHttpHdrAcc* acc = (YuktHttpHdrAcc*)ctx;
    if (acc == NULL || acc->buf == NULL || key == NULL || key[0] == '\0') {
        return;
    }
    if (yukt_http_is_content_length(key)) {
        return;
    }
    yukt_http_buf_append(acc->buf, key, strlen(key));
    yukt_http_buf_append(acc->buf, ": ", 2);
    if (value != NULL) {
        yukt_http_buf_append(acc->buf, value, strlen(value));
    }
    yukt_http_buf_append(acc->buf, "\r\n", 2);
}

static void yukt_http_write_response(void* conn, int64_t status, void* headers, const char* body) {
    if (body == NULL) {
        body = "";
    }
    const size_t body_n = strlen(body);
    YuktHttpBuf out;
    yukt_http_buf_init(&out);
    char line[64];
    const int n = snprintf(line, sizeof(line), "HTTP/1.1 %lld %s\r\n", (long long)status, yukt_http_reason(status));
    if (n > 0) {
        yukt_http_buf_append(&out, line, (size_t)n);
    }
    YuktHttpHdrAcc acc;
    acc.buf = &out;
    yukt_map_each_cstr(headers, yukt_http_acc_header, &acc);
    char cl[80];
    const int cn = snprintf(cl, sizeof(cl), "Content-Length: %zu\r\nConnection: close\r\n\r\n", body_n);
    if (cn > 0) {
        yukt_http_buf_append(&out, cl, (size_t)cn);
    }
    yukt_http_buf_append(&out, body, body_n);
    yukt_http_write_try(conn, out.data == NULL ? "" : out.data);
    yukt_http_buf_free(&out);
}

void yukt_http_serve_on(void* listener, yukt_http_handler_fn handler) {
    if (yukt_net_is_closed(listener)) {
        die("http listener closed");
    }
    for (;;) {
        const int64_t ast = yukt_net_accept_status(listener);
        if (ast == 1) {
            return;
        }
        if (ast != 0) {
            continue;
        }
        void* conn = yukt_net_accept_unchecked();
        if (conn == NULL) {
            continue;
        }

        YuktHttpBuf buf;
        yukt_http_buf_init(&buf);
        char* header_end = NULL;
        int malformed = 0;
        for (;;) {
            header_end = yukt_http_find_header_end(buf.data, buf.len);
            if (header_end != NULL) {
                break;
            }
            const int st = yukt_http_read_more(conn, &buf);
            if (st != 0) {
                malformed = 1;
                break;
            }
        }
        if (header_end == NULL) {
            yukt_http_buf_free(&buf);
            yukt_http_close_conn(conn);
            continue;
        }

        const size_t header_bytes = (size_t)(header_end - buf.data);
        char* headers_copy = (char*)malloc(header_bytes + 1);
        if (headers_copy == NULL) {
            die("out of memory");
        }
        memcpy(headers_copy, buf.data, header_bytes);
        headers_copy[header_bytes] = '\0';

        char* method = NULL;
        char* path = NULL;
        char* query = NULL;
        char* line_end = strstr(headers_copy, "\r\n");
        if (line_end == NULL) {
            malformed = 1;
        } else {
            *line_end = '\0';
            if (yukt_http_parse_request_line(headers_copy, &method, &path, &query) != 0) {
                malformed = 1;
            }
        }

        void* map = yukt_map_new();
        int chunked = 0;
        int have_cl = 0;
        int64_t cl = 0;
        if (!malformed) {
            if (yukt_http_fill_headers(line_end + 2, map, &chunked, &have_cl, &cl) != 0 || chunked) {
                malformed = 1;
            } else if (yukt_map_has(map, "content-length") && !have_cl) {
                malformed = 1;
            }
        }

        const size_t body_off = header_bytes + 4;
        YuktHttpBuf body;
        yukt_http_buf_init(&body);
        if (!malformed && have_cl) {
            if (body_off < buf.len) {
                yukt_http_buf_append(&body, buf.data + body_off, buf.len - body_off);
            }
            while ((int64_t)body.len < cl) {
                const int64_t remain = cl - (int64_t)body.len;
                int64_t max = remain;
                if (max > 4096) {
                    max = 4096;
                }
                const int64_t st = yukt_net_read_status(conn, max);
                if (st != 0) {
                    malformed = 1;
                    break;
                }
                char* chunk = yukt_net_read_unchecked();
                const size_t n = chunk == NULL ? 0 : strlen(chunk);
                yukt_http_buf_append(&body, chunk, n);
                if (chunk != NULL) {
                    yukt_release(chunk);
                }
            }
            if (!malformed && (int64_t)body.len > cl) {
                body.len = (size_t)cl;
                if (body.data != NULL) {
                    body.data[body.len] = '\0';
                }
            }
        }

        yukt_http_buf_free(&buf);
        if (malformed) {
            yukt_http_write_400(conn);
            yukt_release(map);
            yukt_http_buf_free(&body);
            free(headers_copy);
            yukt_http_close_conn(conn);
            continue;
        }

        int64_t status = 200;
        void* hdrs = NULL;
        char* body_out = NULL;
        if (handler != NULL) {
            handler(method, path, query, map, body.data == NULL ? "" : body.data, &status, &hdrs, &body_out);
        } else {
            yukt_release(map);
            map = NULL;
        }
        yukt_http_write_response(conn, status, hdrs, body_out);
        if (hdrs != NULL) {
            yukt_release(hdrs);
        }
        if (body_out != NULL) {
            yukt_release(body_out);
        }
        yukt_http_buf_free(&body);
        free(headers_copy);
        yukt_http_close_conn(conn);
    }
}

void yukt_http_serve(const char* addr, yukt_http_handler_fn handler) {
    if (yukt_net_listen_status(addr) != 0) {
        die("http bind failed");
    }
    yukt_http_serve_on(yukt_net_listen_unchecked(), handler);
}
