#include "yukt_runtime.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

enum { YUKT_OBJ_MAP = 1, YUKT_OBJ_SET = 2, YUKT_OBJ_JSON = 3 };

enum { YUKT_JSON_INVALID = 0, YUKT_JSON_NULL = 1, YUKT_JSON_BOOL = 2, YUKT_JSON_INT = 3,
       YUKT_JSON_FLOAT = 4, YUKT_JSON_STRING = 5, YUKT_JSON_ARRAY = 6, YUKT_JSON_OBJECT = 7 };

typedef struct {
    int tag;
    int64_t cap;
    int64_t len;
    int64_t val_kind; /* -1 unknown, 0 i64, 1 f64, 2 bool, 3 str */
    uint8_t* state;
    char** keys;
    int64_t* i_vals;
    double* f_vals;
    int8_t* b_vals;
    char** s_vals;
} YuktMap;

typedef struct YuktJson {
    int tag;
    int kind;
    int8_t b;
    int64_t i;
    double f;
    char* s;
    int64_t len;
    struct YuktJson** items;
    char** keys;
} YuktJson;

static uint64_t yukt_hash(const char* s) {
    uint64_t h = 1469598103934665603ull;
    if (s == NULL) {
        return h;
    }
    for (const unsigned char* p = (const unsigned char*)s; *p != 0; ++p) {
        h ^= *p;
        h *= 1099511628211ull;
    }
    return h;
}

static char* yukt_cstrdup(const char* s) {
    if (s == NULL) {
        s = "";
    }
    size_t n = strlen(s);
    char* out = (char*)malloc(n + 1);
    if (out == NULL) {
        fprintf(stderr, "YUKT runtime error: out of memory\n");
        exit(1);
    }
    memcpy(out, s, n + 1);
    return out;
}

static YuktMap* yukt_map_alloc(int64_t cap) {
    YuktMap* map = (YuktMap*)calloc(1, sizeof(YuktMap));
    if (map == NULL) {
        fprintf(stderr, "YUKT runtime error: out of memory\n");
        exit(1);
    }
    map->tag = YUKT_OBJ_MAP;
    map->cap = cap < 8 ? 8 : cap;
    map->val_kind = -1;
    map->state = (uint8_t*)calloc((size_t)map->cap, 1);
    map->keys = (char**)calloc((size_t)map->cap, sizeof(char*));
    map->i_vals = (int64_t*)calloc((size_t)map->cap, sizeof(int64_t));
    map->f_vals = (double*)calloc((size_t)map->cap, sizeof(double));
    map->b_vals = (int8_t*)calloc((size_t)map->cap, sizeof(int8_t));
    map->s_vals = (char**)calloc((size_t)map->cap, sizeof(char*));
    if (map->state == NULL || map->keys == NULL) {
        fprintf(stderr, "YUKT runtime error: out of memory\n");
        exit(1);
    }
    return map;
}

static int64_t yukt_map_find(const YuktMap* map, const char* key, int64_t* tomb) {
    if (map == NULL || map->cap <= 0) {
        return -1;
    }
    uint64_t h = yukt_hash(key);
    int64_t mask = map->cap - 1;
    if (tomb != NULL) {
        *tomb = -1;
    }
    for (int64_t n = 0; n < map->cap; ++n) {
        int64_t i = (int64_t)((h + (uint64_t)n) & (uint64_t)mask);
        if (map->state[i] == 0) {
            return tomb != NULL && *tomb >= 0 ? *tomb : i;
        }
        if (map->state[i] == 2) {
            if (tomb != NULL && *tomb < 0) {
                *tomb = i;
            }
            continue;
        }
        if (map->keys[i] != NULL && key != NULL && strcmp(map->keys[i], key) == 0) {
            return i;
        }
    }
    return tomb != NULL && *tomb >= 0 ? *tomb : -1;
}

static void yukt_map_grow(YuktMap* map);

