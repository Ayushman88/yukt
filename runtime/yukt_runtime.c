#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "yukt_runtime.h"

#include <inttypes.h>
#include <locale.h>
#include <math.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__APPLE__)
#include <xlocale.h>
#endif

#include <pthread.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <dispatch/dispatch.h>
#endif

static void (*yukt_die_hook)(const char* message) = NULL;
static int yukt_fail_next_payload_alloc = 0;

static void die(const char* message) {
    fprintf(stderr, "YUKT runtime error: %s\n", message);
    if (yukt_die_hook != NULL) {
        void (*hook)(const char*) = yukt_die_hook;
        yukt_die_hook = NULL;
        hook(message);
    }
    exit(1);
}

void yukt_debug_fail_next_payload_alloc(void) {
    yukt_fail_next_payload_alloc = 1;
}

void yukt_debug_set_die_hook(void (*fn)(const char* message)) {
    yukt_die_hook = fn;
}

enum { YUKT_ALLOC_LIST = 1, YUKT_ALLOC_STR = 2, YUKT_ALLOC_OBJ = 3, YUKT_ALLOC_TASK = 4 };

typedef struct YuktAlloc {
    struct YuktAlloc* next;
    int kind;
    void* ptr;
    size_t bytes;
} YuktAlloc;

static YuktAlloc* yukt_allocs = NULL;

enum { YUKT_BIN_COUNT = 6, YUKT_BIN_SLOTS = 4 };

/* Size classes: 16, 64, 256, 4096, 65536, 1048576.
 * Four slots per class. List and string caches are separate.
 * Max retained payloads: 4 * 1118544 * 2 = 8948352 bytes. docs/RUNTIME.md */
static const size_t yukt_bin_limits[YUKT_BIN_COUNT] = {16, 64, 256, 4096, 65536, 1048576};

typedef struct {
    void* slots[YUKT_BIN_SLOTS];
    int count;
} YuktCacheBin;

static YuktCacheBin yukt_list_bins[YUKT_BIN_COUNT];
static YuktCacheBin yukt_str_bins[YUKT_BIN_COUNT];
static pthread_mutex_t yukt_reduce_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t yukt_heap_mu;
static pthread_once_t yukt_heap_once = PTHREAD_ONCE_INIT;
/* Tracker, bins, and 0.13 object alloc/free take yukt_heap_mu. Do not hold it
 * across a user task body or a blocking wait. */

static void yukt_heap_init(void) {
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&yukt_heap_mu, &attr);
    pthread_mutexattr_destroy(&attr);
}

void yukt_heap_lock(void) {
    pthread_once(&yukt_heap_once, yukt_heap_init);
    pthread_mutex_lock(&yukt_heap_mu);
}

void yukt_heap_unlock(void) {
    pthread_mutex_unlock(&yukt_heap_mu);
}

static int yukt_bin_index(size_t bytes) {
    for (int i = 0; i < YUKT_BIN_COUNT; ++i) {
        if (bytes <= yukt_bin_limits[i]) {
            return i;
        }
    }
    return -1;
}

static size_t yukt_alloc_size(size_t bytes) {
    const int idx = yukt_bin_index(bytes);
    if (idx < 0) {
        return bytes;
    }
    return yukt_bin_limits[idx];
}

static void* yukt_cache_take(YuktCacheBin* bins, size_t bytes) {
    yukt_heap_lock();
    const int idx = yukt_bin_index(bytes);
    void* ptr = NULL;
    if (idx >= 0 && bins[idx].count > 0) {
        ptr = bins[idx].slots[--bins[idx].count];
    }
    yukt_heap_unlock();
    return ptr;
}

static int yukt_cache_put(YuktCacheBin* bins, void* ptr, size_t bytes) {
    yukt_heap_lock();
    const int idx = yukt_bin_index(bytes);
    int ok = 0;
    if (ptr != NULL && idx >= 0 && bins[idx].count < YUKT_BIN_SLOTS) {
        bins[idx].slots[bins[idx].count++] = ptr;
        ok = 1;
    }
    yukt_heap_unlock();
    return ok;
}

static size_t yukt_pad16(size_t bytes) {
    const size_t align = 16;
    if (bytes == 0) {
        return 0;
    }
    return (bytes + align - 1) & ~(align - 1);
}

static int yukt_size_mul(size_t a, size_t b, size_t* out);

void yukt_reduce_i64(int64_t* acc, int64_t partial, int64_t op) {
    if (acc == NULL) {
        return;
    }
    pthread_mutex_lock(&yukt_reduce_mu);
    if (op == 1) {
        *acc *= partial;
    } else {
        *acc += partial;
    }
    pthread_mutex_unlock(&yukt_reduce_mu);
}

void yukt_reduce_f64(double* acc, double partial, int64_t op) {
    if (acc == NULL) {
        return;
    }
    pthread_mutex_lock(&yukt_reduce_mu);
    if (op == 1) {
        *acc *= partial;
    } else {
        *acc += partial;
    }
    pthread_mutex_unlock(&yukt_reduce_mu);
}

void yukt_013_free(void* ptr);

static void yukt_track(int kind, void* ptr, size_t bytes) {
    if (ptr == NULL) {
        return;
    }
    YuktAlloc* node = (YuktAlloc*)malloc(sizeof(YuktAlloc));
    if (node == NULL) {
        die("out of memory");
    }
    node->kind = kind;
    node->ptr = ptr;
    node->bytes = bytes;
    yukt_heap_lock();
    node->next = yukt_allocs;
    yukt_allocs = node;
    yukt_heap_unlock();
}

void yukt_track_obj(void* ptr, size_t bytes) {
    yukt_track(YUKT_ALLOC_OBJ, ptr, bytes);
}

static void yukt_task_destroy_sync(void* ptr);

void yukt_runtime_release(void) {
    yukt_task_shutdown();
    yukt_heap_lock();
    YuktAlloc* node = yukt_allocs;
    yukt_allocs = NULL;
    yukt_heap_unlock();
    while (node != NULL) {
        YuktAlloc* next = node->next;
        if (node->kind == YUKT_ALLOC_LIST) {
            YuktList* list = (YuktList*)node->ptr;
            if (list->data != NULL && yukt_cache_put(yukt_list_bins, list->data, node->bytes)) {
                list->data = NULL;
            }
            free(list->data);
            free(list);
        } else if (node->kind == YUKT_ALLOC_OBJ) {
            yukt_013_free(node->ptr);
        } else if (node->kind == YUKT_ALLOC_TASK) {
            yukt_task_destroy_sync(node->ptr);
            free(node->ptr);
        } else if (!yukt_cache_put(yukt_str_bins, node->ptr, node->bytes)) {
            free(node->ptr);
        }
        free(node);
        node = next;
    }
}

int64_t yukt_debug_live_allocs(void) {
    yukt_heap_lock();
    int64_t n = 0;
    for (YuktAlloc* p = yukt_allocs; p != NULL; p = p->next) {
        n++;
    }
    yukt_heap_unlock();
    return n;
}

static YuktAlloc* yukt_untrack(void* ptr) {
    yukt_heap_lock();
    YuktAlloc** cursor = &yukt_allocs;
    YuktAlloc* node = NULL;
    while (*cursor != NULL) {
        if ((*cursor)->ptr == ptr) {
            node = *cursor;
            *cursor = node->next;
            break;
        }
        cursor = &(*cursor)->next;
    }
    yukt_heap_unlock();
    return node;
}

void yukt_release(void* ptr) {
    if (ptr == NULL) {
        return;
    }
    YuktAlloc* node = yukt_untrack(ptr);
    if (node == NULL) {
        die("double release or untracked pointer");
    }
    if (node->kind == YUKT_ALLOC_LIST) {
        YuktList* list = (YuktList*)node->ptr;
        /* Kind 3/4 payloads are pointers; walk them here. Kind 5 record bytes
         * have field offsets only the compiler knows, so it drops owned
         * fields before calling yukt_release on the list itself. */
        if (list->data != NULL && list->kind == 3) {
            const char** slots = (const char**)list->data;
            for (int64_t i = 0; i < list->length; ++i) {
                if (slots[i] != NULL) {
                    void* slot = (void*)slots[i];
                    slots[i] = NULL;
                    yukt_release(slot);
                }
            }
        } else if (list->data != NULL && list->kind == 4) {
            YuktList** slots = (YuktList**)list->data;
            for (int64_t i = 0; i < list->length; ++i) {
                if (slots[i] != NULL) {
                    YuktList* slot = slots[i];
                    slots[i] = NULL;
                    yukt_release(slot);
                }
            }
        }
        if (list->data != NULL && yukt_cache_put(yukt_list_bins, list->data, node->bytes)) {
            list->data = NULL;
        }
        free(list->data);
        free(list);
    } else if (node->kind == YUKT_ALLOC_OBJ) {
        yukt_013_free(node->ptr);
    } else if (node->kind == YUKT_ALLOC_TASK) {
        yukt_task_destroy_sync(node->ptr);
        free(node->ptr);
    } else if (!yukt_cache_put(yukt_str_bins, node->ptr, node->bytes)) {
        free(node->ptr);
    }
    free(node);
}

static void print_list(const YuktList* list);

static void print_value(int64_t kind, const void* data, int64_t index) {
    switch (kind) {
        case 0:
            printf("%" PRId64, ((const int64_t*)data)[index]);
            break;
        case 1:
            printf("%g", ((const double*)data)[index]);
            break;
        case 2:
            printf(((const int8_t*)data)[index] ? "true" : "false");
            break;
        case 3:
            printf("%s", ((const char* const*)data)[index] != NULL ? ((const char* const*)data)[index] : "");
            break;
        case 4:
            print_list(((YuktList* const*)data)[index]);
            break;
        case 5:
            printf("?");
            break;
        default:
            printf("?");
            break;
    }
}

