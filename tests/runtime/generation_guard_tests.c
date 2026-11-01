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

static jmp_buf gen_jmp;
static const char* gen_message = NULL;

static void on_die(const char* message) {
    gen_message = message;
    longjmp(gen_jmp, 1);
}

int main(int argc, char** argv) {
    if (argc > 1 && strcmp(argv[1], "generation-guard") == 0) {
        YuktList* list = yukt_list_zeros(2);
        const int64_t snapshot = yukt_list_generation(list);
        yukt_list_append_i64(list, 7);
        yukt_list_check_generation(list, snapshot);
        fprintf(stderr, "FAIL generation guard did not abort\n");
        return 0;
    }

    YuktList* list = yukt_list_zeros(2);
    yukt_list_set_i64(list, 0, 1);
    yukt_list_set_i64(list, 1, 2);
    const int64_t snapshot = yukt_list_generation(list);
    yukt_list_check_generation(list, snapshot);
    REQUIRE(yukt_list_get_i64(list, 0) == 1);
    REQUIRE(list->length == 2);

    yukt_list_append_i64(list, 3);
    yukt_debug_set_die_hook(on_die);
    if (setjmp(gen_jmp) == 0) {
        yukt_list_check_generation(list, snapshot);
        fprintf(stderr, "FAIL generation guard did not trap after append\n");
        return 1;
    }

    REQUIRE(gen_message != NULL);
    REQUIRE(strcmp(gen_message, "list changed during iteration") == 0);
    REQUIRE(list->length == 3);
    REQUIRE(yukt_list_get_i64(list, 0) == 1);
    REQUIRE(yukt_list_get_i64(list, 1) == 2);
    REQUIRE(yukt_list_get_i64(list, 2) == 3);
    REQUIRE(yukt_list_generation(list) != snapshot);
    yukt_list_check_generation(list, yukt_list_generation(list));

    yukt_release(list);
    REQUIRE(yukt_debug_live_allocs() == 0);
    return fail ? 1 : 0;
}
