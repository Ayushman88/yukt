#include "backend/pipeline.hpp"
#include "driver/driver.hpp"
#include "test_harness.hpp"
#include "yukt_runtime.h"

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

yukt::CompileOutput run_source(const std::string& source) {
    auto frontend = yukt::analyze_source("<test>", source);
    yukt::analyze_types(frontend);
    yukt::CompileRequest request;
    request.action = yukt::CompileAction::Run;
    request.input_path = "<test>";
    return yukt::compile_program(frontend, request, *frontend.diagnostics);
}

yukt::CompileOutput emit_llvm_file(const std::string& relative) {
    const auto path = source_path(relative);
    auto frontend = yukt::analyze_file(path);
    yukt::analyze_types(frontend);
    yukt::CompileRequest request;
    request.action = yukt::CompileAction::EmitLlvm;
    request.input_path = path;
    return yukt::compile_program(frontend, request, *frontend.diagnostics);
}

std::size_t count_direct_calls(const std::string& llvm_ir, const std::string& callee) {
    std::size_t count = 0;
    const std::string needle = "@" + callee;
    std::size_t pos = 0;
    while ((pos = llvm_ir.find(needle, pos)) != std::string::npos) {
        const std::size_t line = llvm_ir.rfind('\n', pos);
        const std::size_t start = line == std::string::npos ? 0 : line + 1;
        const std::string prefix = llvm_ir.substr(start, pos - start);
        if (prefix.find("call") != std::string::npos && prefix.find("declare") == std::string::npos) {
            ++count;
        }
        pos += needle.size();
    }
    return count;
}

std::string llvm_function_body(const std::string& llvm_ir, const std::string& mangled) {
    const std::string marker = "@" + mangled + "(";
    std::size_t pos = 0;
    while ((pos = llvm_ir.find(marker, pos)) != std::string::npos) {
        const std::size_t line = llvm_ir.rfind('\n', pos);
        const std::size_t start = line == std::string::npos ? 0 : line + 1;
        if (llvm_ir.compare(start, 7, "define ") == 0) {
            const std::size_t open = llvm_ir.find('{', pos);
            if (open == std::string::npos) {
                return {};
            }
            int depth = 1;
            std::size_t i = open + 1;
            while (i < llvm_ir.size() && depth > 0) {
                if (llvm_ir[i] == '{') {
                    ++depth;
                } else if (llvm_ir[i] == '}') {
                    --depth;
                }
                ++i;
            }
            return llvm_ir.substr(open, i - open);
        }
        pos += marker.size();
    }
    return {};
}

}  // namespace

