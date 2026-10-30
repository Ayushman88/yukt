#include "backend/pipeline.hpp"
#include "driver/driver.hpp"
#include "test_harness.hpp"
#include "yukt_runtime.h"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <string>

#ifndef YUKT_SOURCE_DIR
#define YUKT_SOURCE_DIR "."
#endif

namespace {

std::string source_path(const std::string& relative) {
    return (std::filesystem::path(YUKT_SOURCE_DIR) / relative).string();
}

yukt::CompileOutput run_file(const std::string& relative) {
    const auto path = source_path(relative);
    auto frontend = yukt::analyze_file(path);
    yukt::analyze_types(frontend);
    yukt::CompileRequest request;
    request.action = yukt::CompileAction::Run;
    request.input_path = path;
    return yukt::compile_program(frontend, request, *frontend.diagnostics);
}

yukt::FrontendResult load_file(const std::string& relative) {
    auto result = yukt::analyze_file(source_path(relative));
    yukt::analyze_types(result);
    return result;
}

bool has_title(const yukt::FrontendResult& result, const std::string& title) {
    if (result.diagnostics == nullptr) {
        return false;
    }
    for (const auto& diagnostic : result.diagnostics->diagnostics()) {
        if (diagnostic.title == title) {
            return true;
        }
    }
    return false;
}

void require_run(const std::string& relative, const std::string& expected) {
    const auto result = run_file(relative);
    REQUIRE_MSG(result.ok, result.program_output + " (" + relative + ")");
    REQUIRE_MSG(result.exit_code == 0, result.program_output + " (" + relative + ")");
    REQUIRE_MSG(result.program_output == expected, result.program_output + " != " + expected);
}

void require_trap(const std::string& relative) {
    const auto result = run_file(relative);
    REQUIRE_MSG(result.ok, "trap program must compile: " + relative + "\n" + result.program_output);
    REQUIRE_MSG(result.exit_code != 0, "expected non-zero exit for " + relative);
    REQUIRE_MSG(result.program_output.find("YUKT runtime error:") != std::string::npos, result.program_output);
}

}  // namespace