static void print_list(const YuktList* list) {
    if (list == NULL) {
        printf("[]");
        return;
    }
    printf("[");
    for (int64_t i = 0; i < list->length; ++i) {
        if (i != 0) {
            printf(", ");
        }
        print_value(list->kind, list->data, i);
    }
    printf("]");
}

void yukt_print_i64(int64_t value) {
    printf("%" PRId64, value);
}

void yukt_print_f64(double value) {
    printf("%g", value);
}

void yukt_print_bool(int8_t value) {
    printf("%s", value ? "true" : "false");
}

void yukt_print_cstr(const char* value) {
    printf("%s", value != NULL ? value : "");
}

void yukt_say_i64(int64_t value) {
    yukt_print_i64(value);
    printf("\n");
}

void yukt_say_f64(double value) {
    yukt_print_f64(value);
    printf("\n");
}

void yukt_say_bool(int8_t value) {
    yukt_print_bool(value);
    printf("\n");
}

void yukt_say_cstr(const char* value) {
    yukt_print_cstr(value);
    printf("\n");
}

void yukt_say_list(const YuktList* list) {
    print_list(list);
    printf("\n");
}

char* yukt_str_concat(const char* left, const char* right) {
    if (left == NULL) {
        left = "";
    }
    if (right == NULL) {
        right = "";
    }
    const size_t left_size = strlen(left);
    const size_t right_size = strlen(right);
    const size_t need = left_size + right_size + 1;
    const size_t bytes = yukt_alloc_size(need);
    char* out = (char*)yukt_cache_take(yukt_str_bins, bytes);
    if (out == NULL) {
        out = (char*)malloc(bytes);
        if (out == NULL) {
            die("out of memory");
        }
    }
    memcpy(out, left, left_size);
    memcpy(out + left_size, right, right_size);
    out[left_size + right_size] = '\0';
    yukt_track(YUKT_ALLOC_STR, out, bytes);
    return out;
}

int8_t yukt_str_eq(const char* left, const char* right) {
    if (left == NULL) {
        left = "";
    }
    if (right == NULL) {
        right = "";
    }
    return strcmp(left, right) == 0 ? 1 : 0;
}

static size_t element_size(int64_t kind) {
    switch (kind) {
        case 0:
            return sizeof(int64_t);
        case 1:
            return sizeof(double);
        case 2:
            return sizeof(int8_t);
        case 3:
        case 4:
            return sizeof(void*);
        default:
            return sizeof(void*);
    }
}

static size_t element_size_of(const YuktList* list) {
    if (list == NULL) {
        return sizeof(void*);
    }
    if (list->kind == 5) {
        return list->stride > 0 ? (size_t)list->stride : 0;
    }
    if (list->stride > 0) {
        return (size_t)list->stride;
    }
    return element_size(list->kind);
}

static void* yukt_alloc_aligned_zero(size_t bytes) {
    if (bytes == 0) {
        return NULL;
    }
    if (yukt_fail_next_payload_alloc) {
        yukt_fail_next_payload_alloc = 0;
        return NULL;
    }
    const size_t padded = yukt_alloc_size(yukt_pad16(bytes));
    void* data = yukt_cache_take(yukt_list_bins, padded);
    if (data == NULL) {
        data = aligned_alloc(16, padded);
        if (data == NULL) {
            return NULL;
        }
    }
    memset(data, 0, padded);
    return data;
}

YuktList* yukt_list_new(int64_t length, int64_t kind) {
    if (length < 0) {
        die("negative list length");
    }
    YuktList* list = (YuktList*)calloc(1, sizeof(YuktList));
    if (list == NULL) {
        die("out of memory");
    }
    list->length = length;
    list->kind = kind;
    list->stride = (int64_t)element_size(kind);
    list->capacity = length;
    list->generation = 0;
    size_t payload = 0;
    if (length > 0) {
        size_t nbytes = 0;
        if (yukt_size_mul((size_t)length, (size_t)list->stride, &nbytes) != 0) {
            die("out of memory");
        }
        payload = yukt_alloc_size(yukt_pad16(nbytes));
        list->data = yukt_alloc_aligned_zero(payload);
        if (payload > 0 && list->data == NULL) {
            free(list);
            die("out of memory");
        }
    }
    yukt_track(YUKT_ALLOC_LIST, list, payload);
    return list;
}

YuktList* yukt_list_new_rec(int64_t length, int64_t stride) {
    if (length < 0) {
        die("negative list length");
    }
    if (stride <= 0) {
        die("invalid record stride");
    }
    YuktList* list = (YuktList*)calloc(1, sizeof(YuktList));
    if (list == NULL) {
        die("out of memory");
    }
    list->length = length;
    list->kind = 5;
    list->stride = stride;
    list->capacity = length;
    list->generation = 0;
    size_t payload = 0;
    if (length > 0) {
        size_t nbytes = 0;
        if (yukt_size_mul((size_t)length, (size_t)stride, &nbytes) != 0) {
            die("out of memory");
        }
        payload = yukt_alloc_size(yukt_pad16(nbytes));
        list->data = yukt_alloc_aligned_zero(payload);
        if (payload > 0 && list->data == NULL) {
            free(list);
            die("out of memory");
        }
    }
    yukt_track(YUKT_ALLOC_LIST, list, payload);
    return list;
}

void yukt_list_set_rec(YuktList* list, int64_t index, const void* src, int64_t stride) {
    if (list == NULL || list->kind != 5 || src == NULL || index < 0 || index >= list->length) {
        die("list index out of range");
    }
    if (stride != list->stride || list->data == NULL) {
        die("record list layout mismatch");
    }
    /* Bitwise store. For records with owned fields the compiler drops the
     * previous element's heap fields before this call. */
    memcpy((char*)list->data + (size_t)index * (size_t)stride, src, (size_t)stride);
}

void yukt_list_get_rec(const YuktList* list, int64_t index, void* dest, int64_t stride) {
    if (list == NULL || list->kind != 5 || dest == NULL || index < 0 || index >= list->length) {
        die("list index out of range");
    }
    if (stride != list->stride || list->data == NULL) {
        die("record list layout mismatch");
    }
    memcpy(dest, (const char*)list->data + (size_t)index * (size_t)stride, (size_t)stride);
}

void yukt_list_set_i64(YuktList* list, int64_t index, int64_t value) {
    if (list == NULL || index < 0 || index >= list->length) {
        die("list index out of range");
    }
    ((int64_t*)list->data)[index] = value;
}

void yukt_list_set_f64(YuktList* list, int64_t index, double value) {
    if (list == NULL || index < 0 || index >= list->length) {
        die("list index out of range");
    }
    ((double*)list->data)[index] = value;
}

void yukt_list_set_bool(YuktList* list, int64_t index, int8_t value) {
    if (list == NULL || index < 0 || index >= list->length) {
        die("list index out of range");
    }
    ((int8_t*)list->data)[index] = value;
}

void yukt_list_set_cstr(YuktList* list, int64_t index, const char* value) {
    if (list == NULL || index < 0 || index >= list->length) {
        die("list index out of range");
    }
    const char** slot = &((const char**)list->data)[index];
    if (*slot != NULL && *slot != value) {
        yukt_release((void*)*slot);
    }
    *slot = value;
}

void yukt_list_set_list(YuktList* list, int64_t index, YuktList* value) {
    if (list == NULL || index < 0 || index >= list->length) {
        die("list index out of range");
    }
    YuktList** slot = &((YuktList**)list->data)[index];
    if (*slot != NULL && *slot != value) {
        yukt_release(*slot);
    }
    *slot = value;
}

YuktList* yukt_list_zeros(int64_t length) {
    return yukt_list_new(length, 0);
}

int64_t yukt_list_len(const YuktList* list) {
    return list == NULL ? 0 : list->length;
}

static void yukt_track_set_bytes(void* ptr, size_t bytes) {
    yukt_heap_lock();
    for (YuktAlloc* node = yukt_allocs; node != NULL; node = node->next) {
        if (node->ptr == ptr) {
            node->bytes = bytes;
            break;
        }
    }
    yukt_heap_unlock();
}

static size_t yukt_list_payload_bytes(const YuktList* list) {
    if (list == NULL || list->capacity <= 0) {
        return 0;
    }
    size_t nbytes = 0;
    if (yukt_size_mul((size_t)list->capacity, element_size_of(list), &nbytes) != 0) {
        return 0;
    }
    return yukt_alloc_size(yukt_pad16(nbytes));
}

static void yukt_list_bump_gen(YuktList* list) {
    if (list != NULL) {
        list->generation += 1;
    }
}

static void yukt_list_ensure(YuktList* list, int64_t extra) {
    if (list == NULL) {
        die("list index out of range");
    }
    if (extra < 0) {
        die("list too large");
    }
    if (list->length > INT64_MAX - extra) {
        die("list too large");
    }
    const int64_t need = list->length + extra;
    if (need <= list->capacity) {
        return;
    }
    int64_t new_cap = list->capacity <= 0 ? 8 : list->capacity;
    while (new_cap < need) {
        if (new_cap > INT64_MAX / 2) {
            new_cap = need;
            break;
        }
        new_cap *= 2;
    }
    size_t nbytes = 0;
    if (yukt_size_mul((size_t)new_cap, element_size_of(list), &nbytes) != 0) {
        die("list too large");
    }
    const size_t payload = yukt_alloc_size(yukt_pad16(nbytes));
    void* data = NULL;
    if (payload > 0) {
        data = yukt_alloc_aligned_zero(payload);
    }
    if (payload > 0 && data == NULL) {
        die("out of memory");
    }
    const size_t old_bytes = yukt_list_payload_bytes(list);
    if (list->data != NULL && list->length > 0 && data != NULL) {
        size_t live = 0;
        if (yukt_size_mul((size_t)list->length, element_size_of(list), &live) != 0) {
            die("list too large");
        }
        memcpy(data, list->data, live);
    }
    void* old_data = list->data;
    list->data = data;
    list->capacity = new_cap;
    yukt_track_set_bytes(list, payload);
    if (old_data != NULL) {
        if (!yukt_cache_put(yukt_list_bins, old_data, old_bytes)) {
            free(old_data);
        }
    }
}