static void yukt_map_insert_slot(YuktMap* map, const char* key, int64_t kind, int64_t i, double f, int8_t b,
                                 char* s, int grow) {
    if (grow && map->len * 10 >= map->cap * 7) {
        yukt_map_grow(map);
    }
    int64_t tomb = -1;
    int64_t slot = yukt_map_find(map, key, &tomb);
    if (slot < 0) {
        yukt_map_grow(map);
        slot = yukt_map_find(map, key, &tomb);
    }
    if (map->state[slot] == 1) {
        if (map->val_kind == 3 && map->s_vals[slot] != NULL) {
            char* old = map->s_vals[slot];
            map->s_vals[slot] = NULL;
            yukt_release(old);
        }
        free(map->keys[slot]);
    } else {
        map->len += 1;
    }
    map->state[slot] = 1;
    map->keys[slot] = yukt_cstrdup(key);
    map->val_kind = kind;
    map->i_vals[slot] = i;
    map->f_vals[slot] = f;
    map->b_vals[slot] = b;
    map->s_vals[slot] = s;
}

static void yukt_map_grow(YuktMap* map) {
    int64_t old_cap = map->cap;
    uint8_t* state = map->state;
    char** keys = map->keys;
    int64_t* i_vals = map->i_vals;
    double* f_vals = map->f_vals;
    int8_t* b_vals = map->b_vals;
    char** s_vals = map->s_vals;
    int64_t old_len = map->len;
    int64_t kind = map->val_kind;
    map->state = NULL;
    map->keys = NULL;
    YuktMap* grown = yukt_map_alloc(old_cap * 2);
    map->cap = grown->cap;
    map->state = grown->state;
    map->keys = grown->keys;
    map->i_vals = grown->i_vals;
    map->f_vals = grown->f_vals;
    map->b_vals = grown->b_vals;
    map->s_vals = grown->s_vals;
    map->len = 0;
    map->val_kind = kind;
    free(grown);
    for (int64_t i = 0; i < old_cap; ++i) {
        if (state[i] != 1) {
            continue;
        }
        yukt_map_insert_slot(map, keys[i], kind, i_vals[i], f_vals[i], b_vals[i], s_vals[i], 0);
        free(keys[i]);
    }
    (void)old_len;
    free(state);
    free(keys);
    free(i_vals);
    free(f_vals);
    free(b_vals);
    free(s_vals);
}

void* yukt_map_new(void) {
    YuktMap* map = yukt_map_alloc(8);
    yukt_heap_lock();
    yukt_track_obj(map, sizeof(YuktMap));
    yukt_heap_unlock();
    return map;
}

static YuktMap* as_map(void* ptr) {
    YuktMap* map = (YuktMap*)ptr;
    if (map == NULL || map->tag != YUKT_OBJ_MAP) {
        fprintf(stderr, "YUKT runtime error: expected a map\n");
        exit(1);
    }
    return map;
}

void yukt_map_set_i64(void* map, const char* key, int64_t value) {
    yukt_map_insert_slot(as_map(map), key, 0, value, 0.0, 0, NULL, 1);
}

void yukt_map_set_f64(void* map, const char* key, double value) {
    yukt_map_insert_slot(as_map(map), key, 1, 0, value, 0, NULL, 1);
}

void yukt_map_set_bool(void* map, const char* key, int8_t value) {
    yukt_map_insert_slot(as_map(map), key, 2, 0, 0.0, value, NULL, 1);
}

void yukt_map_set_cstr(void* map, const char* key, const char* value) {
    char* copy = yukt_str_clone(value);
    yukt_map_insert_slot(as_map(map), key, 3, 0, 0.0, 0, copy, 1);
}

int8_t yukt_map_has(void* map, const char* key) {
    YuktMap* m = as_map(map);
    int64_t tomb = -1;
    int64_t slot = yukt_map_find(m, key, &tomb);
    return slot >= 0 && m->state[slot] == 1 ? 1 : 0;
}

int64_t yukt_map_get_i64(void* map, const char* key) {
    YuktMap* m = as_map(map);
    int64_t tomb = -1;
    int64_t slot = yukt_map_find(m, key, &tomb);
    if (slot < 0 || m->state[slot] != 1) {
        return 0;
    }
    return m->i_vals[slot];
}

double yukt_map_get_f64(void* map, const char* key) {
    YuktMap* m = as_map(map);
    int64_t tomb = -1;
    int64_t slot = yukt_map_find(m, key, &tomb);
    if (slot < 0 || m->state[slot] != 1) {
        return 0.0;
    }
    return m->f_vals[slot];
}

