#include "yukt_runtime.h"

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail = 0;
#define REQUIRE(c)                                                                                                     \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c);                                                \
            fail = 1;                                                                                                  \
        }                                                                                                              \
    } while (0)

static jmp_buf oom_jmp;
static const char* oom_message = NULL;

static void on_die(const char* message) {
    oom_message = message;
    longjmp(oom_jmp, 1);
}

int main(void) {
    YuktList* list = yukt_list_new(2, 0);
    yukt_list_set_i64(list, 0, 10);
    yukt_list_set_i64(list, 1, 20);
    void* old_data = list->data;
    const int64_t old_length = list->length;
    const int64_t old_capacity = list->capacity;
    const int64_t old_generation = list->generation;
    const int64_t old_kind = list->kind;
    const int64_t old_stride = list->stride;
    const int64_t live_before = yukt_debug_live_allocs();

    yukt_debug_set_die_hook(on_die);
    yukt_debug_fail_next_payload_alloc();
    if (setjmp(oom_jmp) == 0) {
        yukt_list_append_i64(list, 99);
        fprintf(stderr, "FAIL append did not trap on injected allocation failure\n");
        return 1;
    }

    REQUIRE(oom_message != NULL);
    REQUIRE(strcmp(oom_message, "out of memory") == 0);
    REQUIRE(list->length == old_length);
    REQUIRE(list->capacity == old_capacity);
    REQUIRE(list->generation == old_generation);
    REQUIRE(list->kind == old_kind);
    REQUIRE(list->stride == old_stride);
    REQUIRE(list->data == old_data);
    REQUIRE(yukt_list_get_i64(list, 0) == 10);
    REQUIRE(yukt_list_get_i64(list, 1) == 20);
    REQUIRE(yukt_debug_live_allocs() == live_before);

    yukt_list_append_i64(list, 99);
    REQUIRE(list->length == 3);
    REQUIRE(yukt_list_get_i64(list, 0) == 10);
    REQUIRE(yukt_list_get_i64(list, 1) == 20);
    REQUIRE(yukt_list_get_i64(list, 2) == 99);
    yukt_release(list);
    REQUIRE(yukt_debug_live_allocs() == 0);
    return fail ? 1 : 0;
}