static void yukt_list_require_index(const YuktList* list, int64_t index) {
    if (list == NULL || index < 0 || index >= list->length) {
        die("list index out of range");
    }
}

static void yukt_list_compact_tail(YuktList* list, int64_t index) {
    const size_t esz = element_size_of(list);
    const int64_t last = list->length - 1;
    if (index < last && list->data != NULL) {
        memmove((char*)list->data + (size_t)index * esz, (char*)list->data + (size_t)(index + 1) * esz,
                (size_t)(last - index) * esz);
    }
    if (list->data != NULL && last >= 0) {
        memset((char*)list->data + (size_t)last * esz, 0, esz);
    }
    list->length = last;
}

void yukt_list_append_i64(YuktList* list, int64_t value) {
    yukt_list_ensure(list, 1);
    yukt_list_bump_gen(list);
    ((int64_t*)list->data)[list->length] = value;
    list->length += 1;
}

void yukt_list_append_f64(YuktList* list, double value) {
    yukt_list_ensure(list, 1);
    yukt_list_bump_gen(list);
    ((double*)list->data)[list->length] = value;
    list->length += 1;
}

void yukt_list_append_bool(YuktList* list, int8_t value) {
    yukt_list_ensure(list, 1);
    yukt_list_bump_gen(list);
    ((int8_t*)list->data)[list->length] = value;
    list->length += 1;
}

void yukt_list_append_cstr(YuktList* list, const char* value) {
    yukt_list_ensure(list, 1);
    yukt_list_bump_gen(list);
    ((const char**)list->data)[list->length] = value;
    list->length += 1;
}

void yukt_list_append_list(YuktList* list, YuktList* value) {
    if (list != NULL && list == value) {
        die("cannot append a list to itself");
    }
    yukt_list_ensure(list, 1);
    yukt_list_bump_gen(list);
    ((YuktList**)list->data)[list->length] = value;
    list->length += 1;
}

void yukt_list_append_rec(YuktList* list, const void* src, int64_t stride) {
    if (list == NULL || list->kind != 5 || src == NULL || stride != list->stride) {
        die("record list layout mismatch");
    }
    yukt_list_ensure(list, 1);
    yukt_list_bump_gen(list);
    memcpy((char*)list->data + (size_t)list->length * (size_t)stride, src, (size_t)stride);
    list->length += 1;
}

int64_t yukt_list_remove_i64(YuktList* list, int64_t index) {
    yukt_list_require_index(list, index);
    yukt_list_bump_gen(list);
    const int64_t value = ((int64_t*)list->data)[index];
    yukt_list_compact_tail(list, index);
    return value;
}

double yukt_list_remove_f64(YuktList* list, int64_t index) {
    yukt_list_require_index(list, index);
    yukt_list_bump_gen(list);
    const double value = ((double*)list->data)[index];
    yukt_list_compact_tail(list, index);
    return value;
}

int8_t yukt_list_remove_bool(YuktList* list, int64_t index) {
    yukt_list_require_index(list, index);
    yukt_list_bump_gen(list);
    const int8_t value = ((int8_t*)list->data)[index];
    yukt_list_compact_tail(list, index);
    return value;
}

const char* yukt_list_remove_cstr(YuktList* list, int64_t index) {
    yukt_list_require_index(list, index);
    yukt_list_bump_gen(list);
    const char* value = ((const char**)list->data)[index];
    ((const char**)list->data)[index] = NULL;
    yukt_list_compact_tail(list, index);
    return value;
}

YuktList* yukt_list_remove_list(YuktList* list, int64_t index) {
    yukt_list_require_index(list, index);
    yukt_list_bump_gen(list);
    YuktList* value = ((YuktList**)list->data)[index];
    ((YuktList**)list->data)[index] = NULL;
    yukt_list_compact_tail(list, index);
    return value;
}

void yukt_list_remove_rec(YuktList* list, int64_t index, void* dest, int64_t stride) {
    yukt_list_require_index(list, index);
    if (list->kind != 5 || dest == NULL || stride != list->stride || list->data == NULL) {
        die("record list layout mismatch");
    }
    yukt_list_bump_gen(list);
    memcpy(dest, (const char*)list->data + (size_t)index * (size_t)stride, (size_t)stride);
    memset((char*)list->data + (size_t)index * (size_t)stride, 0, (size_t)stride);
    yukt_list_compact_tail(list, index);
}

static void yukt_list_require_pop(const YuktList* list) {
    if (list == NULL || list->length <= 0) {
        die("Cannot pop from an empty list");
    }
}

int64_t yukt_list_pop_i64(YuktList* list) {
    yukt_list_require_pop(list);
    return yukt_list_remove_i64(list, list->length - 1);
}

double yukt_list_pop_f64(YuktList* list) {
    yukt_list_require_pop(list);
    return yukt_list_remove_f64(list, list->length - 1);
}

int8_t yukt_list_pop_bool(YuktList* list) {
    yukt_list_require_pop(list);
    return yukt_list_remove_bool(list, list->length - 1);
}

const char* yukt_list_pop_cstr(YuktList* list) {
    yukt_list_require_pop(list);
    return yukt_list_remove_cstr(list, list->length - 1);
}

YuktList* yukt_list_pop_list(YuktList* list) {
    yukt_list_require_pop(list);
    return yukt_list_remove_list(list, list->length - 1);
}

void yukt_list_pop_rec(YuktList* list, void* dest, int64_t stride) {
    yukt_list_require_pop(list);
    yukt_list_remove_rec(list, list->length - 1, dest, stride);
}

int64_t yukt_list_generation(const YuktList* list) {
    return list == NULL ? 0 : list->generation;
}

void yukt_list_check_generation(const YuktList* list, int64_t expected) {
    if (list == NULL || list->generation != expected) {
        die("list changed during iteration");
    }
}

int64_t yukt_list_get_i64(const YuktList* list, int64_t index) {
    if (list == NULL || index < 0 || index >= list->length) {
        die("list index out of range");
    }
    return ((const int64_t*)list->data)[index];
}

double yukt_list_get_f64(const YuktList* list, int64_t index) {
    if (list == NULL || index < 0 || index >= list->length) {
        die("list index out of range");
    }
    return ((const double*)list->data)[index];
}

int8_t yukt_list_get_bool(const YuktList* list, int64_t index) {
    if (list == NULL || index < 0 || index >= list->length) {
        die("list index out of range");
    }
    return ((const int8_t*)list->data)[index];
}

const char* yukt_list_get_cstr(const YuktList* list, int64_t index) {
    if (list == NULL || index < 0 || index >= list->length) {
        die("list index out of range");
    }
    return ((const char* const*)list->data)[index];
}

YuktList* yukt_list_get_list(const YuktList* list, int64_t index) {
    if (list == NULL || index < 0 || index >= list->length) {
        die("list index out of range");
    }
    return ((YuktList* const*)list->data)[index];
}

enum { YUKT_PAR_MAX_TASKS = 32, YUKT_PAR_SEQ_LIMIT = 64 };

static long yukt_par_ncpu(void) {
    long ncpu = sysconf(_SC_NPROCESSORS_ONLN);
    if (ncpu < 1) {
        ncpu = 1;
    }
    if (ncpu > YUKT_PAR_MAX_TASKS) {
        ncpu = YUKT_PAR_MAX_TASKS;
    }
    return ncpu;
}

static int yukt_par_should_run_sequential(int64_t count, int64_t work_per_iter) {
    if (count < YUKT_PAR_SEQ_LIMIT) {
        return 1;
    }
    const long ncpu = yukt_par_ncpu();
    if (ncpu <= 1) {
        return 1;
    }
    const int64_t per = work_per_iter < 1 ? 1 : work_per_iter;
    if (count > (INT64_MAX / per)) {
        return 0;
    }
    const int64_t total = count * per;
    const int64_t tax = 100000 * ncpu;
    return total < tax;
}

static int yukt_par_plan(int64_t start, int64_t end, int64_t* begins, int64_t* lasts) {
    const int64_t count = end - start + 1;
    long ncpu = sysconf(_SC_NPROCESSORS_ONLN);
    if (ncpu < 1) {
        ncpu = 1;
    }
    if (ncpu > YUKT_PAR_MAX_TASKS) {
        ncpu = YUKT_PAR_MAX_TASKS;
    }
    if (ncpu > count) {
        ncpu = (long)count;
    }
    const int64_t chunk = (count + ncpu - 1) / ncpu;
    int tasks = 0;
    for (long t = 0; t < ncpu; ++t) {
        const int64_t begin = start + t * chunk;
        if (begin > end) {
            break;
        }
        int64_t last = begin + chunk - 1;
        if (last > end) {
            last = end;
        }
        begins[tasks] = begin;
        lasts[tasks] = last;
        ++tasks;
    }
    return tasks;
}

#if defined(__APPLE__)
void yukt_parallel_for(int64_t start, int64_t end, yukt_par_fn fn, void* context, int64_t work_per_iter) {
    if (fn == NULL || end < start) {
        return;
    }
    const int64_t count = end - start + 1;
    if (yukt_par_should_run_sequential(count, work_per_iter)) {
        fn(start, end, context);
        return;
    }

    long ncpu = yukt_par_ncpu();
    if (ncpu > count) {
        ncpu = (long)count;
    }
    if (ncpu <= 1) {
        fn(start, end, context);
        return;
    }
    const int64_t chunk = (count + ncpu - 1) / ncpu;
    dispatch_apply((size_t)ncpu, DISPATCH_APPLY_AUTO, ^(size_t t) {
        const int64_t begin = start + (int64_t)t * chunk;
        int64_t last = begin + chunk - 1;
        if (begin > end) {
            return;
        }
        if (last > end) {
            last = end;
        }
        fn(begin, last, context);
    });
}
#else
typedef struct {
    yukt_par_fn fn;
    void* context;
    int64_t begin;
    int64_t last;
} YuktParJob;

