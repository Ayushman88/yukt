#pragma once

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
void yukt_runtime_release(void);

#ifdef __cplusplus
}
#endif
