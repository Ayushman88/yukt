#include "backend/pipeline.hpp"
#include "driver/driver.hpp"
#include "test_harness.hpp"
#include "yukt_runtime.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <locale.h>
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

void require_trap_message(const std::string& relative, const std::string& message) {
    const auto result = run_file(relative);
    REQUIRE_MSG(result.ok, "trap program must compile: " + relative + "\n" + result.program_output);
    REQUIRE_MSG(result.exit_code != 0, "expected non-zero exit for " + relative);
    REQUIRE_MSG(result.program_output.find("YUKT runtime error:") != std::string::npos, result.program_output);
    REQUIRE_MSG(result.program_output.find(message) != std::string::npos, result.program_output);
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

    require_run("tests/stdlib/str_trim_replace.yukt",
                "hi\n"
                "bb\n"
                "hi  \n"
                "  hi\n"
                "hello  world\n"
                "\n"
                "\n"
                "abc\n"
                "ABC\n"
                "straße\n"
                "bb\n"
                "ba\n"
                "x\n"
                "\n"
                "stay\n"
                "  stay  \n");
    require_run("tests/stdlib/str_parse.yukt",
                "42\n"
                "bad_int\n"
                "42\n"
                "7\n"
                "42\n"
                "0\n"
                "bad_int\n"
                "bad_int\n"
                "bad_int\n"
                "bad_int\n"
                "bad_int\n"
                "bad_int\n"
                "9223372036854775807\n"
                "bad_int\n"
                "-9223372036854775808\n"
                "bad_int\n"
                "1.5\n"
                "0.001\n"
                "0.25\n"
                "1\n"
                "0.5\n"
                "bad_float\n"
                "bad_float\n"
                "bad_float\n"
                "bad_float\n"
                "bad_float\n"
                "bad_float\n"
                "bad_float\n"
                "bad_float\n");

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
    require_trap_message("tests/stdlib/str_trap_replace.yukt", "empty replace pattern");
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
        auto result = load_file("tests/stdlib/type_unused_to_int.yukt");
        REQUIRE(result.diagnostics->has_errors());
        REQUIRE(has_title(result, "Unused result"));
    }

    {
        REQUIRE(yukt_str_len("") == 0);
        REQUIRE(yukt_str_len(nullptr) == 0);
        char* trimmed = yukt_str_trim(" \t\n\v\f\rhi \t\n\v\f\r");
        REQUIRE(std::strcmp(trimmed, "hi") == 0);
        char* interior = yukt_str_trim("  hello  world  ");
        REQUIRE(std::strcmp(interior, "hello  world") == 0);
        char* empty_ws = yukt_str_trim(" \t\n\v\f\r");
        REQUIRE(std::strcmp(empty_ws, "") == 0);
        const char a0[] = {'\xA0', 'h', 'i', '\xA0', 0};
        char* not_nbsp = yukt_str_trim(a0);
        REQUIRE(std::strcmp(not_nbsp, a0) == 0);
        const char utf8_nbsp[] = "\xC2\xA0hi\xC2\xA0";
        char* keep_nbsp = yukt_str_trim(utf8_nbsp);
        REQUIRE(std::strcmp(keep_nbsp, utf8_nbsp) == 0);
        const char* identity_in = "x";
        char* identity = yukt_str_trim(identity_in);
        REQUIRE(std::strcmp(identity, "x") == 0);
        REQUIRE(identity != identity_in);
        const char raw[] = {'A', '\x80', 'a', '\xFF', 0};
        char* lowered = yukt_str_lower(raw);
        REQUIRE((unsigned char)lowered[0] == 'a');
        REQUIRE((unsigned char)lowered[1] == 0x80);
        REQUIRE((unsigned char)lowered[2] == 'a');
        REQUIRE((unsigned char)lowered[3] == 0xFF);
        char* raised = yukt_str_upper(raw);
        REQUIRE((unsigned char)raised[0] == 'A');
        REQUIRE((unsigned char)raised[1] == 0x80);
        REQUIRE((unsigned char)raised[2] == 'A');
        REQUIRE((unsigned char)raised[3] == 0xFF);
        char* street = yukt_str_lower("Straße");
        REQUIRE(std::strcmp(street, "straße") == 0);
        char* replaced = yukt_str_replace("aaa", "aa", "b");
        REQUIRE(std::strcmp(replaced, "ba") == 0);
        char* deleted = yukt_str_replace("abab", "a", "");
        REQUIRE(std::strcmp(deleted, "bb") == 0);
        char* copied = yukt_str_replace("x", "y", "z");
        REQUIRE(std::strcmp(copied, "x") == 0);
        int64_t parse_ok = 0;
        REQUIRE(yukt_str_to_int("  +42  ", &parse_ok) == 42);
        REQUIRE(parse_ok == 1);
        REQUIRE(yukt_str_to_int("-9223372036854775808", &parse_ok) == INT64_MIN);
        REQUIRE(parse_ok == 1);
        REQUIRE(yukt_str_to_int("9223372036854775808", &parse_ok) == 0);
        REQUIRE(parse_ok == 0);
        REQUIRE(yukt_str_to_int("-9223372036854775809", &parse_ok) == 0);
        REQUIRE(parse_ok == 0);
        REQUIRE(yukt_str_to_float("  1.5  ", &parse_ok) == 1.5);
        REQUIRE(parse_ok == 1);
        REQUIRE(yukt_str_to_float("1e-3", &parse_ok) == 0.001);
        REQUIRE(parse_ok == 1);
        REQUIRE(yukt_str_to_float("0x1p-2", &parse_ok) == 0.25);
        REQUIRE(parse_ok == 1);
        REQUIRE(yukt_str_to_float("1.5xyz", &parse_ok) == 0.0);
        REQUIRE(parse_ok == 0);
        REQUIRE(yukt_str_to_float("nan", &parse_ok) == 0.0);
        REQUIRE(parse_ok == 0);
        REQUIRE(yukt_str_to_float("inf", &parse_ok) == 0.0);
        REQUIRE(parse_ok == 0);
        REQUIRE(yukt_str_to_float("+inf", &parse_ok) == 0.0);
        REQUIRE(parse_ok == 0);
        REQUIRE(yukt_str_to_float("-infinity", &parse_ok) == 0.0);
        REQUIRE(parse_ok == 0);
        REQUIRE(yukt_str_to_float("1e1000", &parse_ok) == 0.0);
        REQUIRE(parse_ok == 0);
        if (setlocale(LC_NUMERIC, "de_DE.UTF-8") != nullptr || setlocale(LC_NUMERIC, "de_DE") != nullptr) {
            REQUIRE(yukt_str_to_float("1.5", &parse_ok) == 1.5);
            REQUIRE(parse_ok == 1);
            REQUIRE(yukt_str_to_float("1,5", &parse_ok) == 0.0);
            REQUIRE(parse_ok == 0);
            setlocale(LC_NUMERIC, "C");
        }
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
        YuktList* grown = yukt_list_new(0, 0);
        REQUIRE(grown != nullptr);
        REQUIRE(grown->length == 0);
        REQUIRE(grown->capacity == 0);
        for (int64_t i = 0; i < 20; ++i) {
            yukt_list_append_i64(grown, i + 1);
        }
        REQUIRE(grown->length == 20);
        REQUIRE(grown->capacity >= 20);
        REQUIRE(yukt_list_get_i64(grown, 0) == 1);
        REQUIRE(yukt_list_get_i64(grown, 19) == 20);
        REQUIRE(yukt_list_remove_i64(grown, 1) == 2);
        REQUIRE(grown->length == 19);
        REQUIRE(yukt_list_get_i64(grown, 1) == 3);
        REQUIRE(yukt_list_pop_i64(grown) == 20);
        REQUIRE(grown->length == 18);
        const int64_t gen = yukt_list_generation(grown);
        yukt_list_append_i64(grown, 99);
        REQUIRE(yukt_list_generation(grown) != gen);
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

    require_run("tests/stdlib/map_set.yukt",
                "1\n"
                "not_found\n"
                "2\n"
                "true\n"
                "false\n"
                "1\n"
                "true\n"
                "false\n");
    require_run("tests/stdlib/file_json.yukt",
                "5\n"
                "hello\n"
                "not_found\n"
                "9\n"
                "hi\n"
                "{\"n\":9,\"s\":\"hi\"}\n"
                "invalid_json\n");
    require_run("tests/lists/append_from_empty.yukt", "2\n10\n[10, 20]\n");
    require_run("tests/lists/append_after_zeros.yukt", "[0, 0, 7]\n3\n");
    require_run("tests/lists/append_after_literal.yukt", "[1, 2, 3]\n");
    require_run("tests/lists/remove_middle.yukt", "20\n[10, 30]\n");
    require_run("tests/lists/remove_ends.yukt", "10\n[20, 30]\n30\n[20]\n");
    require_run("tests/lists/pop_order.yukt", "b\n[a]\na\n0\n");
    require_run("tests/lists/drain_counted.yukt", "3\n2\n1\n0\n");
    require_run("tests/lists/count_not_capacity.yukt", "20\n1\n20\n");
    require_run("tests/lists/remove_preserves_order.yukt", "20\n[10, 30, 40]\n");
    require_run("tests/lists/iter_empty.yukt", "1\n0\n");
    require_run("tests/lists/iter_slot_write.yukt", "[2, 3, 4]\n");
    require_run("tests/lists/drain_not_iteration_subject.yukt", "5\n4\n0\n");
    require_run("tests/lists/empty_infer_append.yukt", "[10]\n");
    require_run("tests/lists/append_types.yukt", "[true, false]\n[1.5, 2.5]\n[false]\n");
    require_run("tests/ownership/pop_steals_string.yukt", "b\n[a]\n");
    require_run("tests/ownership/append_profile.yukt", "ann\n3\n0\n");
    require_run("tests/ownership/append_nested_list.yukt", "[[1, 2]]\n2\n");
    require_run("tests/ownership/remove_does_not_drop_neighbors.yukt", "b\n[a, c]\n");
    require_run("tests/ownership/realloc_does_not_drop.yukt", "9\na\ni\n");
    require_run("tests/ownership/ref_append.yukt", "[1, 2]\n");
    require_run("tests/lists/append_stays_sequential.yukt", "200\n20100\n");
    require_trap_message("tests/lists/pop_empty_trap.yukt", "Cannot pop from an empty list");
    require_trap_message("tests/lists/remove_oob_trap.yukt", "list index out of range");
    require_trap_message("tests/lists/remove_oob_empty.yukt", "list index out of range");

    {
        const auto rejected = load_file("tests/lists/iter_append_rejected.yukt");
        REQUIRE(has_title(rejected, "Cannot resize list during iteration"));
        const auto pop_rejected = load_file("tests/lists/iter_pop_rejected.yukt");
        REQUIRE(has_title(pop_rejected, "Cannot resize list during iteration"));
        const auto grow_rejected = load_file("tests/lists/iter_ref_grow_rejected.yukt");
        REQUIRE(has_title(grow_rejected, "Cannot resize list during iteration"));
        const auto alias_rejected = load_file("tests/lists/iter_header_alias_ref.yukt");
        REQUIRE(has_title(alias_rejected, "Cannot resize list during iteration"));
        const auto mixed = load_file("tests/lists/empty_mixed_append.yukt");
        REQUIRE(has_title(mixed, "Type mismatch"));
        const auto zeros_string = load_file("tests/lists/append_zeros_string.yukt");
        REQUIRE(has_title(zeros_string, "Type mismatch"));
        const auto temp = load_file("tests/lists/append_temp.yukt");
        REQUIRE(has_title(temp, "Cannot borrow a temporary"));
    }

    {
        auto frontend = yukt::analyze_file(source_path("tests/lists/append_stays_sequential.yukt"));
        yukt::analyze_types(frontend);
        yukt::CompileRequest request;
        request.action = yukt::CompileAction::Explain;
        request.input_path = source_path("tests/lists/append_stays_sequential.yukt");
        const auto result = yukt::compile_program(frontend, request, *frontend.diagnostics);
        REQUIRE_MSG(result.ok, result.explanation);
        REQUIRE(result.explanation.find("planned: sequential") != std::string::npos);
        REQUIRE(result.llvm_ir.find("yukt_parallel_for") == std::string::npos);
    }

    {
        auto frontend = yukt::analyze_file(source_path("tests/stdlib/str_trim_stays_sequential.yukt"));
        yukt::analyze_types(frontend);
        yukt::CompileRequest request;
        request.action = yukt::CompileAction::Explain;
        request.input_path = source_path("tests/stdlib/str_trim_stays_sequential.yukt");
        const auto result = yukt::compile_program(frontend, request, *frontend.diagnostics);
        REQUIRE_MSG(result.ok, result.explanation);
        REQUIRE(result.explanation.find("planned: sequential") != std::string::npos);
        REQUIRE(result.llvm_ir.find("yukt_parallel_for") == std::string::npos);
    }
    require_run("tests/stdlib/str_trim_stays_sequential.yukt", "8\n");

    require_run("tests/suite013/all.yukt",
                "2\n"
                "7\n"
                "4\n"
                "7\n"
                "50005000\n"
                "1\n"
                "1\n"
                "5\n"
                "42\n"
                "not_found\n"
                "bad_int\n");

    return yukt::test::summary("stdlib");
}
