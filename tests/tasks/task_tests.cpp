#include "backend/pipeline.hpp"
#include "driver/driver.hpp"
#include "ir/ir.hpp"
#include "ir/lower.hpp"
#include "semantic/type.hpp"
#include "test_harness.hpp"

#include <sstream>
#include <string>

#ifndef YUKT_SOURCE_DIR
#define YUKT_SOURCE_DIR "."
#endif

namespace {

std::string source_path(const std::string& relative) {
    return std::string(YUKT_SOURCE_DIR) + "/" + relative;
}

yukt::FrontendResult check_source(const std::string& source) {
    auto result = yukt::analyze_source("<test>", source);
    yukt::analyze_types(result);
    return result;
}

yukt::FrontendResult check_file(const std::string& relative) {
    auto result = yukt::analyze_file(source_path(relative));
    yukt::analyze_types(result);
    return result;
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

}  // namespace

int main() {
    {
        yukt::TypeContext types;
        REQUIRE(types.to_string(yukt::task_type(yukt::integer_type())) == "task[integer]");
        REQUIRE(types.to_string(yukt::task_type(yukt::void_type())) == "task[void]");
    }

    {
        const auto result = check_source(
            "create work(n):\n"
            "    give n\n"
            "\n"
            "create main:\n"
            "    t = start work(1)\n"
            "    say wait t\n");
        REQUIRE(!result.diagnostics->has_errors());
        std::ostringstream out;
        yukt::print_types(result.semantic, out);
        REQUIRE(out.str().find("t: task[integer]") != std::string::npos);
    }

    {
        const auto result = check_source("create main:\n    t = start missing(1)\n");
        REQUIRE(has_title(result, "Unknown function"));
    }

    {
        const auto result = check_source(
            "foreign strlen(s: ref string): integer\n"
            "create main:\n"
            "    t = start strlen(\"a\")\n");
        REQUIRE(has_title(result, "Invalid start"));
    }

    {
        const auto result = check_source("create main:\n    t = start main()\n");
        REQUIRE(has_title(result, "Invalid start"));
    }

    {
        const auto result = check_source(
            "record Point:\n"
            "    x: integer\n"
            "create main:\n"
            "    t = start Point(1)\n");
        REQUIRE(has_title(result, "Invalid start"));
    }

    {
        const auto result = check_source(
            "create main:\n"
            "    t = start ok(1)\n");
        REQUIRE(has_title(result, "Invalid start"));
    }

    {
        const auto result = check_source(
            "create inc(n: ref integer):\n"
            "    n = n + 1\n"
            "create main:\n"
            "    x = 1\n"
            "    t = start inc(x)\n");
        REQUIRE(has_title(result, "Cannot start a ref"));
    }

    {
        const auto result = check_source(
            "create take(t: ref task[integer]):\n"
            "    give wait t\n"
            "create work(n):\n"
            "    give n\n"
            "create main:\n"
            "    t = start work(1)\n"
            "    say take(t)\n");
        REQUIRE(has_title(result, "Cannot move out of a ref"));
    }

    {
        const auto result = check_source(
            "record task:\n"
            "    n: integer\n"
            "create main:\n"
            "    say 1\n");
        REQUIRE(has_title(result, "Invalid record"));
    }

    {
        const auto result = check_source(
            "record Box:\n"
            "    t: task[integer]\n"
            "create main:\n"
            "    say 1\n");
        REQUIRE(!result.diagnostics->has_errors());
    }

    {
        const auto result = check_source(
            "create work(n):\n"
            "    give n\n"
            "create main:\n"
            "    t = start work(1)\n");
        REQUIRE(has_title(result, "Unused task"));
    }

    {
        const auto result = check_source(
            "create work(n):\n"
            "    give n\n"
            "create main:\n"
            "    t = start work(1)\n"
            "    t = start work(2)\n"
            "    say wait t\n");
        REQUIRE(has_title(result, "Unused task"));
    }

    {
        const auto result = check_source("create main:\n    say wait 1\n");
        REQUIRE(has_title(result, "Type mismatch"));
    }

    {
        const auto result = check_source(
            "create work:\n"
            "    say 1\n"
            "create main:\n"
            "    t = start work()\n"
            "    wait t\n");
        REQUIRE(!result.diagnostics->has_errors());
        std::ostringstream out;
        yukt::print_types(result.semantic, out);
        REQUIRE(out.str().find("t: task[void]") != std::string::npos);
    }

    {
        const auto result = check_source(
            "create work:\n"
            "    say 1\n"
            "create main:\n"
            "    t = start work()\n"
            "    x = wait t\n");
        REQUIRE(has_title(result, "Type mismatch"));
    }

    {
        const auto result = check_source(
            "create take(xs):\n"
            "    give count(xs)\n"
            "create main:\n"
            "    xs = [1, 2]\n"
            "    t = start take(xs)\n"
            "    say count(xs)\n"
            "    say wait t\n");
        REQUIRE(has_title(result, "Use after move"));
    }

    {
        const auto result = check_source(
            "create work(n):\n"
            "    give n\n"
            "create main:\n"
            "    t = start work(1)\n"
            "    say t\n"
            "    say wait t\n");
        REQUIRE(has_title(result, "Type mismatch"));
    }

    {
        const auto result = check_source(
            "create work(n):\n"
            "    give n\n"
            "create main:\n"
            "    start work(1)\n");
        REQUIRE(has_title(result, "Unused task"));
    }

    {
        const auto result = check_source(
            "create work(n):\n"
            "    give n\n"
            "create main:\n"
            "    t = start work(1, 2)\n"
            "    say wait t\n");
        REQUIRE(has_title(result, "Wrong number of arguments"));
    }

    {
        const auto result = check_file("tests/tasks/imported/app.yukt");
        REQUIRE(!result.diagnostics->has_errors());
        REQUIRE(result.semantic.start_ir_names.size() >= 1);
    }

    {
        const auto result = check_file("tests/tasks/imported/app_private.yukt");
        REQUIRE(has_title(result, "Unknown function"));
    }

    {
        const auto result = check_file("tests/tasks/basic.yukt");
        REQUIRE(!result.diagnostics->has_errors());
        yukt::IrModule module;
        if (!result.graph.modules.empty()) {
            module = yukt::lower_to_ir(result.graph, result.semantic);
        } else {
            module = yukt::lower_to_ir(*result.program, result.semantic, result.path);
        }
        std::ostringstream printed;
        yukt::print_ir(module, printed);
        const std::string ir = printed.str();
        REQUIRE(ir.find(" = start ") != std::string::npos);
        REQUIRE(ir.find(" = wait ") != std::string::npos);
        REQUIRE(ir.find("t: task[i64]") != std::string::npos);
    }

    {
        const auto output = run_file("tests/tasks/basic.yukt");
        REQUIRE_MSG(output.ok, output.program_output);
        REQUIRE(output.program_output == "4\n");
    }

    {
        const auto output = run_file("tests/tasks/void_task.yukt");
        REQUIRE_MSG(output.ok, output.program_output);
        REQUIRE(output.program_output == "1\n");
    }

    {
        const auto output = run_file("tests/tasks/heap_result.yukt");
        REQUIRE_MSG(output.ok, output.program_output);
        REQUIRE(output.program_output == "ok\n");
    }

    {
        const auto output = run_file("tests/tasks/result_ok.yukt");
        REQUIRE_MSG(output.ok, output.program_output);
        REQUIRE(output.program_output == "ok\n");
    }

    {
        const auto output = run_file("tests/tasks/two_tasks.yukt");
        REQUIRE_MSG(output.ok, output.program_output);
        REQUIRE(output.program_output == "3\n2\n");
    }

    {
        const auto output = run_file("tests/tasks/nested.yukt");
        REQUIRE_MSG(output.ok, output.program_output);
        REQUIRE(output.program_output == "4\n");
    }

    {
        const auto output = run_file("tests/tasks/transfer_take.yukt");
        REQUIRE_MSG(output.ok, output.program_output);
        REQUIRE(output.program_output == "2\n");
    }

    {
        const auto output = run_file("tests/tasks/transfer_give.yukt");
        REQUIRE_MSG(output.ok, output.program_output);
        REQUIRE(output.program_output == "2\n");
    }

    {
        const auto result = check_source(
            "create work(n):\n"
            "    give n + 1\n"
            "create take(t):\n"
            "    give wait t\n"
            "create main:\n"
            "    u = start work(1)\n"
            "    say take(u)\n"
            "    say wait u\n");
        REQUIRE(has_title(result, "Use after move"));
    }

    {
        const auto result = check_source(
            "create work:\n"
            "    say 1\n"
            "create main:\n"
            "    repeat i in 1..3:\n"
            "        wait start work()\n");
        REQUIRE(!result.diagnostics->has_errors());
    }

    {
        const auto result = check_source(
            "create work(n):\n"
            "    give n\n"
            "create main:\n"
            "    xs = zeros(1000000)\n"
            "    repeat i in 1..1000000:\n"
            "        xs[i] = i\n"
            "        wait start work(i)\n");
        REQUIRE(has_title(result, "Cannot start inside parallel repeat"));
    }

    {
        const auto result = check_source(
            "create work(n):\n"
            "    give n\n"
            "create main:\n"
            "    xs = zeros(1000000)\n"
            "    repeat i in 1..1000000:\n"
            "        xs[i] = i\n"
            "        if i > 0:\n"
            "            wait start work(i)\n");
        REQUIRE(has_title(result, "Cannot start inside parallel repeat"));
    }

    {
        const auto output = run_file("tests/tasks/nested_depth.yukt");
        REQUIRE_MSG(output.ok, output.program_output);
        REQUIRE(output.exit_code == 0);
        REQUIRE(output.program_output == "9\n");
    }

    {
        const auto output = run_file("tests/tasks/many_reverse.yukt");
        REQUIRE_MSG(output.ok, output.program_output);
        REQUIRE(output.exit_code == 0);
        REQUIRE(output.program_output == "5\n4\n3\n2\n1\n");
    }

    {
        const auto output = run_file("tests/tasks/worker_trap.yukt");
        REQUIRE(output.exit_code != 0);
        REQUIRE(output.program_output.find("YUKT runtime error:") != std::string::npos);
    }

    return yukt::test::summary("yukt_task_tests");
}