static pthread_mutex_t yukt_par_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t yukt_par_work = PTHREAD_COND_INITIALIZER;
static pthread_cond_t yukt_par_done = PTHREAD_COND_INITIALIZER;
static pthread_t yukt_par_threads[YUKT_PAR_MAX_TASKS];
static YuktParJob yukt_par_jobs[YUKT_PAR_MAX_TASKS];
static int yukt_par_nthreads = 0;
static int yukt_par_job_count = 0;
static int yukt_par_next_job = 0;
static int yukt_par_active = 0;
static int yukt_par_stop = 0;

static void* yukt_par_pool_worker(void* arg) {
    (void)arg;
    for (;;) {
        pthread_mutex_lock(&yukt_par_mu);
        while (!yukt_par_stop && yukt_par_next_job >= yukt_par_job_count) {
            pthread_cond_wait(&yukt_par_work, &yukt_par_mu);
        }
        if (yukt_par_stop) {
            pthread_mutex_unlock(&yukt_par_mu);
            return NULL;
        }
        const YuktParJob job = yukt_par_jobs[yukt_par_next_job++];
        yukt_par_active++;
        pthread_mutex_unlock(&yukt_par_mu);

        job.fn(job.begin, job.last, job.context);

        pthread_mutex_lock(&yukt_par_mu);
        yukt_par_active--;
        if (yukt_par_next_job >= yukt_par_job_count && yukt_par_active == 0) {
            pthread_cond_signal(&yukt_par_done);
        }
        pthread_mutex_unlock(&yukt_par_mu);
    }
}

static void yukt_par_ensure_pool(int want) {
    if (yukt_par_nthreads >= want) {
        return;
    }
    for (int t = yukt_par_nthreads; t < want; ++t) {
        if (pthread_create(&yukt_par_threads[t], NULL, yukt_par_pool_worker, NULL) != 0) {
            break;
        }
        yukt_par_nthreads = t + 1;
    }
}

void yukt_parallel_for(int64_t start, int64_t end, yukt_par_fn fn, void* context, int64_t work_per_iter) {
    if (fn == NULL || end < start) {
        return;
    }
    const int64_t count = end - start + 1;
    if (yukt_par_should_run_sequential(count, work_per_iter)) {
        fn(start, end, context);
        return;
    }

    int64_t begins[YUKT_PAR_MAX_TASKS];
    int64_t lasts[YUKT_PAR_MAX_TASKS];
    const int tasks = yukt_par_plan(start, end, begins, lasts);
    if (tasks <= 1) {
        fn(start, end, context);
        return;
    }

    pthread_mutex_lock(&yukt_par_mu);
    yukt_par_ensure_pool(tasks);
    if (yukt_par_nthreads < 1) {
        pthread_mutex_unlock(&yukt_par_mu);
        fn(start, end, context);
        return;
    }
    yukt_par_job_count = tasks;
    yukt_par_next_job = 0;
    yukt_par_active = 0;
    for (int t = 0; t < tasks; ++t) {
        yukt_par_jobs[t].fn = fn;
        yukt_par_jobs[t].context = context;
        yukt_par_jobs[t].begin = begins[t];
        yukt_par_jobs[t].last = lasts[t];
    }
    pthread_cond_broadcast(&yukt_par_work);
    while (yukt_par_next_job < yukt_par_job_count || yukt_par_active > 0) {
        pthread_cond_wait(&yukt_par_done, &yukt_par_mu);
    }
    pthread_mutex_unlock(&yukt_par_mu);
}
#endif

static const char* yukt_cstr(const char* s) {
    return s != NULL ? s : "";
}

static int yukt_size_mul(size_t a, size_t b, size_t* out) {
    if (b != 0 && a > SIZE_MAX / b) {
        return -1;
    }
    *out = a * b;
    return 0;
}

static char* yukt_str_alloc(size_t need) {
    if (need == 0) {
        need = 1;
    }
    const size_t bytes = yukt_alloc_size(need);
    char* out = (char*)yukt_cache_take(yukt_str_bins, bytes);
    if (out == NULL) {
        out = (char*)malloc(bytes);
        if (out == NULL) {
            die("out of memory");
        }
    }
    yukt_track(YUKT_ALLOC_STR, out, bytes);
    return out;
}

static char* yukt_str_from_bytes(const char* data, size_t len) {
    char* out = yukt_str_alloc(len + 1);
    if (len > 0 && data != NULL) {
        memcpy(out, data, len);
    }
    out[len] = '\0';
    return out;
}

char* yukt_str_from_lit(const char* s) {
    if (s == NULL) {
        s = "";
    }
    return yukt_str_from_bytes(s, strlen(s));
}

char* yukt_str_clone(const char* s) {
    if (s == NULL) {
        return NULL;
    }
    return yukt_str_from_lit(s);
}

int64_t yukt_str_len(const char* s) {
    return (int64_t)strlen(yukt_cstr(s));
}

int64_t yukt_str_at(const char* s, int64_t index) {
    s = yukt_cstr(s);
    const int64_t n = (int64_t)strlen(s);
    if (index < 0 || index >= n) {
        die("string index out of range");
    }
    return (unsigned char)s[index];
}

char* yukt_str_slice(const char* s, int64_t start, int64_t end) {
    s = yukt_cstr(s);
    const int64_t n = (int64_t)strlen(s);
    if (start < 0 || end < start || end > n) {
        die("invalid string slice");
    }
    return yukt_str_from_bytes(s + start, (size_t)(end - start));
}

int64_t yukt_str_find(const char* s, const char* sub) {
    s = yukt_cstr(s);
    sub = yukt_cstr(sub);
    if (sub[0] == '\0') {
        return 0;
    }
    const char* found = strstr(s, sub);
    if (found == NULL) {
        return -1;
    }
    return (int64_t)(found - s);
}

int8_t yukt_str_contains(const char* s, const char* sub) {
    return yukt_str_find(s, sub) != -1 ? 1 : 0;
}

int8_t yukt_str_starts(const char* s, const char* sub) {
    s = yukt_cstr(s);
    sub = yukt_cstr(sub);
    const size_t n = strlen(sub);
    return strncmp(s, sub, n) == 0 ? 1 : 0;
}

int8_t yukt_str_ends(const char* s, const char* sub) {
    s = yukt_cstr(s);
    sub = yukt_cstr(sub);
    const size_t n = strlen(s);
    const size_t m = strlen(sub);
    if (m > n) {
        return 0;
    }
    return memcmp(s + (n - m), sub, m) == 0 ? 1 : 0;
}

char* yukt_str_repeat(const char* s, int64_t n) {
    if (n < 0) {
        die("negative repeat count");
    }
    s = yukt_cstr(s);
    const size_t len = strlen(s);
    if (n == 0 || len == 0) {
        return yukt_str_from_bytes("", 0);
    }
    if ((uint64_t)n > SIZE_MAX / len) {
        die("out of memory");
    }
    size_t total = 0;
    if (yukt_size_mul(len, (size_t)n, &total) != 0 || total > SIZE_MAX - 1) {
        die("out of memory");
    }
    char* out = yukt_str_alloc(total + 1);
    for (int64_t i = 0; i < n; ++i) {
        memcpy(out + (size_t)i * len, s, len);
    }
    out[total] = '\0';
    return out;
}

char* yukt_str_join(const YuktList* parts, const char* sep) {
    sep = yukt_cstr(sep);
    if (parts == NULL) {
        return yukt_str_from_bytes("", 0);
    }
    if (parts->kind != 3) {
        die("join expects a list of strings");
    }
    const int64_t n = parts->length;
    if (n <= 0) {
        return yukt_str_from_bytes("", 0);
    }
    const char** data = (const char**)parts->data;
    const size_t sep_len = strlen(sep);
    size_t total = 0;
    for (int64_t i = 0; i < n; ++i) {
        const size_t piece = strlen(yukt_cstr(data[i]));
        if (total > SIZE_MAX - piece) {
            die("out of memory");
        }
        total += piece;
        if (i + 1 < n) {
            if (total > SIZE_MAX - sep_len) {
                die("out of memory");
            }
            total += sep_len;
        }
    }
    if (total > SIZE_MAX - 1) {
        die("out of memory");
    }
    char* out = yukt_str_alloc(total + 1);
    size_t off = 0;
    for (int64_t i = 0; i < n; ++i) {
        const char* piece = yukt_cstr(data[i]);
        const size_t piece_len = strlen(piece);
        memcpy(out + off, piece, piece_len);
        off += piece_len;
        if (i + 1 < n && sep_len > 0) {
            memcpy(out + off, sep, sep_len);
            off += sep_len;
        }
    }
    out[total] = '\0';
    return out;
}

YuktList* yukt_str_split(const char* s, const char* sep) {
    s = yukt_cstr(s);
    sep = yukt_cstr(sep);
    const size_t sep_len = strlen(sep);
    if (sep_len == 0) {
        die("empty split separator");
    }
    const size_t n = strlen(s);
    int64_t parts = 1;
    for (size_t i = 0; i + sep_len <= n;) {
        if (memcmp(s + i, sep, sep_len) == 0) {
            ++parts;
            i += sep_len;
        } else {
            ++i;
        }
    }
    YuktList* list = yukt_list_new(parts, 3);
    const char** data = (const char**)list->data;
    int64_t slot = 0;
    size_t start = 0;
    for (size_t i = 0; i + sep_len <= n;) {
        if (memcmp(s + i, sep, sep_len) == 0) {
            data[slot++] = yukt_str_from_bytes(s + start, i - start);
            i += sep_len;
            start = i;
        } else {
            ++i;
        }
    }
    data[slot] = yukt_str_from_bytes(s + start, n - start);
    return list;
}