int8_t yukt_map_get_bool(void* map, const char* key) {
    YuktMap* m = as_map(map);
    int64_t tomb = -1;
    int64_t slot = yukt_map_find(m, key, &tomb);
    if (slot < 0 || m->state[slot] != 1) {
        return 0;
    }
    return m->b_vals[slot];
}

char* yukt_map_get_cstr(void* map, const char* key) {
    YuktMap* m = as_map(map);
    int64_t tomb = -1;
    int64_t slot = yukt_map_find(m, key, &tomb);
    if (slot < 0 || m->state[slot] != 1) {
        return yukt_str_from_lit("");
    }
    return yukt_str_clone(m->s_vals[slot] != NULL ? m->s_vals[slot] : "");
}

int64_t yukt_map_len(void* map) {
    return as_map(map)->len;
}

void yukt_map_remove(void* map, const char* key) {
    YuktMap* m = as_map(map);
    int64_t tomb = -1;
    int64_t slot = yukt_map_find(m, key, &tomb);
    if (slot < 0 || m->state[slot] != 1) {
        return;
    }
    if (m->val_kind == 3 && m->s_vals[slot] != NULL) {
        char* old = m->s_vals[slot];
        m->s_vals[slot] = NULL;
        yukt_release(old);
    }
    free(m->keys[slot]);
    m->keys[slot] = NULL;
    m->state[slot] = 2;
    m->len -= 1;
}

void yukt_map_each_cstr(void* map, void (*fn)(const char* key, const char* value, void* ctx), void* ctx) {
    if (map == NULL || fn == NULL) {
        return;
    }
    YuktMap* m = as_map(map);
    for (int64_t i = 0; i < m->cap; ++i) {
        if (m->state[i] != 1 || m->keys[i] == NULL) {
            continue;
        }
        if (m->val_kind != 3) {
            continue;
        }
        fn(m->keys[i], m->s_vals[i] != NULL ? m->s_vals[i] : "", ctx);
    }
}

void* yukt_set_new(void) {
    return yukt_map_new();
}

static YuktMap* as_set(void* ptr) {
    return as_map(ptr);
}

void yukt_set_add(void* set, const char* key) {
    yukt_map_set_i64(as_set(set), key, 1);
}

int8_t yukt_set_has(void* set, const char* key) {
    return yukt_map_has(as_set(set), key);
}

int64_t yukt_set_len(void* set) {
    return yukt_map_len(as_set(set));
}

void yukt_set_remove(void* set, const char* key) {
    yukt_map_remove(as_set(set), key);
}

int8_t yukt_file_exists(const char* path) {
    if (path == NULL) {
        return 0;
    }
    struct stat st;
    return stat(path, &st) == 0 ? 1 : 0;
}

char* yukt_file_read(const char* path) {
    if (path == NULL) {
        return yukt_str_from_lit("");
    }
    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        return yukt_str_from_lit("");
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return yukt_str_from_lit("");
    }
    long size = ftell(file);
    if (size < 0) {
        fclose(file);
        return yukt_str_from_lit("");
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return yukt_str_from_lit("");
    }
    char* raw = (char*)malloc((size_t)size + 1);
    if (raw == NULL) {
        fclose(file);
        fprintf(stderr, "YUKT runtime error: out of memory\n");
        exit(1);
    }
    size_t n = fread(raw, 1, (size_t)size, file);
    raw[n] = 0;
    fclose(file);
    char* owned = yukt_str_from_lit(raw);
    free(raw);
    return owned;
}

int64_t yukt_file_write(const char* path, const char* data) {
    if (path == NULL) {
        return -1;
    }
    FILE* file = fopen(path, "wb");
    if (file == NULL) {
        return -1;
    }
    if (data == NULL) {
        data = "";
    }
    size_t n = strlen(data);
    size_t wrote = fwrite(data, 1, n, file);
    int ok = fclose(file) == 0;
    if (!ok || wrote != n) {
        return -1;
    }
    return (int64_t)wrote;
}

typedef struct {
    const char* s;
    size_t i;
    int failed;
} JsonParser;

static void json_skip(JsonParser* p) {
    while (p->s[p->i] == ' ' || p->s[p->i] == '\n' || p->s[p->i] == '\r' || p->s[p->i] == '\t') {
        p->i += 1;
    }
}