int main() {
    require_run("tests/stdlib/str_basic.yukt",
                "0\n"
                "3\n"
                "97\n"
                "99\n"
                "cde\n"
                "\n"
                "hi\n"
                "2\n"
                "-1\n"
                "0\n"
                "true\n"
                "false\n"
                "true\n"
                "true\n"
                "false\n"
                "true\n"
                "true\n"
                "false\n"
                "ababab\n"
                "\n"
                "\n"
                "\n"
                "only\n"
                "a,b,c\n"
                "ab\n"
                "[a, b, c]\n"
                "1\n"
                "1\n"
                "1\n"
                "[a, b, ]\n"
                "[, a]\n"
                "[a, , b]\n"
                "1\n"
                "0\n"
                "99\n"
                "97\n"
                "102\n"
                "195\n"
                "169\n");

    require_run("tests/stdlib/str_alias.yukt", "2\n1\n");

    require_run("tests/stdlib/list_basic.yukt",
                "0\n"
                "0\n"
                "3\n"
                "[1, 2, 3]\n"
                "[9, 2, 3]\n"
                "[4, 4, 4]\n"
                "[1.5, 2.5, 3.5]\n"
                "[1.5, 9.5, 3.5]\n"
                "[0.5, 0.5, 0.5]\n"
                "[false, false, true]\n"
                "[false, false, false]\n"
                "[c, b, a]\n"
                "[z, z, z]\n"
                "[[1, 2], [3, 4]]\n"
                "[]\n"
                "[20, 30]\n"
                "[10, 20, 30, 40]\n"
                "0\n");

    require_run("tests/stdlib/sort_nan.yukt",
                "true\n"
                "1\n"
                "2\n"
                "false\n"
                "1\n"
                "3\n"
                "false\n"
                "2\n"
                "1\n"
                "true\n");

    require_run("tests/stdlib/sort_basic.yukt",
                "0\n"
                "-1\n"
                "[7]\n"
                "0\n"
                "-1\n"
                "[1, 1, 2, 3, 4]\n"
                "0\n"
                "4\n"
                "-1\n"
                "[3, 2, 1]\n"
                "[9, 1, 8]\n"
                "[1, 8, 9]\n"
                "[1.5, 1.5, 2.5, 3]\n"
                "0\n"
                "-1\n");

    require_run("tests/stdlib/math_basic.yukt",
                "3\n"
                "4\n"
                "2.5\n"
                "-1\n"
                "3\n"
                "-1.5\n"
                "3\n"
                "2\n"
                "-3\n"
                "3\n"
                "-2\n"
                "2\n"
                "8\n"
                "0\n"
                "1\n"
                "0\n"
                "1\n"
                "0\n"
                "false\n"
                "false\n"
                "false\n"
                "true\n"
                "true\n");

    require_run("tests/stdlib/stat_basic.yukt",
                "0\n"
                "0\n"
                "10\n"
                "7\n"
                "1\n"
                "9\n"
                "1.5\n"
                "9\n"
                "3\n"
                "7\n"
                "7\n"
                "7\n"
                "8\n");

    require_run("tests/stdlib/all_modules.yukt",
                "3\n"
                "[1, 2, 3]\n"
                "1\n"
                "6\n"
                "3\n");

    require_trap("tests/stdlib/str_trap_at.yukt");
    require_trap("tests/stdlib/str_trap_slice.yukt");
    require_trap("tests/stdlib/str_trap_repeat.yukt");
    require_trap("tests/stdlib/str_trap_split.yukt");
    require_trap("tests/stdlib/list_trap_slice.yukt");
    require_trap("tests/stdlib/stat_trap_empty.yukt");
    require_trap("tests/stdlib/stat_trap_mean.yukt");

    {
        auto result = load_file("tests/stdlib/type_sort_strings.yukt");
        REQUIRE(result.diagnostics->has_errors());
        REQUIRE(has_title(result, "Type mismatch"));
    }
    {
        auto result = load_file("tests/stdlib/type_math_mix.yukt");
        REQUIRE(result.diagnostics->has_errors());
        REQUIRE(has_title(result, "Type mismatch"));
    }
    {
        auto result = load_file("tests/stdlib/type_join_ints.yukt");
        REQUIRE(result.diagnostics->has_errors());
        REQUIRE(has_title(result, "Type mismatch"));
    }

    {
        REQUIRE(yukt_str_len("") == 0);
        REQUIRE(yukt_str_len(nullptr) == 0);
        REQUIRE(yukt_str_find("abc", "") == 0);
        REQUIRE(yukt_str_contains("abc", "") == 1);
        REQUIRE(yukt_str_starts("", "") == 1);
        REQUIRE(yukt_str_ends("", "") == 1);
        char* joined = yukt_str_join(nullptr, ",");
        REQUIRE(joined != nullptr);
        REQUIRE(std::strcmp(joined, "") == 0);
        YuktList* parts = yukt_str_split("a,b,", ",");
        REQUIRE(parts != nullptr);
        REQUIRE(parts->length == 3);
        REQUIRE(parts->kind == 3);
        REQUIRE(std::strcmp(yukt_list_get_cstr(parts, 2), "") == 0);
        YuktList* nums = yukt_list_zeros(4);
        yukt_list_set_i64(nums, 0, 3);
        yukt_list_set_i64(nums, 1, 1);
        yukt_list_set_i64(nums, 2, 2);
        yukt_list_set_i64(nums, 3, 1);
        yukt_list_sort_asc(nums);
        REQUIRE(yukt_list_get_i64(nums, 0) == 1);
        REQUIRE(yukt_list_search(nums, 1, 0.0) == 0);
        REQUIRE(yukt_list_search(nums, 4, 0.0) == -1);
        YuktList* reverse = yukt_list_zeros(256);
        for (int64_t i = 0; i < 256; ++i) {
            yukt_list_set_i64(reverse, i, 255 - i);
        }
        yukt_list_sort_asc(reverse);
        REQUIRE(yukt_list_get_i64(reverse, 0) == 0);
        REQUIRE(yukt_list_get_i64(reverse, 255) == 255);
        for (int64_t i = 1; i < 256; ++i) {
            REQUIRE(yukt_list_get_i64(reverse, i - 1) <= yukt_list_get_i64(reverse, i));
        }
        YuktList* floats = yukt_list_new(4, 1);
        yukt_list_set_f64(floats, 0, 2.0);
        yukt_list_set_f64(floats, 1, yukt_math_sqrt(-1.0));
        yukt_list_set_f64(floats, 2, yukt_math_log(0.0));
        yukt_list_set_f64(floats, 3, 1.0);
        yukt_list_sort_asc(floats);
        REQUIRE(std::isinf(yukt_list_get_f64(floats, 0)) && yukt_list_get_f64(floats, 0) < 0.0);
        REQUIRE(yukt_list_get_f64(floats, 1) == 1.0);
        REQUIRE(yukt_list_get_f64(floats, 2) == 2.0);
        REQUIRE(std::isnan(yukt_list_get_f64(floats, 3)));
        REQUIRE(yukt_list_search(floats, 0, yukt_math_sqrt(-1.0)) == 3);
        YuktList* copy = yukt_list_copy(nums);
        REQUIRE(copy != nullptr);
        REQUIRE(copy->kind == 0);
        REQUIRE(copy->length == 4);
        yukt_list_fill_i64(copy, 9);
        REQUIRE(yukt_list_get_i64(nums, 0) == 1);
        REQUIRE(yukt_list_get_i64(copy, 0) == 9);
        REQUIRE(yukt_math_min_i(3, -2) == -2);
        REQUIRE(std::isnan(yukt_math_min_f(1.0, yukt_math_sqrt(-1.0))));
        REQUIRE(std::isnan(yukt_math_sqrt(-1.0)));
        REQUIRE(yukt_math_abs_i(-8) == 8);
        yukt_runtime_release();
        YuktList* reused = yukt_list_zeros(4);
        REQUIRE(reused != nullptr);
        REQUIRE(yukt_list_get_i64(reused, 0) == 0);
        yukt_runtime_release();
    }

    {
        const auto path = source_path("tests/stdlib/all_modules.yukt");
        auto frontend = yukt::analyze_file(path);
        yukt::analyze_types(frontend);
        yukt::CompileRequest request;
        request.action = yukt::CompileAction::Build;
        request.input_path = path;
        const auto out_path =
            (std::filesystem::temp_directory_path() / "yukt-stdlib-all-modules").string();
        request.output_path = out_path;
        const auto result = yukt::compile_program(frontend, request, *frontend.diagnostics);
        REQUIRE_MSG(result.ok, "native build of all_modules.yukt failed");
        REQUIRE(std::filesystem::exists(out_path));
        REQUIRE(std::filesystem::status(out_path).permissions() != std::filesystem::perms::none);
        std::error_code ec;
        std::filesystem::remove(out_path, ec);
    }

    return yukt::test::summary("stdlib");
}