static int yukt_ascii_ws(unsigned char c) {
    return c == 0x20 || c == 0x09 || c == 0x0A || c == 0x0B || c == 0x0C || c == 0x0D;
}

static const char* yukt_ascii_trim_view(const char* s, size_t* out_len) {
    s = yukt_cstr(s);
    size_t n = strlen(s);
    size_t begin = 0;
    while (begin < n && yukt_ascii_ws((unsigned char)s[begin])) {
        begin += 1;
    }
    size_t end = n;
    while (end > begin && yukt_ascii_ws((unsigned char)s[end - 1])) {
        end -= 1;
    }
    *out_len = end - begin;
    return s + begin;
}

char* yukt_str_trim(const char* s) {
    size_t len = 0;
    const char* view = yukt_ascii_trim_view(s, &len);
    return yukt_str_from_bytes(view, len);
}

char* yukt_str_trim_left(const char* s) {
    s = yukt_cstr(s);
    const size_t n = strlen(s);
    size_t begin = 0;
    while (begin < n && yukt_ascii_ws((unsigned char)s[begin])) {
        begin += 1;
    }
    return yukt_str_from_bytes(s + begin, n - begin);
}

char* yukt_str_trim_right(const char* s) {
    s = yukt_cstr(s);
    const size_t n = strlen(s);
    size_t end = n;
    while (end > 0 && yukt_ascii_ws((unsigned char)s[end - 1])) {
        end -= 1;
    }
    return yukt_str_from_bytes(s, end);
}

char* yukt_str_replace(const char* s, const char* old, const char* new_text) {
    s = yukt_cstr(s);
    old = yukt_cstr(old);
    new_text = yukt_cstr(new_text);
    const size_t old_len = strlen(old);
    if (old_len == 0) {
        die("empty replace pattern");
    }
    const size_t n = strlen(s);
    const size_t new_len = strlen(new_text);
    size_t out_len = 0;
    size_t i = 0;
    while (i < n) {
        if (i + old_len <= n && memcmp(s + i, old, old_len) == 0) {
            if (out_len > SIZE_MAX - new_len) {
                die("out of memory");
            }
            out_len += new_len;
            i += old_len;
        } else {
            if (out_len == SIZE_MAX) {
                die("out of memory");
            }
            out_len += 1;
            i += 1;
        }
    }
    if (out_len > SIZE_MAX - 1) {
        die("out of memory");
    }
    char* out = yukt_str_alloc(out_len + 1);
    size_t off = 0;
    i = 0;
    while (i < n) {
        if (i + old_len <= n && memcmp(s + i, old, old_len) == 0) {
            if (new_len > 0) {
                memcpy(out + off, new_text, new_len);
            }
            off += new_len;
            i += old_len;
        } else {
            out[off] = s[i];
            off += 1;
            i += 1;
        }
    }
    out[out_len] = '\0';
    return out;
}

char* yukt_str_lower(const char* s) {
    s = yukt_cstr(s);
    const size_t n = strlen(s);
    char* out = yukt_str_from_bytes(s, n);
    for (size_t i = 0; i < n; ++i) {
        const unsigned char c = (unsigned char)out[i];
        if (c >= 'A' && c <= 'Z') {
            out[i] = (char)(c + ('a' - 'A'));
        }
    }
    return out;
}

char* yukt_str_upper(const char* s) {
    s = yukt_cstr(s);
    const size_t n = strlen(s);
    char* out = yukt_str_from_bytes(s, n);
    for (size_t i = 0; i < n; ++i) {
        const unsigned char c = (unsigned char)out[i];
        if (c >= 'a' && c <= 'z') {
            out[i] = (char)(c - ('a' - 'A'));
        }
    }
    return out;
}

int64_t yukt_str_to_int(const char* s, int64_t* ok) {
    size_t n = 0;
    const char* p = yukt_ascii_trim_view(s, &n);
    if (ok != NULL) {
        *ok = 0;
    }
    if (n == 0) {
        return 0;
    }
    size_t i = 0;
    int neg = 0;
    if (p[0] == '+' || p[0] == '-') {
        neg = p[0] == '-';
        i += 1;
    }
    if (i >= n) {
        return 0;
    }
    const uint64_t limit = neg ? 9223372036854775808ULL : 9223372036854775807ULL;
    uint64_t acc = 0;
    for (; i < n; ++i) {
        const unsigned char c = (unsigned char)p[i];
        if (c < '0' || c > '9') {
            return 0;
        }
        const uint64_t digit = (uint64_t)(c - '0');
        if (acc > (limit - digit) / 10) {
            return 0;
        }
        acc = acc * 10 + digit;
    }
    if (ok != NULL) {
        *ok = 1;
    }
    if (neg) {
        if (acc == 9223372036854775808ULL) {
            return INT64_MIN;
        }
        return -(int64_t)acc;
    }
    return (int64_t)acc;
}

static double yukt_strtod_c(const char* s, char** end) {
    static locale_t c_locale;
    static int ready;
    if (!ready) {
        c_locale = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0);
        ready = 1;
    }
    if (c_locale != (locale_t)0) {
        return strtod_l(s, end, c_locale);
    }
    const char* old = setlocale(LC_NUMERIC, NULL);
    char saved[64];
    saved[0] = '\0';
    if (old != NULL) {
        strncpy(saved, old, sizeof(saved) - 1);
    }
    setlocale(LC_NUMERIC, "C");
    const double value = strtod(s, end);
    if (saved[0] != '\0') {
        setlocale(LC_NUMERIC, saved);
    }
    return value;
}

double yukt_str_to_float(const char* s, int64_t* ok) {
    size_t n = 0;
    const char* p = yukt_ascii_trim_view(s, &n);
    if (ok != NULL) {
        *ok = 0;
    }
    if (n == 0) {
        return 0.0;
    }
    char* end = NULL;
    const double value = yukt_strtod_c(p, &end);
    if (end != p + n || !isfinite(value)) {
        return 0.0;
    }
    if (ok != NULL) {
        *ok = 1;
    }
    return value;
}

int64_t yukt_str_to_int_status(const char* s) {
    int64_t ok = 0;
    (void)yukt_str_to_int(s, &ok);
    return ok ? 0 : 1;
}

int64_t yukt_str_to_int_unchecked(const char* s) {
    int64_t ok = 0;
    return yukt_str_to_int(s, &ok);
}

int64_t yukt_str_to_float_status(const char* s) {
    int64_t ok = 0;
    (void)yukt_str_to_float(s, &ok);
    return ok ? 0 : 1;
}

double yukt_str_to_float_unchecked(const char* s) {
    int64_t ok = 0;
    return yukt_str_to_float(s, &ok);
}

static YuktList* yukt_list_clone_range(const YuktList* src, int64_t start, int64_t length) {
    if (src == NULL) {
        return yukt_list_new(0, 0);
    }
    YuktList* dest = src->kind == 5 ? yukt_list_new_rec(length, src->stride) : yukt_list_new(length, src->kind);
    if (length <= 0 || src->data == NULL || dest->data == NULL) {
        return dest;
    }
    if (src->kind == 3) {
        const char* const* from = (const char* const*)src->data;
        const char** to = (const char**)dest->data;
        for (int64_t i = 0; i < length; ++i) {
            to[i] = yukt_str_clone(from[start + i]);
        }
        return dest;
    }
    if (src->kind == 4) {
        YuktList* const* from = (YuktList* const*)src->data;
        YuktList** to = (YuktList**)dest->data;
        for (int64_t i = 0; i < length; ++i) {
            to[i] = from[start + i] == NULL ? NULL : yukt_list_copy(from[start + i]);
        }
        return dest;
    }
    /* Kind 0/1/2 and kind-5 plain records stay memcpy. Kind-5 owned records
     * are cloned by the compiler, which does not call this path. */
    const size_t esz = element_size_of(src);
    size_t bytes = 0;
    if (yukt_size_mul((size_t)length, esz, &bytes) != 0) {
        die("out of memory");
    }
    memcpy(dest->data, (const char*)src->data + (size_t)start * esz, bytes);
    return dest;
}

YuktList* yukt_list_copy(const YuktList* src) {
    if (src == NULL) {
        return yukt_list_new(0, 0);
    }
    return yukt_list_clone_range(src, 0, src->length);
}

static void yukt_require_kind(const YuktList* dest, int64_t kind, const char* what) {
    if (dest == NULL || dest->kind != kind) {
        die(what);
    }
}

void yukt_list_fill_i64(YuktList* dest, int64_t value) {
    yukt_require_kind(dest, 0, "list fill type mismatch");
    for (int64_t i = 0; i < dest->length; ++i) {
        ((int64_t*)dest->data)[i] = value;
    }
}

void yukt_list_fill_f64(YuktList* dest, double value) {
    yukt_require_kind(dest, 1, "list fill type mismatch");
    for (int64_t i = 0; i < dest->length; ++i) {
        ((double*)dest->data)[i] = value;
    }
}

void yukt_list_fill_bool(YuktList* dest, int8_t value) {
    yukt_require_kind(dest, 2, "list fill type mismatch");
    for (int64_t i = 0; i < dest->length; ++i) {
        ((int8_t*)dest->data)[i] = value;
    }
}

void yukt_list_fill_cstr(YuktList* dest, const char* value) {
    yukt_require_kind(dest, 3, "list fill type mismatch");
    if (dest->data == NULL || dest->length <= 0) {
        return;
    }
    const char** data = (const char**)dest->data;
    /* Clone first so `fill(names, names[0])` cannot free the prototype. */
    char* proto = yukt_str_clone(value);
    for (int64_t i = 0; i < dest->length; ++i) {
        if (data[i] != NULL) {
            yukt_release((void*)data[i]);
            data[i] = NULL;
        }
    }
    for (int64_t i = 0; i < dest->length; ++i) {
        data[i] = yukt_str_clone(proto);
    }
    yukt_release(proto);
}