static YuktJson* json_value(JsonParser* p);

static YuktJson* json_new(int kind) {
    YuktJson* json = (YuktJson*)calloc(1, sizeof(YuktJson));
    if (json == NULL) {
        fprintf(stderr, "YUKT runtime error: out of memory\n");
        exit(1);
    }
    json->tag = YUKT_OBJ_JSON;
    json->kind = kind;
    return json;
}

static YuktJson* json_fail(JsonParser* p) {
    p->failed = 1;
    return json_new(YUKT_JSON_INVALID);
}

static int json_eat(JsonParser* p, char c) {
    json_skip(p);
    if (p->s[p->i] != c) {
        return 0;
    }
    p->i += 1;
    return 1;
}

static YuktJson* json_string(JsonParser* p) {
    if (!json_eat(p, '"')) {
        return json_fail(p);
    }
    size_t start = p->i;
    while (p->s[p->i] != 0 && p->s[p->i] != '"') {
        if (p->s[p->i] == '\\') {
            p->i += 1;
            if (p->s[p->i] == 0) {
                return json_fail(p);
            }
        }
        p->i += 1;
    }
    if (p->s[p->i] != '"') {
        return json_fail(p);
    }
    size_t n = p->i - start;
    char* buf = (char*)malloc(n + 1);
    if (buf == NULL) {
        fprintf(stderr, "YUKT runtime error: out of memory\n");
        exit(1);
    }
    size_t o = 0;
    for (size_t i = start; i < p->i; ++i) {
        if (p->s[i] == '\\' && i + 1 < p->i) {
            ++i;
            char e = p->s[i];
            if (e == 'n') {
                buf[o++] = '\n';
            } else if (e == 't') {
                buf[o++] = '\t';
            } else {
                buf[o++] = e;
            }
        } else {
            buf[o++] = p->s[i];
        }
    }
    buf[o] = 0;
    p->i += 1;
    YuktJson* json = json_new(YUKT_JSON_STRING);
    json->s = buf;
    return json;
}

static YuktJson* json_number(JsonParser* p) {
    json_skip(p);
    size_t start = p->i;
    if (p->s[p->i] == '-') {
        p->i += 1;
    }
    if (p->s[p->i] < '0' || p->s[p->i] > '9') {
        return json_fail(p);
    }
    int is_float = 0;
    while (p->s[p->i] >= '0' && p->s[p->i] <= '9') {
        p->i += 1;
    }
    if (p->s[p->i] == '.') {
        is_float = 1;
        p->i += 1;
        while (p->s[p->i] >= '0' && p->s[p->i] <= '9') {
            p->i += 1;
        }
    }
    char tmp[64];
    size_t n = p->i - start;
    if (n >= sizeof(tmp)) {
        return json_fail(p);
    }
    memcpy(tmp, p->s + start, n);
    tmp[n] = 0;
    YuktJson* json = json_new(is_float ? YUKT_JSON_FLOAT : YUKT_JSON_INT);
    if (is_float) {
        json->f = strtod(tmp, NULL);
    } else {
        json->i = strtoll(tmp, NULL, 10);
    }
    return json;
}

static YuktJson* json_array(JsonParser* p) {
    if (!json_eat(p, '[')) {
        return json_fail(p);
    }
    YuktJson* json = json_new(YUKT_JSON_ARRAY);
    json_skip(p);
    if (json_eat(p, ']')) {
        return json;
    }
    int64_t cap = 4;
    json->items = (YuktJson**)malloc((size_t)cap * sizeof(YuktJson*));
    while (1) {
        if (json->len >= cap) {
            cap *= 2;
            json->items = (YuktJson**)realloc(json->items, (size_t)cap * sizeof(YuktJson*));
        }
        json->items[json->len++] = json_value(p);
        if (p->failed) {
            return json;
        }
        json_skip(p);
        if (json_eat(p, ']')) {
            return json;
        }
        if (!json_eat(p, ',')) {
            return json_fail(p);
        }
    }
}

