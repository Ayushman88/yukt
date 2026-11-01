#pragma once

/* Size-class payload cache (0.11, frozen). See docs/RUNTIME.md.
 * Six classes: 16, 64, 256, 4096, 65536, 1048576 bytes.
 * Four slots per class; list and string caches are separate.
 * Max retained after yukt_runtime_release: 8,948,352 bytes.
 * Tracker, bins, and 0.13 object alloc/free take one recursive heap mutex. */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct YuktList {
    int64_t length;
    int64_t kind;
    void* data;
    int64_t stride;
    int64_t capacity;
    int64_t generation;
} YuktList;

void yukt_say_i64(int64_t value);
void yukt_say_f64(double value);
void yukt_say_bool(int8_t value);
void yukt_say_cstr(const char* value);
void yukt_say_list(const YuktList* list);
void yukt_print_i64(int64_t value);
void yukt_print_f64(double value);
void yukt_print_bool(int8_t value);
void yukt_print_cstr(const char* value);

YuktList* yukt_list_new_rec(int64_t length, int64_t stride) __attribute__((malloc));
void yukt_list_set_rec(YuktList* list, int64_t index, const void* src, int64_t stride);
void yukt_list_get_rec(const YuktList* list, int64_t index, void* dest, int64_t stride);

char* yukt_str_concat(const char* left, const char* right);
/* Content equality. NULL and "" compare equal. */
int8_t yukt_str_eq(const char* left, const char* right);
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
void yukt_list_check_slice(const YuktList* src, int64_t start, int64_t end);
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
void yukt_list_append_i64(YuktList* list, int64_t value);
void yukt_list_append_f64(YuktList* list, double value);
void yukt_list_append_bool(YuktList* list, int8_t value);
void yukt_list_append_cstr(YuktList* list, const char* value);
void yukt_list_append_list(YuktList* list, YuktList* value);
void yukt_list_append_rec(YuktList* list, const void* src, int64_t stride);
int64_t yukt_list_remove_i64(YuktList* list, int64_t index);
double yukt_list_remove_f64(YuktList* list, int64_t index);
int8_t yukt_list_remove_bool(YuktList* list, int64_t index);
const char* yukt_list_remove_cstr(YuktList* list, int64_t index);
YuktList* yukt_list_remove_list(YuktList* list, int64_t index);
void yukt_list_remove_rec(YuktList* list, int64_t index, void* dest, int64_t stride);
int64_t yukt_list_pop_i64(YuktList* list);
double yukt_list_pop_f64(YuktList* list);
int8_t yukt_list_pop_bool(YuktList* list);
const char* yukt_list_pop_cstr(YuktList* list);
YuktList* yukt_list_pop_list(YuktList* list);
void yukt_list_pop_rec(YuktList* list, void* dest, int64_t stride);
int64_t yukt_list_generation(const YuktList* list);
void yukt_list_check_generation(const YuktList* list, int64_t expected);

typedef void (*yukt_par_fn)(int64_t begin, int64_t end, void* context);
void yukt_parallel_for(int64_t start, int64_t end, yukt_par_fn fn, void* context, int64_t work_per_iter);
void yukt_reduce_i64(int64_t* acc, int64_t partial, int64_t op);
void yukt_reduce_f64(double* acc, double partial, int64_t op);
void yukt_runtime_release(void);

/* Unregister and free (or cache) one tracked allocation. NULL is a no-op.
 * Aborts on double release or an untracked pointer. */
void yukt_release(void* ptr);
/* Fresh tracked, owned copy of a NUL-terminated string (NULL becomes ""). */
char* yukt_str_from_lit(const char* s);
/* Fresh tracked copy of a string slot. NULL stays NULL. */
char* yukt_str_clone(const char* s);
/* Number of currently tracked allocations (for ownership tests). */
int64_t yukt_debug_live_allocs(void);
/* Next list payload allocation returns NULL instead of taking cache or malloc. */
void yukt_debug_fail_next_payload_alloc(void);
/* Optional die interceptor for tests. The hook is cleared before it is called.
 * If the hook returns, the process still exits 1. */
void yukt_debug_set_die_hook(void (*fn)(const char* message));

/* 0.13.4 ABI helper: sum of a two-integer record `{ x, y }`. */
int64_t yukt_point_sum(const int64_t* point);
/* 0.13.4 ABI helper: C `int8_t` boolean and. */
int8_t yukt_bool_and(int8_t a, int8_t b);

void yukt_track_obj(void* ptr, size_t bytes);
void yukt_013_free(void* ptr);
void yukt_heap_lock(void);
void yukt_heap_unlock(void);

typedef void (*yukt_task_fn)(void* args, void* result);
void* yukt_task_start(yukt_task_fn fn, void* args, size_t result_bytes, size_t result_align);
void yukt_task_wait(void* task, void* result_out);
void yukt_task_worker_try(void* task);
void yukt_task_shutdown(void);

void* yukt_map_new(void);
void yukt_map_set_i64(void* map, const char* key, int64_t value);
void yukt_map_set_f64(void* map, const char* key, double value);
void yukt_map_set_bool(void* map, const char* key, int8_t value);
void yukt_map_set_cstr(void* map, const char* key, const char* value);
int8_t yukt_map_has(void* map, const char* key);
int64_t yukt_map_get_i64(void* map, const char* key);
double yukt_map_get_f64(void* map, const char* key);
int8_t yukt_map_get_bool(void* map, const char* key);
char* yukt_map_get_cstr(void* map, const char* key);
int64_t yukt_map_len(void* map);
void yukt_map_remove(void* map, const char* key);

void* yukt_set_new(void);
void yukt_set_add(void* set, const char* key);
int8_t yukt_set_has(void* set, const char* key);
int64_t yukt_set_len(void* set);
void yukt_set_remove(void* set, const char* key);

int8_t yukt_file_exists(const char* path);
char* yukt_file_read(const char* path);
int64_t yukt_file_write(const char* path, const char* data);

void* yukt_json_parse(const char* text);
int8_t yukt_json_is_valid(void* json);
int8_t yukt_json_has_int(void* json, const char* key);
int8_t yukt_json_has_string(void* json, const char* key);
int64_t yukt_json_get_int(void* json, const char* key);
char* yukt_json_get_string(void* json, const char* key);
char* yukt_json_encode(void* json);

#ifdef __cplusplus
}
#endif