void yukt_list_fill_list(YuktList* dest, YuktList* value) {
    yukt_require_kind(dest, 4, "list fill type mismatch");
    if (dest->data == NULL || dest->length <= 0) {
        return;
    }
    YuktList** data = (YuktList**)dest->data;
    /* Clone first so a dest slot used as the prototype is not released under us. */
    YuktList* proto = yukt_list_copy(value);
    for (int64_t i = 0; i < dest->length; ++i) {
        if (data[i] != NULL) {
            yukt_release(data[i]);
            data[i] = NULL;
        }
    }
    for (int64_t i = 0; i < dest->length; ++i) {
        data[i] = yukt_list_copy(proto);
    }
    yukt_release(proto);
}

void yukt_list_reverse(YuktList* dest) {
    if (dest == NULL || dest->length <= 1 || dest->data == NULL) {
        return;
    }
    const size_t esz = element_size_of(dest);
    if (esz == 0) {
        return;
    }
    char stack_tmp[16];
    char* tmp = esz <= sizeof(stack_tmp) ? stack_tmp : (char*)malloc(esz);
    if (tmp == NULL) {
        die("out of memory");
    }
    char* data = (char*)dest->data;
    for (int64_t i = 0, j = dest->length - 1; i < j; ++i, --j) {
        memcpy(tmp, data + (size_t)i * esz, esz);
        memcpy(data + (size_t)i * esz, data + (size_t)j * esz, esz);
        memcpy(data + (size_t)j * esz, tmp, esz);
    }
    if (tmp != stack_tmp) {
        free(tmp);
    }
}

void yukt_list_check_slice(const YuktList* src, int64_t start, int64_t end) {
    const int64_t n = src == NULL ? 0 : src->length;
    if (start < 0 || end < start || end > n) {
        die("invalid list slice");
    }
}

YuktList* yukt_list_slice(const YuktList* src, int64_t start, int64_t end) {
    yukt_list_check_slice(src, start, end);
    if (src == NULL) {
        return yukt_list_new(0, 0);
    }
    return yukt_list_clone_range(src, start, end - start);
}

static int yukt_i64_after(int64_t a, int64_t b, int descending) {
    if (a == b) {
        return 0;
    }
    return descending ? a < b : a > b;
}

static int yukt_f64_asc_before(double a, double b) {
    const int nan_a = isnan(a);
    const int nan_b = isnan(b);
    if (nan_a != nan_b) {
        return !nan_a;
    }
    if (nan_a) {
        return 0;
    }
    return a < b;
}

static int yukt_f64_after(double a, double b, int descending) {
    return descending ? yukt_f64_asc_before(a, b) : yukt_f64_asc_before(b, a);
}

static void yukt_sift_i64(int64_t* data, int64_t n, int64_t root, int descending) {
    for (;;) {
        if (root > (INT64_MAX - 1) / 2) {
            break;
        }
        const int64_t left = root * 2 + 1;
        if (left >= n) {
            break;
        }
        int64_t pick = left;
        const int64_t right = left + 1;
        if (right < n && yukt_i64_after(data[right], data[pick], descending)) {
            pick = right;
        }
        if (!yukt_i64_after(data[pick], data[root], descending)) {
            break;
        }
        const int64_t tmp = data[root];
        data[root] = data[pick];
        data[pick] = tmp;
        root = pick;
    }
}

static void yukt_sift_f64(double* data, int64_t n, int64_t root, int descending) {
    for (;;) {
        if (root > (INT64_MAX - 1) / 2) {
            break;
        }
        const int64_t left = root * 2 + 1;
        if (left >= n) {
            break;
        }
        int64_t pick = left;
        const int64_t right = left + 1;
        if (right < n && yukt_f64_after(data[right], data[pick], descending)) {
            pick = right;
        }
        if (!yukt_f64_after(data[pick], data[root], descending)) {
            break;
        }
        const double tmp = data[root];
        data[root] = data[pick];
        data[pick] = tmp;
        root = pick;
    }
}

static void yukt_sort_i64(int64_t* data, int64_t n, int descending) {
    for (int64_t i = n / 2 - 1; i >= 0; --i) {
        yukt_sift_i64(data, n, i, descending);
    }
    for (int64_t end = n - 1; end > 0; --end) {
        const int64_t tmp = data[0];
        data[0] = data[end];
        data[end] = tmp;
        yukt_sift_i64(data, end, 0, descending);
    }
}

static void yukt_sort_f64(double* data, int64_t n, int descending) {
    for (int64_t i = n / 2 - 1; i >= 0; --i) {
        yukt_sift_f64(data, n, i, descending);
    }
    for (int64_t end = n - 1; end > 0; --end) {
        const double tmp = data[0];
        data[0] = data[end];
        data[end] = tmp;
        yukt_sift_f64(data, end, 0, descending);
    }
}

static void yukt_require_numeric_list(const YuktList* items) {
    if (items == NULL || (items->kind != 0 && items->kind != 1)) {
        die("sort requires a numeric list");
    }
}

void yukt_list_sort_asc(YuktList* items) {
    yukt_require_numeric_list(items);
    if (items->length <= 1 || items->data == NULL) {
        return;
    }
    if (items->kind == 0) {
        yukt_sort_i64((int64_t*)items->data, items->length, 0);
    } else {
        yukt_sort_f64((double*)items->data, items->length, 0);
    }
}

void yukt_list_sort_desc(YuktList* items) {
    yukt_require_numeric_list(items);
    if (items->length <= 1 || items->data == NULL) {
        return;
    }
    if (items->kind == 0) {
        yukt_sort_i64((int64_t*)items->data, items->length, 1);
    } else {
        yukt_sort_f64((double*)items->data, items->length, 1);
    }
}

YuktList* yukt_list_sorted(const YuktList* items) {
    YuktList* copy = yukt_list_copy(items);
    yukt_list_sort_asc(copy);
    return copy;
}