static YuktJson* json_object(JsonParser* p) {
    if (!json_eat(p, '{')) {
        return json_fail(p);
    }
    YuktJson* json = json_new(YUKT_JSON_OBJECT);
    json_skip(p);
    if (json_eat(p, '}')) {
        return json;
    }
    int64_t cap = 4;
    json->keys = (char**)malloc((size_t)cap * sizeof(char*));
    json->items = (YuktJson**)malloc((size_t)cap * sizeof(YuktJson*));
    while (1) {
        YuktJson* key = json_string(p);
        if (p->failed) {
            return json;
        }
        if (!json_eat(p, ':')) {
            return json_fail(p);
        }
        if (json->len >= cap) {
            cap *= 2;
            json->keys = (char**)realloc(json->keys, (size_t)cap * sizeof(char*));
            json->items = (YuktJson**)realloc(json->items, (size_t)cap * sizeof(YuktJson*));
        }
        json->keys[json->len] = key->s;
        key->s = NULL;
        free(key);
        json->items[json->len] = json_value(p);
        json->len += 1;
        if (p->failed) {
            return json;
        }
        json_skip(p);
        if (json_eat(p, '}')) {
            return json;
        }
        if (!json_eat(p, ',')) {
            return json_fail(p);
        }
    }
}

static YuktJson* json_value(JsonParser* p) {
    json_skip(p);
    char c = p->s[p->i];
    if (c == '"') {
        return json_string(p);
    }
    if (c == '{') {
        return json_object(p);
    }
    if (c == '[') {
        return json_array(p);
    }
    if (c == 't' && strncmp(p->s + p->i, "true", 4) == 0) {
        p->i += 4;
        YuktJson* json = json_new(YUKT_JSON_BOOL);
        json->b = 1;
        return json;
    }
    if (c == 'f' && strncmp(p->s + p->i, "false", 5) == 0) {
        p->i += 5;
        YuktJson* json = json_new(YUKT_JSON_BOOL);
        json->b = 0;
        return json;
    }
    if (c == 'n' && strncmp(p->s + p->i, "null", 4) == 0) {
        p->i += 4;
        return json_new(YUKT_JSON_NULL);
    }
    if (c == '-' || (c >= '0' && c <= '9')) {
        return json_number(p);
    }
    return json_fail(p);
}

static void json_free_tree(YuktJson* json) {
    if (json == NULL) {
        return;
    }
    free(json->s);
    if (json->keys != NULL) {
        for (int64_t i = 0; i < json->len; ++i) {
            free(json->keys[i]);
        }
        free(json->keys);
    }
    if (json->items != NULL) {
        for (int64_t i = 0; i < json->len; ++i) {
            json_free_tree(json->items[i]);
        }
        free(json->items);
    }
    free(json);
}

void* yukt_json_parse(const char* text) {
    JsonParser p;
    p.s = text != NULL ? text : "";
    p.i = 0;
    p.failed = 0;
    YuktJson* json = json_value(&p);
    json_skip(&p);
    if (p.failed || p.s[p.i] != 0) {
        json_free_tree(json);
        json = json_new(YUKT_JSON_INVALID);
    }
    yukt_heap_lock();
    yukt_track_obj(json, sizeof(YuktJson));
    yukt_heap_unlock();
    return json;
}

int8_t yukt_json_is_valid(void* json) {
    YuktJson* j = (YuktJson*)json;
    return j != NULL && j->tag == YUKT_OBJ_JSON && j->kind != YUKT_JSON_INVALID ? 1 : 0;
}

static YuktJson* json_field(YuktJson* json, const char* key) {
    if (json == NULL || json->tag != YUKT_OBJ_JSON || json->kind != YUKT_JSON_OBJECT || key == NULL) {
        return NULL;
    }
    for (int64_t i = 0; i < json->len; ++i) {
        if (json->keys[i] != NULL && strcmp(json->keys[i], key) == 0) {
            return json->items[i];
        }
    }
    return NULL;
}

int8_t yukt_json_has_int(void* json, const char* key) {
    YuktJson* field = json_field((YuktJson*)json, key);
    return field != NULL && field->kind == YUKT_JSON_INT ? 1 : 0;
}

int8_t yukt_json_has_string(void* json, const char* key) {
    YuktJson* field = json_field((YuktJson*)json, key);
    return field != NULL && field->kind == YUKT_JSON_STRING ? 1 : 0;
}

int64_t yukt_json_get_int(void* json, const char* key) {
    YuktJson* field = json_field((YuktJson*)json, key);
    return field != NULL ? field->i : 0;
}

