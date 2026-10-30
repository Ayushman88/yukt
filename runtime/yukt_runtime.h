#pragma once

/* Size-class payload cache (0.11, frozen). See docs/RUNTIME.md.
 * Six classes: 16, 64, 256, 4096, 65536, 1048576 bytes.
 * Four slots per class; list and string caches are separate.
 * Max retained after yukt_runtime_release: 8,948,352 bytes.
 * Caches are not thread-safe. */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct YuktList {
    int64_t length;
    int64_t kind;
    void* data;
} YuktList;

void yukt_say_i64(int64_t value);
void yukt_say_f64(double value);
void yukt_say_bool(int8_t value);
void yukt_say_cstr(const char* value);
void yukt_say_list(const YuktList* list);

char* yukt_str_concat(const char* left, const char* right);
int64_t yukt_str_len(const char* s);
int64_t yukt_str_at(const char* s, int64_t index);
char* yukt_str_slice(const char* s, int64_t start, int64_t end);
int64_t yukt_str_find(const char* s, const char* sub);
int8_t yukt_str_contains(const char* s, const char* sub);
int8_t yukt_str_starts(const char* s, const char* sub);
int8_t yukt_str_ends(const char* s, const char* sub);
char* yukt_str_repeat(const char* s, int64_t n);
char* yukt_str_join(const YuktList* parts, const char* sep);
YuktList* yukt_str_split(const char* s, const char* sep);

YuktList* yukt_list_copy(const YuktList* src);
void yukt_list_fill_i64(YuktList* dest, int64_t value);
void yukt_list_fill_f64(YuktList* dest, double value);
void yukt_list_fill_bool(YuktList* dest, int8_t value);
void yukt_list_fill_cstr(YuktList* dest, const char* value);
void yukt_list_fill_list(YuktList* dest, YuktList* value);
void yukt_list_reverse(YuktList* dest);
YuktList* yukt_list_slice(const YuktList* src, int64_t start, int64_t end);

void yukt_list_sort_asc(YuktList* items);
void yukt_list_sort_desc(YuktList* items);
YuktList* yukt_list_sorted(const YuktList* items);
int64_t yukt_list_search(const YuktList* items, int64_t ivalue, double fvalue);

int64_t yukt_math_abs_i(int64_t x);
double yukt_math_abs_f(double x);
int64_t yukt_math_min_i(int64_t a, int64_t b);
int64_t yukt_math_max_i(int64_t a, int64_t b);
double yukt_math_min_f(double a, double b);
double yukt_math_max_f(double a, double b);
double yukt_math_floor(double x);
double yukt_math_ceil(double x);
double yukt_math_sqrt(double x);
double yukt_math_pow(double x, double y);
double yukt_math_sin(double x);
double yukt_math_cos(double x);
double yukt_math_tan(double x);
double yukt_math_exp(double x);
double yukt_math_log(double x);

int64_t yukt_stat_min_i(const YuktList* items);
int64_t yukt_stat_max_i(const YuktList* items);
double yukt_stat_min_f(const YuktList* items);
double yukt_stat_max_f(const YuktList* items);
double yukt_stat_mean_f(const YuktList* items);

YuktList* yukt_list_new(int64_t length, int64_t kind) __attribute__((malloc));
YuktList* yukt_list_zeros(int64_t length) __attribute__((malloc));
int64_t yukt_list_len(const YuktList* list);
int64_t yukt_list_get_i64(const YuktList* list, int64_t index);
double yukt_list_get_f64(const YuktList* list, int64_t index);
int8_t yukt_list_get_bool(const YuktList* list, int64_t index);
const char* yukt_list_get_cstr(const YuktList* list, int64_t index);
YuktList* yukt_list_get_list(const YuktList* list, int64_t index);
void yukt_list_set_i64(YuktList* list, int64_t index, int64_t value);
void yukt_list_set_f64(YuktList* list, int64_t index, double value);
void yukt_list_set_bool(YuktList* list, int64_t index, int8_t value);
void yukt_list_set_cstr(YuktList* list, int64_t index, const char* value);
void yukt_list_set_list(YuktList* list, int64_t index, YuktList* value);

typedef void (*yukt_par_fn)(int64_t begin, int64_t end, void* context);
void yukt_parallel_for(int64_t start, int64_t end, yukt_par_fn fn, void* context, int64_t work_per_iter);
void yukt_reduce_i64(int64_t* acc, int64_t partial, int64_t op);
void yukt_reduce_f64(double* acc, double partial, int64_t op);
void yukt_runtime_release(void);

#ifdef __cplusplus
}
#endif