int64_t yukt_list_search(const YuktList* items, int64_t ivalue, double fvalue) {
    yukt_require_numeric_list(items);
    int64_t lo = 0;
    int64_t hi = items->length;
    if (items->kind == 0) {
        const int64_t* data = (const int64_t*)items->data;
        while (lo < hi) {
            const int64_t mid = lo + (hi - lo) / 2;
            if (data[mid] < ivalue) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
        if (lo < items->length && data[lo] == ivalue) {
            return lo;
        }
        return -1;
    }
    const double* data = (const double*)items->data;
    while (lo < hi) {
        const int64_t mid = lo + (hi - lo) / 2;
        if (yukt_f64_asc_before(data[mid], fvalue)) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    if (lo < items->length && (isnan(fvalue) ? isnan(data[lo]) : data[lo] == fvalue)) {
        return lo;
    }
    return -1;
}

int64_t yukt_math_abs_i(int64_t x) {
    return x < 0 ? -x : x;
}

double yukt_math_abs_f(double x) {
    return fabs(x);
}

int64_t yukt_math_min_i(int64_t a, int64_t b) {
    return a <= b ? a : b;
}

int64_t yukt_math_max_i(int64_t a, int64_t b) {
    return a >= b ? a : b;
}

double yukt_math_min_f(double a, double b) {
    if (isnan(a) || isnan(b)) {
        return NAN;
    }
    return a < b ? a : b;
}

double yukt_math_max_f(double a, double b) {
    if (isnan(a) || isnan(b)) {
        return NAN;
    }
    return a > b ? a : b;
}

double yukt_math_floor(double x) {
    return floor(x);
}

double yukt_math_ceil(double x) {
    return ceil(x);
}

double yukt_math_sqrt(double x) {
    return sqrt(x);
}

double yukt_math_pow(double x, double y) {
    return pow(x, y);
}

double yukt_math_sin(double x) {
    return sin(x);
}

double yukt_math_cos(double x) {
    return cos(x);
}

double yukt_math_tan(double x) {
    return tan(x);
}

double yukt_math_exp(double x) {
    return exp(x);
}

double yukt_math_log(double x) {
    return log(x);
}

static void yukt_require_nonempty(const YuktList* items, int64_t kind) {
    if (items == NULL || items->kind != kind || items->length <= 0) {
        die("empty list");
    }
}

int64_t yukt_stat_min_i(const YuktList* items) {
    yukt_require_nonempty(items, 0);
    const int64_t* data = (const int64_t*)items->data;
    int64_t best = data[0];
    for (int64_t i = 1; i < items->length; ++i) {
        if (data[i] < best) {
            best = data[i];
        }
    }
    return best;
}

int64_t yukt_stat_max_i(const YuktList* items) {
    yukt_require_nonempty(items, 0);
    const int64_t* data = (const int64_t*)items->data;
    int64_t best = data[0];
    for (int64_t i = 1; i < items->length; ++i) {
        if (data[i] > best) {
            best = data[i];
        }
    }
    return best;
}

double yukt_stat_min_f(const YuktList* items) {
    yukt_require_nonempty(items, 1);
    const double* data = (const double*)items->data;
    double best = data[0];
    for (int64_t i = 1; i < items->length; ++i) {
        if (isnan(data[i]) || (!(isnan(best)) && data[i] < best) || isnan(best)) {
            best = data[i];
        }
    }
    return best;
}

double yukt_stat_max_f(const YuktList* items) {
    yukt_require_nonempty(items, 1);
    const double* data = (const double*)items->data;
    double best = data[0];
    for (int64_t i = 1; i < items->length; ++i) {
        if (isnan(data[i]) || (!(isnan(best)) && data[i] > best) || isnan(best)) {
            best = data[i];
        }
    }
    return best;
}

double yukt_stat_mean_f(const YuktList* items) {
    yukt_require_nonempty(items, 1);
    const double* data = (const double*)items->data;
    double total = 0.0;
    for (int64_t i = 0; i < items->length; ++i) {
        total += data[i];
    }
    return total / (double)items->length;
}

int64_t yukt_point_sum(const int64_t* point) {
    if (point == NULL) {
        return 0;
    }
    return point[0] + point[1];
}

int8_t yukt_bool_and(int8_t a, int8_t b) {
    return (a != 0 && b != 0) ? 1 : 0;
}

enum {
    YUKT_TASK_QUEUED = 0,
    YUKT_TASK_RUNNING = 1,
    YUKT_TASK_COMPLETED = 2,
    YUKT_TASK_CONSUMED = 3,
};

typedef struct YuktTask {
    atomic_int state;
    atomic_int refs;
    yukt_task_fn fn;
    void* args;
    void* result;
    size_t result_bytes;
    pthread_mutex_t mu;
    pthread_cond_t done;
} YuktTask;

static pthread_mutex_t yukt_task_life_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t yukt_task_life_cv = PTHREAD_COND_INITIALIZER;
static int yukt_task_busy = 0;
static int yukt_task_ops = 0;

/* Recent consumed handles, for double-wait after the wrapper is freed.
 * Bounded: one slot is overwritten when the ring is full. A void* handle
 * cannot carry a generation, so a malloc-recycled address is a new task
 * and must drop any stale tomb (yukt_task_forget_consumed). */
enum { YUKT_TASK_TOMB_CAP = 256 };

static void* yukt_task_tombs[YUKT_TASK_TOMB_CAP];
static unsigned yukt_task_tomb_head;
static unsigned yukt_task_tomb_count;

static unsigned yukt_task_tomb_index(unsigned n) {
    return (yukt_task_tomb_head + YUKT_TASK_TOMB_CAP - yukt_task_tomb_count + n) % YUKT_TASK_TOMB_CAP;
}

static void yukt_task_tomb_clear_slot(void* ptr) {
    for (unsigned n = 0; n < yukt_task_tomb_count; ++n) {
        const unsigned i = yukt_task_tomb_index(n);
        if (yukt_task_tombs[i] == ptr) {
            yukt_task_tombs[i] = NULL;
            return;
        }
    }
}

static void yukt_task_mark_consumed(void* ptr) {
    if (ptr == NULL) {
        return;
    }
    pthread_mutex_lock(&yukt_task_life_mu);
    yukt_task_tomb_clear_slot(ptr);
    yukt_task_tombs[yukt_task_tomb_head] = ptr;
    yukt_task_tomb_head = (yukt_task_tomb_head + 1) % YUKT_TASK_TOMB_CAP;
    if (yukt_task_tomb_count < YUKT_TASK_TOMB_CAP) {
        yukt_task_tomb_count++;
    }
    pthread_mutex_unlock(&yukt_task_life_mu);
}

static int yukt_task_already_consumed(void* ptr) {
    int found = 0;
    pthread_mutex_lock(&yukt_task_life_mu);
    for (unsigned n = 0; n < yukt_task_tomb_count; ++n) {
        if (yukt_task_tombs[yukt_task_tomb_index(n)] == ptr) {
            found = 1;
            break;
        }
    }
    pthread_mutex_unlock(&yukt_task_life_mu);
    return found;
}

static void yukt_task_forget_consumed(void* ptr) {
    pthread_mutex_lock(&yukt_task_life_mu);
    yukt_task_tomb_clear_slot(ptr);
    pthread_mutex_unlock(&yukt_task_life_mu);
}

static void yukt_task_clear_consumed(void) {
    pthread_mutex_lock(&yukt_task_life_mu);
    memset(yukt_task_tombs, 0, sizeof(yukt_task_tombs));
    yukt_task_tomb_head = 0;
    yukt_task_tomb_count = 0;
    pthread_mutex_unlock(&yukt_task_life_mu);
}

#if !defined(__APPLE__)
typedef struct YuktTaskNode {
    YuktTask* task;
    struct YuktTaskNode* next;
} YuktTaskNode;

static pthread_mutex_t yukt_task_qmu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t yukt_task_qcv = PTHREAD_COND_INITIALIZER;
static YuktTaskNode* yukt_task_qhead = NULL;
static YuktTaskNode* yukt_task_qtail = NULL;
static pthread_t yukt_task_threads[YUKT_PAR_MAX_TASKS];
static int yukt_task_nthreads = 0;
static int yukt_task_stop = 0;
#endif

static void yukt_task_destroy_sync(void* ptr) {
    YuktTask* task = (YuktTask*)ptr;
    if (task == NULL) {
        return;
    }
    pthread_mutex_destroy(&task->mu);
    pthread_cond_destroy(&task->done);
}

static void yukt_task_note_start(void) {
    pthread_mutex_lock(&yukt_task_life_mu);
    yukt_task_busy++;
    yukt_task_ops++;
    pthread_mutex_unlock(&yukt_task_life_mu);
}

static void yukt_task_note_body_done(void) {
    pthread_mutex_lock(&yukt_task_life_mu);
    yukt_task_busy--;
    pthread_cond_broadcast(&yukt_task_life_cv);
    pthread_mutex_unlock(&yukt_task_life_mu);
}

static void yukt_task_note_op_done(void) {
    pthread_mutex_lock(&yukt_task_life_mu);
    yukt_task_ops--;
    pthread_cond_broadcast(&yukt_task_life_cv);
    pthread_mutex_unlock(&yukt_task_life_mu);
}

static void yukt_task_run_body(YuktTask* task) {
    if (task->fn != NULL) {
        task->fn(task->args, task->result);
    }
    pthread_mutex_lock(&task->mu);
    atomic_store(&task->state, YUKT_TASK_COMPLETED);
    pthread_cond_broadcast(&task->done);
    pthread_mutex_unlock(&task->mu);
    yukt_task_note_body_done();
}

static int yukt_task_is_tracked(void* ptr) {
    int found = 0;
    yukt_heap_lock();
    for (YuktAlloc* node = yukt_allocs; node != NULL; node = node->next) {
        if (node->ptr == ptr) {
            found = 1;
            break;
        }
    }
    yukt_heap_unlock();
    return found;
}

/* Wait until the worker has dropped its ref so the wrapper is untracked.
 * After consume the waiter no longer owns the object; this only compares the
 * pointer against the tracker and waits on life_cv. */
static void yukt_task_wait_untracked(void* ptr) {
    pthread_mutex_lock(&yukt_task_life_mu);
    while (yukt_task_is_tracked(ptr)) {
        pthread_cond_wait(&yukt_task_life_cv, &yukt_task_life_mu);
    }
    pthread_mutex_unlock(&yukt_task_life_mu);
}

static void yukt_task_drop(YuktTask* task) {
    if (task == NULL) {
        return;
    }
    if (atomic_fetch_sub(&task->refs, 1) == 1) {
        yukt_release(task);
        pthread_mutex_lock(&yukt_task_life_mu);
        pthread_cond_broadcast(&yukt_task_life_cv);
        pthread_mutex_unlock(&yukt_task_life_mu);
    }
}

void yukt_task_worker_try(void* task_ptr) {
    YuktTask* task = (YuktTask*)task_ptr;
    if (task == NULL) {
        return;
    }
    int expected = YUKT_TASK_QUEUED;
    if (atomic_compare_exchange_strong(&task->state, &expected, YUKT_TASK_RUNNING)) {
        yukt_task_run_body(task);
    }
}

#if !defined(__APPLE__)
static void* yukt_task_pool_worker(void* arg) {
    (void)arg;
    for (;;) {
        pthread_mutex_lock(&yukt_task_qmu);
        while (!yukt_task_stop && yukt_task_qhead == NULL) {
            pthread_cond_wait(&yukt_task_qcv, &yukt_task_qmu);
        }
        if (yukt_task_stop && yukt_task_qhead == NULL) {
            pthread_mutex_unlock(&yukt_task_qmu);
            return NULL;
        }
        YuktTaskNode* node = yukt_task_qhead;
        yukt_task_qhead = node->next;
        if (yukt_task_qhead == NULL) {
            yukt_task_qtail = NULL;
        }
        pthread_mutex_unlock(&yukt_task_qmu);
        yukt_task_worker_try(node->task);
        yukt_task_drop(node->task);
        yukt_task_note_op_done();
        free(node);
    }
}

static void yukt_task_ensure_pool(void) {
    if (yukt_task_nthreads > 0) {
        return;
    }
    long ncpu = yukt_par_ncpu();
    if (ncpu < 1) {
        ncpu = 1;
    }
    if (ncpu > YUKT_PAR_MAX_TASKS) {
        ncpu = YUKT_PAR_MAX_TASKS;
    }
    yukt_task_stop = 0;
    for (int t = 0; t < ncpu; ++t) {
        if (pthread_create(&yukt_task_threads[t], NULL, yukt_task_pool_worker, NULL) != 0) {
            break;
        }
        yukt_task_nthreads = t + 1;
    }
}

static void yukt_task_enqueue(YuktTask* task) {
    pthread_mutex_lock(&yukt_task_qmu);
    yukt_task_ensure_pool();
    if (yukt_task_nthreads < 1) {
        pthread_mutex_unlock(&yukt_task_qmu);
        yukt_task_worker_try(task);
        yukt_task_drop(task);
        yukt_task_note_op_done();
        return;
    }
    YuktTaskNode* node = (YuktTaskNode*)malloc(sizeof(YuktTaskNode));
    if (node == NULL) {
        pthread_mutex_unlock(&yukt_task_qmu);
        die("out of memory");
    }
    node->task = task;
    node->next = NULL;
    if (yukt_task_qtail == NULL) {
        yukt_task_qhead = node;
        yukt_task_qtail = node;
    } else {
        yukt_task_qtail->next = node;
        yukt_task_qtail = node;
    }
    pthread_cond_signal(&yukt_task_qcv);
    pthread_mutex_unlock(&yukt_task_qmu);
}
#endif

void* yukt_task_start(yukt_task_fn fn, void* args, size_t result_bytes, size_t result_align) {
    if (fn == NULL) {
        die("task function is null");
    }
    if (result_align < 1) {
        result_align = 1;
    }
    size_t prefix = sizeof(YuktTask);
    if (result_bytes > 0) {
        const size_t rem = prefix % result_align;
        if (rem != 0) {
            prefix += result_align - rem;
        }
    }
    const size_t total = prefix + result_bytes;
    YuktTask* task = (YuktTask*)calloc(1, total);
    if (task == NULL) {
        die("out of memory");
    }
    atomic_init(&task->state, YUKT_TASK_QUEUED);
    atomic_init(&task->refs, 2);
    task->fn = fn;
    task->args = args;
    task->result_bytes = result_bytes;
    task->result = result_bytes == 0 ? NULL : (void*)((char*)task + prefix);
    if (pthread_mutex_init(&task->mu, NULL) != 0 || pthread_cond_init(&task->done, NULL) != 0) {
        free(task);
        die("out of memory");
    }
    yukt_task_forget_consumed(task);
    yukt_track(YUKT_ALLOC_TASK, task, total);
    yukt_task_note_start();
#if defined(__APPLE__)
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_DEFAULT, 0), ^{
        yukt_task_worker_try(task);
        yukt_task_drop(task);
        yukt_task_note_op_done();
    });
#else
    yukt_task_enqueue(task);
#endif
    return task;
}