int main() {
    {
        const auto result = run_file("examples/hello.yukt");
        REQUIRE_MSG(result.ok, "hello.yukt must compile and run");
        REQUIRE(result.exit_code == 0);
        REQUIRE(result.program_output ==
                "Hello, World\n"
                "1\n"
                "2\n"
                "3\n"
                "4\n"
                "5\n"
                "YUKT is running!\n");
    }

    {
        const auto result = run_file("tests/programs/arithmetic.yukt");
        REQUIRE(result.ok);
        REQUIRE(result.program_output == "7\n");
        REQUIRE(result.exit_code == 54);
    }

    {
        const auto result = run_file("tests/programs/conditionals.yukt");
        REQUIRE(result.ok);
        REQUIRE(result.program_output == "adult\n");
    }

    {
        const auto result = run_file("tests/programs/nested_loops.yukt");
        REQUIRE(result.ok);
        REQUIRE(result.program_output ==
                "1\n1\n"
                "1\n2\n"
                "1\n3\n"
                "2\n1\n"
                "2\n2\n"
                "2\n3\n"
                "3\n1\n"
                "3\n2\n"
                "3\n3\n");
    }

    {
        const auto result = run_source(
            "create add(left, right):\n"
            "    give left + right\n"
            "\n"
            "create main:\n"
            "    say add(20, 22)\n"
            "    items = [1, 2, 3]\n"
            "    say items\n"
            "    if 3 > 1:\n"
            "        say true\n"
            "    otherwise:\n"
            "        say false\n");
        REQUIRE(result.ok);
        REQUIRE(result.program_output == "42\n[1, 2, 3]\ntrue\n");
    }

    {
        auto frontend = yukt::analyze_source("<test>", "create main:\n    greet(1)\n");
        yukt::analyze_types(frontend);
        REQUIRE(frontend.diagnostics->has_errors());
    }

    {
        const auto result = run_source(
            "create add(left, right):\n"
            "    give left + right\n"
            "\n"
            "create main:\n"
            "    items = zeros(20)\n"
            "    repeat i in 0..19:\n"
            "        items[i] = i + 1\n"
            "    total = 0\n"
            "    repeat i in 0..19:\n"
            "        total = total + items[i]\n"
            "    say count(items)\n"
            "    say total\n"
            "    say add(items[0], items[19])\n");
        REQUIRE_MSG(result.ok, result.program_output);
        REQUIRE(result.program_output == "20\n210\n21\n");
    }

    {
        const auto result = run_source(
            "create main:\n"
            "    n = 8\n"
            "    a = zeros(n * n)\n"
            "    b = zeros(n * n)\n"
            "    c = zeros(n * n)\n"
            "    repeat i in 0..n - 1:\n"
            "        repeat j in 0..n - 1:\n"
            "            a[i * n + j] = 1\n"
            "            b[i * n + j] = 2\n"
            "    repeat i in 0..n - 1:\n"
            "        repeat j in 0..n - 1:\n"
            "            total = 0\n"
            "            repeat k in 0..n - 1:\n"
            "                total = total + a[i * n + k] * b[k * n + j]\n"
            "            c[i * n + j] = total\n"
            "    checksum = 0\n"
            "    repeat i in 0..n * n - 1:\n"
            "        checksum = checksum + c[i]\n"
            "    say checksum\n");
        REQUIRE(result.ok);
        REQUIRE(result.program_output == "1024\n");
    }

    {
        const auto result = run_source(
            "create main:\n"
            "    total = 0\n"
            "    repeat i in 5..1:\n"
            "        total = total + i\n"
            "    say total\n"
            "    once = 0\n"
            "    repeat i in 7..7:\n"
            "        once = i\n"
            "    say once\n");
        REQUIRE_MSG(result.ok, result.program_output);
        REQUIRE(result.program_output == "0\n7\n");
    }

    {
        const auto result = run_source(
            "create main:\n"
            "    n = 8\n"
            "    items = zeros(n)\n"
            "    last = n - 1\n"
            "    repeat i in 0..last:\n"
            "        repeat j in 0..last:\n"
            "            items[j] = items[j] + i\n"
            "    checksum = 0\n"
            "    repeat j in 0..last:\n"
            "        checksum = checksum + items[j]\n"
            "    say checksum\n");
        REQUIRE_MSG(result.ok, result.program_output);
        REQUIRE(result.program_output == "224\n");
    }

    {
        const auto result = run_source(
            "create main:\n"
            "    n = 4096\n"
            "    items = zeros(n)\n"
            "    last = n - 1\n"
            "    repeat i in 0..last:\n"
            "        items[i] = i\n"
            "    total = 0\n"
            "    repeat i in 0..last:\n"
            "        total = total + items[i]\n"
            "    say total\n");
        REQUIRE_MSG(result.ok, result.program_output);
        REQUIRE(result.program_output == "8386560\n");
    }

    {
        const auto result = run_source(
            "create main:\n"
            "    n = 1000000\n"
            "    items = zeros(n)\n"
            "    repeat i in 0..n - 1:\n"
            "        items[i] = i\n"
            "    say items[0]\n"
            "    say items[n - 1]\n");
        REQUIRE_MSG(result.ok, result.program_output);
        REQUIRE(result.program_output == "0\n999999\n");
    }

    {
        const auto result = run_source(
            "create main:\n"
            "    n = 64\n"
            "    a = zeros(n)\n"
            "    b = zeros(n)\n"
            "    c = zeros(n)\n"
            "    repeat i in 0..n - 1:\n"
            "        a[i] = 1\n"
            "        b[i] = 2\n"
            "    repeat i in 0..n - 1:\n"
            "        total = 0\n"
            "        repeat k in 0..n - 1:\n"
            "            total = total + a[k] * b[k]\n"
            "        c[i] = total\n"
            "    checksum = 0\n"
            "    repeat i in 0..n - 1:\n"
            "        checksum = checksum + c[i]\n"
            "    say checksum\n");
        REQUIRE_MSG(result.ok, result.program_output);
        REQUIRE(result.program_output == "8192\n");
    }

    {
        auto frontend = yukt::analyze_file(source_path("examples/hello.yukt"));
        yukt::analyze_types(frontend);
        yukt::CompileRequest request;
        request.action = yukt::CompileAction::Explain;
        request.input_path = source_path("examples/hello.yukt");
        const auto result = yukt::compile_program(frontend, request, *frontend.diagnostics);
        REQUIRE(result.ok);
        REQUIRE(result.explanation.find("planned: sequential") != std::string::npos);
        REQUIRE(result.explanation.find("body contains `say`") != std::string::npos ||
                result.explanation.find("below 1000000") != std::string::npos);
        REQUIRE(result.explanation.find("verified parallel: yes") == std::string::npos);
        REQUIRE(result.explanation.find("PassBuilder default O3") != std::string::npos);
        REQUIRE(result.explanation.find("native, not cross-compiled") != std::string::npos);
        REQUIRE(result.explanation.find("triple:") != std::string::npos);
        REQUIRE(result.explanation.find("arm64") != std::string::npos ||
                result.explanation.find("aarch64") != std::string::npos ||
                result.explanation.find("x86_64") != std::string::npos);
        REQUIRE(result.explanation.find("Cost model thresholds") != std::string::npos);
    }

    {
        auto frontend = yukt::analyze_source("<test>",
                                            "create main:\n"
                                            "    n = 1000000\n"
                                            "    items = zeros(n)\n"
                                            "    repeat i in 0..n - 1:\n"
                                            "        items[i] = i\n"
                                            "    say items[0]\n");
        yukt::analyze_types(frontend);
        yukt::CompileRequest request;
        request.action = yukt::CompileAction::Explain;
        request.input_path = "<test>";
        const auto result = yukt::compile_program(frontend, request, *frontend.diagnostics);
        REQUIRE_MSG(result.ok, result.explanation);
        REQUIRE(result.explanation.find("planned: parallel") != std::string::npos);
        REQUIRE(result.explanation.find("verified parallel: yes") != std::string::npos);
        REQUIRE(result.llvm_ir.find("yukt_parallel_for") != std::string::npos);
    }

    {
        const auto result = run_source(
            "create main:\n"
            "    n = 1000000\n"
            "    items = zeros(n)\n"
            "    repeat i in 0..n - 1:\n"
            "        items[i] = 1\n"
            "    total = 0\n"
            "    repeat i in 0..n - 1:\n"
            "        total = total + items[i]\n"
            "    say total\n");
        REQUIRE_MSG(result.ok, result.program_output);
        REQUIRE(result.program_output == "1000000\n");
    }

    {
        const auto result = run_source(
            "create main:\n"
            "    n = 1000000\n"
            "    items = zeros(n)\n"
            "    repeat i in 0..n - 1:\n"
            "        items[i] = 1\n"
            "    total = 0.0\n"
            "    repeat i in 0..n - 1:\n"
            "        total = total + 1.0\n"
            "    say total\n");
        REQUIRE_MSG(result.ok, result.program_output);
        REQUIRE_MSG(result.program_output == "1000000\n" || result.program_output == "1e+06\n",
                    result.program_output);
    }

    {
        YuktList* first = yukt_list_zeros(128);
        REQUIRE(first != nullptr);
        yukt_list_set_i64(first, 0, 9);
        yukt_runtime_release();
        YuktList* second = yukt_list_zeros(128);
        REQUIRE(second != nullptr);
        REQUIRE(yukt_list_get_i64(second, 0) == 0);
        yukt_runtime_release();
    }

    {
        const auto result = run_file("tests/modules/ok/app.yukt");
        REQUIRE_MSG(result.ok, result.program_output);
        REQUIRE(result.program_output == "5\n");
        REQUIRE(result.exit_code == 0);
    }

    {
        const auto result = run_file("tests/modules/nested/a.yukt");
        REQUIRE_MSG(result.ok, result.program_output);
        REQUIRE(result.program_output == "7\n");
    }

    {
        const auto result = run_file("tests/modules/two_add/app.yukt");
        REQUIRE_MSG(result.ok, result.program_output);
        REQUIRE(result.program_output == "5\n6\n");
    }

    {
        const auto result = run_file("tests/modules/mangle/app.yukt");
        REQUIRE_MSG(result.ok, result.program_output);
        REQUIRE(result.program_output == "1\n2\n");
    }

    {
        const auto result = run_file("tests/ownership/overwrite.yukt");
        REQUIRE_MSG(result.ok, result.program_output);
        REQUIRE(result.program_output == "5\n");

        const auto llvm = emit_llvm_file("tests/ownership/overwrite.yukt");
        REQUIRE_MSG(llvm.ok, llvm.llvm_ir);
        const auto main_body = llvm_function_body(llvm.llvm_ir, "yukt_main");
        REQUIRE_MSG(!main_body.empty(), llvm.llvm_ir);
        REQUIRE_MSG(count_direct_calls(main_body, "yukt_release") == 2, main_body);
        REQUIRE_MSG(count_direct_calls(main_body, "yukt_runtime_release") == 1, main_body);
    }

    {
        const auto result = run_file("tests/ownership/give_live.yukt");
        REQUIRE_MSG(result.ok, result.program_output);
        REQUIRE(result.program_output == "3\n");

        const auto llvm = emit_llvm_file("tests/ownership/give_live.yukt");
        REQUIRE_MSG(llvm.ok, llvm.llvm_ir);
        const auto ident_body = llvm_function_body(llvm.llvm_ir, "yukt_ident");
        if (!ident_body.empty()) {
            REQUIRE_MSG(count_direct_calls(ident_body, "yukt_release") == 0, ident_body);
        }
        const auto main_body = llvm_function_body(llvm.llvm_ir, "yukt_main");
        REQUIRE_MSG(!main_body.empty(), llvm.llvm_ir);
        REQUIRE_MSG(count_direct_calls(main_body, "yukt_release") == 1, main_body);
        REQUIRE_MSG(count_direct_calls(main_body, "yukt_runtime_release") == 1, main_body);
    }

    {
        const auto result = run_file("tests/ownership/if_uninit_drop.yukt");
        REQUIRE_MSG(result.ok, result.program_output);
        REQUIRE(result.program_output == "1\n");
        REQUIRE(result.exit_code == 0);
    }

    {
        const auto result = run_file("tests/ownership/if_overwrite.yukt");
        REQUIRE_MSG(result.ok, result.program_output);
        REQUIRE(result.program_output == "5\n");
        REQUIRE(result.exit_code == 0);
    }

    {
        const auto result = run_source(
            "create main:\n"
            "    xs = zeros(1)\n"
            "    if false:\n"
            "        ys = xs\n"
            "        say count(ys)\n"
            "    say 1\n");
        REQUIRE_MSG(result.ok, result.program_output);
        REQUIRE(result.program_output == "1\n");
        REQUIRE(result.exit_code == 0);
    }

    {
        const auto result = run_file("tests/ownership/repeat_local.yukt");
        REQUIRE_MSG(result.ok, result.program_output);
        REQUIRE(result.program_output == "1\n");
        REQUIRE(result.exit_code == 0);
    }

    {
        const auto result = run_file("tests/ownership/loop_local_after.yukt");
        REQUIRE(!result.ok);
    }

    {
        const auto result = run_file("tests/ownership/moved_reassign.yukt");
        REQUIRE(!result.ok);
    }

    {
        const auto result = run_file("tests/ownership/if_join.yukt");
        REQUIRE(!result.ok);
    }

    {
        const auto result = run_file("tests/ownership/profile.yukt");
        REQUIRE_MSG(result.ok, result.program_output);
        REQUIRE(result.program_output == "4\n");
        REQUIRE(result.exit_code == 0);

        const auto llvm = emit_llvm_file("tests/ownership/profile.yukt");
        REQUIRE_MSG(llvm.ok, llvm.llvm_ir);
        const auto main_body = llvm_function_body(llvm.llvm_ir, "yukt_main");
        REQUIRE_MSG(!main_body.empty(), llvm.llvm_ir);
        REQUIRE_MSG(count_direct_calls(main_body, "yukt_release") >= 1, main_body);
        REQUIRE_MSG(count_direct_calls(main_body, "yukt_runtime_release") == 1, main_body);
    }

    {
        const auto result = run_file("tests/ownership/profile_count_overwrite.yukt");
        REQUIRE_MSG(result.ok, result.program_output);
        REQUIRE(result.program_output == "2\n");
        REQUIRE(result.exit_code == 0);

        const auto llvm = emit_llvm_file("tests/ownership/profile_count_overwrite.yukt");
        REQUIRE_MSG(llvm.ok, llvm.llvm_ir);
        const auto main_body = llvm_function_body(llvm.llvm_ir, "yukt_main");
        REQUIRE_MSG(!main_body.empty(), llvm.llvm_ir);
        REQUIRE_MSG(count_direct_calls(main_body, "yukt_release") >= 1, main_body);
        REQUIRE_MSG(count_direct_calls(main_body, "yukt_runtime_release") == 1, main_body);
    }

    {
        const auto result = run_file("tests/ownership/profile_move_ok.yukt");
        REQUIRE_MSG(result.ok, result.program_output);
        REQUIRE(result.program_output == "ann\n");
        REQUIRE(result.exit_code == 0);

        const auto llvm = emit_llvm_file("tests/ownership/profile_move_ok.yukt");
        REQUIRE_MSG(llvm.ok, llvm.llvm_ir);
        const auto main_body = llvm_function_body(llvm.llvm_ir, "yukt_main");
        REQUIRE_MSG(!main_body.empty(), llvm.llvm_ir);
        REQUIRE_MSG(count_direct_calls(main_body, "yukt_release") == 2, main_body);
        REQUIRE_MSG(count_direct_calls(main_body, "yukt_runtime_release") == 1, main_body);
    }

    {
        const auto result = run_file("tests/ownership/profile_field_assign_move.yukt");
        REQUIRE(!result.ok);
    }

    {
        auto frontend = yukt::analyze_source(
            "<test>",
            "record Profile:\n"
            "    name: string\n"
            "    scores: list\n"
            "\n"
            "create take(p):\n"
            "    say p.name\n"
            "    give zeros(1)\n"
            "\n"
            "create main:\n"
            "    p = Profile(\"ann\", zeros(2))\n"
            "    p.scores = take(p)\n");
        yukt::analyze_types(frontend);
        REQUIRE(frontend.diagnostics->has_errors());
    }

    {
        const auto result = run_file("tests/ownership/plain_record_no_drop.yukt");
        REQUIRE_MSG(result.ok, result.program_output);
        REQUIRE(result.program_output == "1\n1\n");

        const auto llvm = emit_llvm_file("tests/ownership/plain_record_no_drop.yukt");
        REQUIRE_MSG(llvm.ok, llvm.llvm_ir);
        const auto main_body = llvm_function_body(llvm.llvm_ir, "yukt_main");
        REQUIRE_MSG(!main_body.empty(), llvm.llvm_ir);
        REQUIRE_MSG(count_direct_calls(llvm.llvm_ir, "yukt_release") == 0, llvm.llvm_ir);
        REQUIRE_MSG(count_direct_calls(main_body, "yukt_release") == 0, main_body);
        REQUIRE_MSG(count_direct_calls(main_body, "yukt_runtime_release") == 1, main_body);
    }

    {
        const auto result = run_file("tests/ownership/list_slots.yukt");
        REQUIRE_MSG(result.ok, result.program_output);
        REQUIRE(result.program_output == "c\nb\n");
        REQUIRE(result.exit_code == 0);
    }

    {
        const auto result = run_file("tests/ownership/list_of_profile.yukt");
        REQUIRE_MSG(result.ok, result.program_output);
        REQUIRE(result.program_output == "a\n");
        REQUIRE(result.exit_code == 0);
    }

    {
        const auto result = run_file("tests/ownership/list_steal.yukt");
        REQUIRE(!result.ok);
    }

    {
        const auto result = run_file("tests/ownership/list_fill_strings.yukt");
        REQUIRE_MSG(result.ok, result.program_output);
        REQUIRE(result.program_output == "x\nz\n");
        REQUIRE(result.exit_code == 0);
    }

    {
        const auto result = run_file("tests/ownership/list_fill_alias.yukt");
        REQUIRE_MSG(result.ok, result.program_output);
        REQUIRE(result.program_output == "x\na\n");
        REQUIRE(result.exit_code == 0);
    }

    {
        const auto result = run_file("tests/ownership/list_index_assign_move.yukt");
        REQUIRE(!result.ok);
    }

    {
        auto frontend = yukt::analyze_source(
            "<test>",
            "create take(xs):\n"
            "    give \"z\"\n"
            "\n"
            "create main:\n"
            "    names = [\"a\", \"b\"]\n"
            "    names[0] = take(names)\n");
        yukt::analyze_types(frontend);
        REQUIRE(frontend.diagnostics->has_errors());
    }

    {
        const auto result = run_file("tests/ownership/list_index_borrow.yukt");
        REQUIRE_MSG(result.ok, result.program_output);
        REQUIRE(result.program_output == "x\nb\n");
        REQUIRE(result.exit_code == 0);
    }

    {
        const auto result = run_file("tests/ownership/list_of_profile_nested.yukt");
        REQUIRE_MSG(result.ok, result.program_output);
        REQUIRE(result.program_output == "a\n");
        REQUIRE(result.exit_code == 0);
    }

    {
        const auto result = run_file("tests/ownership/list_fill_nested_profile.yukt");
        REQUIRE_MSG(result.ok, result.program_output);
        REQUIRE(result.program_output == "x\na\n");
        REQUIRE(result.exit_code == 0);
        const auto llvm = emit_llvm_file("tests/ownership/list_fill_nested_profile.yukt");
        REQUIRE_MSG(llvm.ok, llvm.llvm_ir);
        REQUIRE(llvm.llvm_ir.find("yukt_list_fill_list") == std::string::npos);
        REQUIRE(llvm.llvm_ir.find("yukt_str_clone") != std::string::npos);
    }

    {
        const auto result = run_file("tests/ownership/list_index_replace_nested.yukt");
        REQUIRE_MSG(result.ok, result.program_output);
        REQUIRE(result.program_output == "keep\nx\n");
        REQUIRE(result.exit_code == 0);
    }

    {
        const auto result = run_file("tests/ownership/list_literal_steal.yukt");
        REQUIRE(!result.ok);
    }

    {
        const auto result = run_file("tests/ownership/list_literal_use_after.yukt");
        REQUIRE(!result.ok);
    }

    {
        const auto result = run_file("tests/ownership/list_literal_move.yukt");
        REQUIRE_MSG(result.ok, result.program_output);
        REQUIRE(result.program_output == "a\n");
        REQUIRE(result.exit_code == 0);
        const auto llvm = emit_llvm_file("tests/ownership/list_literal_move.yukt");
        REQUIRE_MSG(llvm.ok, llvm.llvm_ir);
        REQUIRE(llvm.llvm_ir.find("yukt_str_clone") == std::string::npos);
    }

    {
        const auto result = run_file("tests/ownership/list_slice_owned.yukt");
        REQUIRE_MSG(result.ok, result.program_output);
        REQUIRE(result.program_output == "b\n");
        REQUIRE(result.exit_code == 0);
    }

    {
        const auto result = run_file("tests/ownership/list_slice_owned_trap.yukt");
        REQUIRE_MSG(result.ok, "owned-record slice trap must compile\n" + result.program_output);
        REQUIRE(result.exit_code != 0);
        REQUIRE(result.program_output.find("YUKT runtime error:") != std::string::npos);
        REQUIRE(result.program_output.find("invalid list slice") != std::string::npos);
    }

    {
        const auto result = run_file("tests/ownership/list_slice_owned_trap_start.yukt");
        REQUIRE_MSG(result.ok, "start<0 owned-record slice must compile\n" + result.program_output);
        REQUIRE(result.exit_code != 0);
        REQUIRE(result.program_output.find("invalid list slice") != std::string::npos);
    }

    {
        const auto result = run_file("tests/ownership/list_slice_owned_trap_order.yukt");
        REQUIRE_MSG(result.ok, "end<start owned-record slice must compile\n" + result.program_output);
        REQUIRE(result.exit_code != 0);
        REQUIRE(result.program_output.find("invalid list slice") != std::string::npos);
    }

    return yukt::test::summary("native");
}
