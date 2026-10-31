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
        const auto path = source_path("tests/tasks/basic.yukt");
        auto frontend = yukt::analyze_file(path);
        yukt::analyze_types(frontend);
        yukt::CompileRequest request;
        request.action = yukt::CompileAction::Run;
        request.input_path = path;
        const auto output = yukt::compile_program(frontend, request, *frontend.diagnostics);
        REQUIRE_MSG(output.ok, output.program_output);
        REQUIRE(output.program_output == "4\n");
    }

    {
        const auto path = source_path("tests/tasks/void_task.yukt");
        auto frontend = yukt::analyze_file(path);
        yukt::analyze_types(frontend);
        yukt::CompileRequest request;
        request.action = yukt::CompileAction::Run;
        request.input_path = path;
        const auto output = yukt::compile_program(frontend, request, *frontend.diagnostics);
        REQUIRE_MSG(output.ok, output.program_output);
        REQUIRE(output.program_output == "1\n");
    }

    {
        const auto path = source_path("tests/tasks/heap_result.yukt");
        auto frontend = yukt::analyze_file(path);
        yukt::analyze_types(frontend);
        yukt::CompileRequest request;
        request.action = yukt::CompileAction::Run;
        request.input_path = path;
        const auto output = yukt::compile_program(frontend, request, *frontend.diagnostics);
        REQUIRE_MSG(output.ok, output.program_output);
        REQUIRE(output.program_output == "ok\n");
    }

    {
        const auto path = source_path("tests/tasks/result_ok.yukt");
        auto frontend = yukt::analyze_file(path);
        yukt::analyze_types(frontend);
        yukt::CompileRequest request;
        request.action = yukt::CompileAction::Run;
        request.input_path = path;
        const auto output = yukt::compile_program(frontend, request, *frontend.diagnostics);
        REQUIRE_MSG(output.ok, output.program_output);
        REQUIRE(output.program_output == "ok\n");
    }

    return yukt::test::summary("yukt_task_tests");
}
