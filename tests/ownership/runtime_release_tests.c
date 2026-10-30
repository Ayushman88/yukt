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
    yukt_runtime_release();
    REQUIRE(yukt_debug_live_allocs() == 0);
    return fail ? 1 : 0;
}
