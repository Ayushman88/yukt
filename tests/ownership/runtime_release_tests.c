#include "yukt_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail = 0;
#define REQUIRE(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fail = 1; } } while (0)

int main(int argc, char** argv) {
    if (argc > 1 && strcmp(argv[1], "double-release") == 0) {
        /* Expected to exit 1 with "double release or untracked pointer". */
        YuktList* a = yukt_list_zeros(4);
        yukt_release(a);
        yukt_release(a);
        fprintf(stderr, "FAIL second release did not abort\n");
        return 0;
    }

    REQUIRE(yukt_debug_live_allocs() == 0);
    YuktList* a = yukt_list_zeros(4);
    REQUIRE(yukt_debug_live_allocs() == 1);
    yukt_release(a);
    REQUIRE(yukt_debug_live_allocs() == 0);
    char* s = yukt_str_from_lit("hi");
    REQUIRE(yukt_debug_live_allocs() == 1);
    REQUIRE(strcmp(s, "hi") == 0);
    yukt_release(s);
    REQUIRE(yukt_debug_live_allocs() == 0);
    yukt_release(NULL);
    REQUIRE(yukt_debug_live_allocs() == 0);

    /* Kind-3 copy clones string slots; releasing the original keeps the copy. */
    {
        YuktList* words = yukt_list_new(2, 3);
        yukt_list_set_cstr(words, 0, yukt_str_from_lit("a"));
        yukt_list_set_cstr(words, 1, yukt_str_from_lit("b"));
        REQUIRE(yukt_debug_live_allocs() == 3);
        YuktList* copy = yukt_list_copy(words);
        REQUIRE(copy != NULL);
        REQUIRE(copy->kind == 3);
        REQUIRE(copy->length == 2);
        REQUIRE(yukt_list_get_cstr(copy, 0) != yukt_list_get_cstr(words, 0));
        REQUIRE(strcmp(yukt_list_get_cstr(copy, 0), "a") == 0);
        REQUIRE(yukt_debug_live_allocs() == 6);
        yukt_list_set_cstr(words, 0, yukt_str_from_lit("c"));
        REQUIRE(strcmp(yukt_list_get_cstr(copy, 0), "a") == 0);
        REQUIRE(strcmp(yukt_list_get_cstr(words, 0), "c") == 0);
        yukt_release(words);
        REQUIRE(yukt_debug_live_allocs() == 3);
        REQUIRE(strcmp(yukt_list_get_cstr(copy, 0), "a") == 0);
        yukt_release(copy);
        REQUIRE(yukt_debug_live_allocs() == 0);
    }

    /* Kind-4 copy clones nested lists. */
    {
        YuktList* inner = yukt_list_zeros(2);
        yukt_list_set_i64(inner, 0, 1);
        yukt_list_set_i64(inner, 1, 2);
        YuktList* nested = yukt_list_new(1, 4);
        yukt_list_set_list(nested, 0, inner);
        YuktList* copy = yukt_list_copy(nested);
        REQUIRE(yukt_list_get_list(copy, 0) != inner);
        REQUIRE(yukt_list_get_i64(yukt_list_get_list(copy, 0), 0) == 1);
        yukt_list_set_i64(inner, 0, 9);
        REQUIRE(yukt_list_get_i64(yukt_list_get_list(copy, 0), 0) == 1);
        yukt_release(nested);
        REQUIRE(yukt_list_get_i64(yukt_list_get_list(copy, 0), 0) == 1);
        yukt_release(copy);
        REQUIRE(yukt_debug_live_allocs() == 0);
    }

    /* list.fill of strings clones N times so a later slot overwrite is independent. */
    {
        YuktList* words = yukt_list_new(2, 3);
        yukt_list_set_cstr(words, 0, yukt_str_from_lit("a"));
        yukt_list_set_cstr(words, 1, yukt_str_from_lit("b"));
        yukt_list_fill_cstr(words, "z");
        REQUIRE(yukt_list_get_cstr(words, 0) != yukt_list_get_cstr(words, 1));
        REQUIRE(strcmp(yukt_list_get_cstr(words, 0), "z") == 0);
        yukt_list_set_cstr(words, 0, yukt_str_from_lit("x"));
        REQUIRE(strcmp(yukt_list_get_cstr(words, 1), "z") == 0);
        yukt_release(words);
        REQUIRE(yukt_debug_live_allocs() == 0);
    }

    yukt_runtime_release();
    REQUIRE(yukt_debug_live_allocs() == 0);
    return fail ? 1 : 0;
}