static void yukt_task_consume(YuktTask* task, void* result_out) {
    int expected = YUKT_TASK_COMPLETED;
    if (!atomic_compare_exchange_strong(&task->state, &expected, YUKT_TASK_CONSUMED)) {
        die("double wait or untracked task");
    }
    if (result_out != NULL && task->result != NULL && task->result_bytes > 0) {
        memcpy(result_out, task->result, task->result_bytes);
    }
    yukt_task_mark_consumed(task);
    yukt_task_drop(task);
}

void yukt_task_wait(void* task_ptr, void* result_out) {
    if (task_ptr == NULL || yukt_task_already_consumed(task_ptr)) {
        die("double wait or untracked task");
    }
    YuktTask* task = (YuktTask*)task_ptr;
    int expected = YUKT_TASK_QUEUED;
    if (atomic_compare_exchange_strong(&task->state, &expected, YUKT_TASK_RUNNING)) {
        yukt_task_run_body(task);
        yukt_task_consume(task, result_out);
        yukt_task_wait_untracked(task);
        return;
    }
    pthread_mutex_lock(&task->mu);
    while (atomic_load(&task->state) == YUKT_TASK_RUNNING) {
        pthread_cond_wait(&task->done, &task->mu);
    }
    const int state = atomic_load(&task->state);
    pthread_mutex_unlock(&task->mu);
    if (state != YUKT_TASK_COMPLETED) {
        die("double wait or untracked task");
    }
    yukt_task_consume(task, result_out);
    yukt_task_wait_untracked(task);
}

void yukt_task_shutdown(void) {
    pthread_mutex_lock(&yukt_task_life_mu);
    while (yukt_task_busy > 0 || yukt_task_ops > 0) {
        pthread_cond_wait(&yukt_task_life_cv, &yukt_task_life_mu);
    }
    pthread_mutex_unlock(&yukt_task_life_mu);
    yukt_task_clear_consumed();
#if !defined(__APPLE__)
    pthread_mutex_lock(&yukt_task_qmu);
    yukt_task_stop = 1;
    pthread_cond_broadcast(&yukt_task_qcv);
    pthread_mutex_unlock(&yukt_task_qmu);
    for (int t = 0; t < yukt_task_nthreads; ++t) {
        pthread_join(yukt_task_threads[t], NULL);
    }
    yukt_task_nthreads = 0;
    yukt_task_stop = 0;
#endif
}

enum { YUKT_IO_LINE_CAP = 16 * 1024 * 1024 };

static int yukt_io_argc = 0;
static char** yukt_io_argv = NULL;

static char* yukt_io_line_text = NULL;
static int64_t yukt_io_line_status = 0;
static int yukt_io_line_ready = 0;

static char* yukt_io_all_text = NULL;
static int64_t yukt_io_all_status = 0;
static int yukt_io_all_ready = 0;

void yukt_io_set_args(int argc, char** argv) {
    yukt_io_argc = argc;
    yukt_io_argv = argv;
}

char* yukt_io_program(void) {
    if (yukt_io_argc <= 0 || yukt_io_argv == NULL || yukt_io_argv[0] == NULL) {
        return yukt_str_from_lit("");
    }
    return yukt_str_from_lit(yukt_io_argv[0]);
}

YuktList* yukt_io_args(void) {
    const int64_t n = yukt_io_argc > 0 ? (int64_t)yukt_io_argc - 1 : 0;
    YuktList* list = yukt_list_new(n, 3);
    for (int64_t i = 0; i < n; ++i) {
        const char* arg = yukt_io_argv != NULL ? yukt_io_argv[i + 1] : NULL;
        yukt_list_set_cstr(list, i, yukt_str_from_lit(arg));
    }
    return list;
}

char* yukt_io_read_line(int64_t* status) {
    size_t cap = 128;
    size_t len = 0;
    char* buf = (char*)malloc(cap);
    if (buf == NULL) {
        die("out of memory");
    }
    while (1) {
        const int ch = fgetc(stdin);
        if (ch == EOF) {
            if (ferror(stdin)) {
                free(buf);
                if (status != NULL) {
                    *status = 2;
                }
                return yukt_str_from_lit("");
            }
            if (len == 0) {
                free(buf);
                if (status != NULL) {
                    *status = 1;
                }
                return yukt_str_from_lit("");
            }
            if (status != NULL) {
                *status = 0;
            }
            char* owned = yukt_str_from_bytes(buf, len);
            free(buf);
            return owned;
        }
        if (ch == '\n') {
            if (len > 0 && buf[len - 1] == '\r') {
                len -= 1;
            }
            if (status != NULL) {
                *status = 0;
            }
            char* owned = yukt_str_from_bytes(buf, len);
            free(buf);
            return owned;
        }
        if (len >= (size_t)YUKT_IO_LINE_CAP) {
            free(buf);
            die("line too large");
        }
        if (len + 1 >= cap) {
            size_t ncap = cap * 2;
            if (ncap > (size_t)YUKT_IO_LINE_CAP + 1) {
                ncap = (size_t)YUKT_IO_LINE_CAP + 1;
            }
            char* grown = (char*)realloc(buf, ncap);
            if (grown == NULL) {
                free(buf);
                die("out of memory");
            }
            buf = grown;
            cap = ncap;
        }
        buf[len] = (char)ch;
        len += 1;
    }
}

char* yukt_io_read_all(int64_t* status) {
    size_t cap = 256;
    size_t len = 0;
    char* buf = (char*)malloc(cap);
    if (buf == NULL) {
        die("out of memory");
    }
    while (1) {
        if (len + 1 >= cap) {
            size_t ncap = cap * 2;
            char* grown = (char*)realloc(buf, ncap);
            if (grown == NULL) {
                free(buf);
                die("out of memory");
            }
            buf = grown;
            cap = ncap;
        }
        const size_t n = fread(buf + len, 1, cap - len - 1, stdin);
        len += n;
        if (n > 0) {
            continue;
        }
        if (ferror(stdin)) {
            free(buf);
            if (status != NULL) {
                *status = 2;
            }
            return yukt_str_from_lit("");
        }
        if (status != NULL) {
            *status = 0;
        }
        char* owned = yukt_str_from_bytes(buf, len);
        free(buf);
        return owned;
    }
}

static void yukt_io_ensure_line(void) {
    if (yukt_io_line_ready) {
        return;
    }
    yukt_io_line_text = yukt_io_read_line(&yukt_io_line_status);
    yukt_io_line_ready = 1;
}

int64_t yukt_io_read_line_status(void) {
    yukt_io_ensure_line();
    return yukt_io_line_status;
}

char* yukt_io_read_line_unchecked(void) {
    yukt_io_ensure_line();
    char* text = yukt_io_line_text;
    yukt_io_line_text = NULL;
    yukt_io_line_ready = 0;
    if (text == NULL) {
        return yukt_str_from_lit("");
    }
    return text;
}

static void yukt_io_ensure_all(void) {
    if (yukt_io_all_ready) {
        return;
    }
    yukt_io_all_text = yukt_io_read_all(&yukt_io_all_status);
    yukt_io_all_ready = 1;
}

int64_t yukt_io_read_all_status(void) {
    yukt_io_ensure_all();
    return yukt_io_all_status;
}

char* yukt_io_read_all_unchecked(void) {
    yukt_io_ensure_all();
    char* text = yukt_io_all_text;
    yukt_io_all_text = NULL;
    yukt_io_all_ready = 0;
    if (text == NULL) {
        return yukt_str_from_lit("");
    }
    return text;
}

static void yukt_env_require_name(const char* name) {
    if (name == NULL || name[0] == '\0') {
        die("empty environment name");
    }
}

char* yukt_env_get(const char* name) {
    yukt_env_require_name(name);
    const char* value = getenv(name);
    if (value == NULL) {
        return NULL;
    }
    return yukt_str_from_lit(value);
}

char* yukt_env_get_unchecked(const char* name) {
    char* value = yukt_env_get(name);
    if (value == NULL) {
        return yukt_str_from_lit("");
    }
    return value;
}

int8_t yukt_env_has(const char* name) {
    yukt_env_require_name(name);
    return getenv(name) != NULL ? 1 : 0;
}

void yukt_env_set(const char* name, const char* value) {
    yukt_env_require_name(name);
    if (setenv(name, value != NULL ? value : "", 1) != 0) {
        die("out of memory");
    }
}
