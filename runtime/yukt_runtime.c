#include "yukt_runtime.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__)
#include <dispatch/dispatch.h>
#include <unistd.h>
#else
#include <pthread.h>
#include <unistd.h>
#endif

static void die(const char* message) {
    fprintf(stderr, "YUKT runtime error: %s\n", message);
    exit(1);
}

enum { YUKT_ALLOC_LIST = 1, YUKT_ALLOC_STR = 2 };

typedef struct YuktAlloc {
    struct YuktAlloc* next;
    int kind;
    void* ptr;
    size_t bytes;
} YuktAlloc;

static YuktAlloc* yukt_allocs = NULL;
static void* yukt_cached_data = NULL;
static size_t yukt_cached_bytes = 0;

static void yukt_track(int kind, void* ptr, size_t bytes) {
    if (ptr == NULL) {
        return;
    }
    YuktAlloc* node = (YuktAlloc*)malloc(sizeof(YuktAlloc));
    if (node == NULL) {
        die("out of memory");
    }
    node->next = yukt_allocs;
    node->kind = kind;
    node->ptr = ptr;
    node->bytes = bytes;
    yukt_allocs = node;
}

void yukt_runtime_release(void) {
    YuktAlloc* node = yukt_allocs;
    yukt_allocs = NULL;
    while (node != NULL) {
        YuktAlloc* next = node->next;
        if (node->kind == YUKT_ALLOC_LIST) {
            YuktList* list = (YuktList*)node->ptr;
            if (list->data != NULL && node->bytes > yukt_cached_bytes) {
                free(yukt_cached_data);
                yukt_cached_data = list->data;
                yukt_cached_bytes = node->bytes;
                list->data = NULL;
            }
            free(list->data);
            free(list);
        } else {
            free(node->ptr);
        }
        free(node);
        node = next;
    }
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

void yukt_say_i64(int64_t value) {
    printf("%" PRId64 "\n", value);
}

void yukt_say_f64(double value) {
    printf("%g\n", value);
}

void yukt_say_bool(int8_t value) {
    printf("%s\n", value ? "true" : "false");
}

void yukt_say_cstr(const char* value) {
    printf("%s\n", value != NULL ? value : "");
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
    char* out = (char*)malloc(left_size + right_size + 1);
    if (out == NULL) {
        die("out of memory");
    }
    memcpy(out, left, left_size);
    memcpy(out + left_size, right, right_size);
    out[left_size + right_size] = '\0';
    yukt_track(YUKT_ALLOC_STR, out, left_size + right_size + 1);
    return out;
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

static void* yukt_alloc_aligned_zero(size_t bytes) {
    if (bytes == 0) {
        return NULL;
    }
    const size_t align = 16;
    const size_t padded = (bytes + align - 1) & ~(align - 1);
    if (yukt_cached_data != NULL && yukt_cached_bytes >= padded) {
        void* data = yukt_cached_data;
        yukt_cached_data = NULL;
        yukt_cached_bytes = 0;
        memset(data, 0, padded);
        return data;
    }
    void* data = aligned_alloc(align, padded);
    if (data == NULL) {
        die("out of memory");
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
    size_t payload = 0;
    if (length > 0) {
        payload = (size_t)length * element_size(kind);
        list->data = yukt_alloc_aligned_zero(payload);
    }
    yukt_track(YUKT_ALLOC_LIST, list, payload);
    return list;
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
    ((const char**)list->data)[index] = value;
}

void yukt_list_set_list(YuktList* list, int64_t index, YuktList* value) {
    if (list == NULL || index < 0 || index >= list->length) {
        die("list index out of range");
    }
    ((YuktList**)list->data)[index] = value;
}

YuktList* yukt_list_zeros(int64_t length) {
    return yukt_list_new(length, 0);
}

int64_t yukt_list_len(const YuktList* list) {
    return list == NULL ? 0 : list->length;
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