char* yukt_json_get_string(void* json, const char* key) {
    YuktJson* field = json_field((YuktJson*)json, key);
    if (field == NULL || field->s == NULL) {
        return yukt_str_from_lit("");
    }
    return yukt_str_from_lit(field->s);
}

static void json_encode_into(YuktJson* json, char** buf, size_t* n, size_t* cap);

static void json_append(char** buf, size_t* n, size_t* cap, const char* s) {
    size_t add = strlen(s);
    if (*n + add + 1 > *cap) {
        *cap = (*cap + add + 1) * 2;
        *buf = (char*)realloc(*buf, *cap);
        if (*buf == NULL) {
            fprintf(stderr, "YUKT runtime error: out of memory\n");
            exit(1);
        }
    }
    memcpy(*buf + *n, s, add + 1);
    *n += add;
}

static void json_encode_into(YuktJson* json, char** buf, size_t* n, size_t* cap) {
    if (json == NULL) {
        json_append(buf, n, cap, "null");
        return;
    }
    char tmp[64];
    switch (json->kind) {
        case YUKT_JSON_NULL:
            json_append(buf, n, cap, "null");
            break;
        case YUKT_JSON_BOOL:
            json_append(buf, n, cap, json->b ? "true" : "false");
            break;
        case YUKT_JSON_INT:
            snprintf(tmp, sizeof(tmp), "%" PRId64, json->i);
            json_append(buf, n, cap, tmp);
            break;
        case YUKT_JSON_FLOAT:
            snprintf(tmp, sizeof(tmp), "%g", json->f);
            json_append(buf, n, cap, tmp);
            break;
        case YUKT_JSON_STRING:
            json_append(buf, n, cap, "\"");
            json_append(buf, n, cap, json->s != NULL ? json->s : "");
            json_append(buf, n, cap, "\"");
            break;
        case YUKT_JSON_ARRAY:
            json_append(buf, n, cap, "[");
            for (int64_t i = 0; i < json->len; ++i) {
                if (i != 0) {
                    json_append(buf, n, cap, ",");
                }
                json_encode_into(json->items[i], buf, n, cap);
            }
            json_append(buf, n, cap, "]");
            break;
        case YUKT_JSON_OBJECT:
            json_append(buf, n, cap, "{");
            for (int64_t i = 0; i < json->len; ++i) {
                if (i != 0) {
                    json_append(buf, n, cap, ",");
                }
                json_append(buf, n, cap, "\"");
                json_append(buf, n, cap, json->keys[i] != NULL ? json->keys[i] : "");
                json_append(buf, n, cap, "\":");
                json_encode_into(json->items[i], buf, n, cap);
            }
            json_append(buf, n, cap, "}");
            break;
        default:
            json_append(buf, n, cap, "null");
            break;
    }
}

char* yukt_json_encode(void* json) {
    char* buf = (char*)malloc(16);
    size_t n = 0;
    size_t cap = 16;
    buf[0] = 0;
    json_encode_into((YuktJson*)json, &buf, &n, &cap);
    char* owned = yukt_str_from_lit(buf);
    free(buf);
    return owned;
}

static void yukt_map_free(YuktMap* map) {
    if (map == NULL) {
        return;
    }
    for (int64_t i = 0; i < map->cap; ++i) {
        if (map->state != NULL && map->state[i] == 1) {
            if (map->val_kind == 3 && map->s_vals != NULL && map->s_vals[i] != NULL) {
                char* old = map->s_vals[i];
                map->s_vals[i] = NULL;
                yukt_release(old);
            }
            free(map->keys[i]);
        }
    }
    free(map->state);
    free(map->keys);
    free(map->i_vals);
    free(map->f_vals);
    free(map->b_vals);
    free(map->s_vals);
    free(map);
}

void yukt_013_free(void* ptr) {
    if (ptr == NULL) {
        return;
    }
    yukt_heap_lock();
    int tag = *(int*)ptr;
    if (tag == YUKT_OBJ_MAP || tag == YUKT_OBJ_SET) {
        yukt_map_free((YuktMap*)ptr);
        yukt_heap_unlock();
        return;
    }
    if (tag == YUKT_OBJ_JSON) {
        json_free_tree((YuktJson*)ptr);
    }
    yukt_heap_unlock();
}
