#include "backend/pipeline.hpp"
#include "driver/driver.hpp"
#include "test_harness.hpp"

#include <string>

// Padding bytes cannot be observed from YUKT: records have no padding-typed
// fields, and equality is specified as fieldwise. A source program cannot
// construct two values that differ only in padding, so padding-independent
// equality is locked by fieldwise == tests (basic/nested_eq) plus LLVM
// extractvalue lowering, not by a padding-injection test.

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

}  // namespace

int main() {
    {
        const auto result = run_file("tests/records/basic.yukt");
        REQUIRE_MSG(result.ok, result.program_output);
        REQUIRE(result.program_output ==
                "Point(1, 2)\n"
                "1\n"
                "Point(3, 2)\n"
                "Point(3, 2)\n"
                "Point(3, 9)\n"
                "true\n"
                "true\n");
    }

    {
        const auto result = run_file("tests/records/nested.yukt");
        REQUIRE_MSG(result.ok, result.program_output);
        REQUIRE(result.program_output ==
                "Box(Point(1, 2), 5)\n"
                "8\n"
                "Box(Point(8, 2), 5)\n");
    }

    {
        const auto result = run_file("tests/records/list.yukt");
        REQUIRE_MSG(result.ok, result.program_output);
        REQUIRE(result.program_output ==
                "[Point(1, 2), Point(3, 4)]\n"
                "Point(1, 2)\n"
                "[Point(1, 2), Point(9, 8)]\n"
                "[Point(7, 2), Point(9, 8)]\n"
                "2\n");
    }

    {
        const auto result = run_file("tests/records/bool_abi.yukt");
        REQUIRE_MSG(result.ok, result.program_output);
        REQUIRE(result.program_output ==
                "Flag(true, 4)\n"
                "true\n"
                "Flag(false, 4)\n"
                "true\n");
    }

    {
        const auto result = run_file("tests/records/nan.yukt");
        REQUIRE_MSG(result.ok, result.program_output);
        REQUIRE(result.program_output == "false\ntrue\n");
    }

    {
        const auto result = run_file("tests/records/params.yukt");
        REQUIRE_MSG(result.ok, result.program_output);
        REQUIRE(result.program_output == "Point(4, 6)\n");
    }

    {
        const auto result = run_file("tests/records/import/app.yukt");
        REQUIRE_MSG(result.ok, result.program_output);
        REQUIRE(result.program_output == "Point(1, 2)\nPoint(0, 0)\n");
    }

    {
        const auto result = run_file("tests/records/same_name/app.yukt");
        REQUIRE_MSG(result.ok, result.program_output);
        REQUIRE(result.program_output == "Point(1, 2)\nPoint(9)\n");
    }

    {
        auto result = check_file("tests/records/private/app.yukt");
        REQUIRE(has_title(result, "Private type"));
    }

    {
        auto result = check_file("tests/records/leak/app.yukt");
        REQUIRE(has_title(result, "Private type"));
    }

    {
        auto result = check_file("tests/records/import/module_value.yukt");
        REQUIRE(has_title(result, "Invalid expression"));
    }

    {
        auto result = check_source(
            "record Point:\n"
            "    x: integer\n"
            "    y: integer\n"
            "\n"
            "create main:\n"
            "    say unknown.x\n");
        REQUIRE(has_title(result, "Unknown name"));
    }

    {
        auto result = check_source(
            "record Hidden:\n"
            "    n: integer\n"
            "\n"
            "share record Box:\n"
            "    h: Hidden\n"
            "\n"
            "create main:\n"
            "    say 1\n");
        REQUIRE(has_title(result, "Private type"));
    }

    {
        auto result = check_source(
            "record Point:\n"
            "    x: integer\n"
            "    y: integer\n"
            "\n"
            "create main:\n"
            "    p = Point(1, 2)\n"
            "    items = zeros(1)\n"
            "    items[0] = p\n");
        REQUIRE(has_title(result, "Type mismatch"));
    }

    {
        const auto result = run_source(
            "record Point:\n"
            "    x: integer\n"
            "    y: integer\n"
            "\n"
            "create ident(p):\n"
            "    give p\n"
            "\n"
            "create main:\n"
            "    empty = [Point(0, 0)]\n"
            "    empty[0] = ident(Point(4, 5))\n"
            "    say empty\n"
            "    say empty[0].y\n");
        REQUIRE_MSG(result.ok, result.program_output);
        REQUIRE(result.program_output == "[Point(4, 5)]\n5\n");
    }

    {
        auto result = check_file("tests/records/type_fill.yukt");
        REQUIRE(result.diagnostics->has_errors());
        REQUIRE(has_title(result, "Type mismatch"));
        bool named_fill = false;
        for (const auto& diagnostic : result.diagnostics->diagnostics()) {
            if (diagnostic.title == "Type mismatch" &&
                diagnostic.detail.find("fill") != std::string::npos &&
                diagnostic.detail.find("record") != std::string::npos) {
                named_fill = true;
            }
        }
        REQUIRE(named_fill);
        yukt::CompileRequest request;
        request.action = yukt::CompileAction::EmitLlvm;
        request.input_path = source_path("tests/records/type_fill.yukt");
        const auto compiled = yukt::compile_program(result, request, *result.diagnostics);
        REQUIRE(!compiled.ok);
        REQUIRE(compiled.llvm_ir.find("yukt_list_fill_i64") == std::string::npos);
    }

    {
        auto result = check_source(
            "record Point:\n"
            "    x: integer\n"
            "    y: integer\n"
            "\n"
            "create main:\n"
            "    p = Point(1, true)\n");
        REQUIRE(has_title(result, "Type mismatch"));
    }

    {
        auto result = check_source(
            "record Point:\n"
            "    x: integer\n"
            "    x: float\n"
            "\n"
            "create main:\n"
            "    say 1\n");
        REQUIRE(has_title(result, "Duplicate field"));
    }

    {
        auto result = check_source(
            "record Node:\n"
            "    next: Node\n"
            "\n"
            "create main:\n"
            "    say 1\n");
        REQUIRE(has_title(result, "Recursive record"));
    }

    {
        const auto result = run_file("tests/records/empty.yukt");
        REQUIRE_MSG(result.ok, result.program_output);
        REQUIRE(result.exit_code == 0);
        REQUIRE(result.program_output == "[]\n0\n");
    }

    {
        const auto result = run_file("tests/records/oob.yukt");
        REQUIRE_MSG(result.ok, "oob must compile\n" + result.program_output);
        REQUIRE(result.exit_code != 0);
        REQUIRE(result.program_output.find("YUKT runtime error:") != std::string::npos);
        REQUIRE(result.program_output.find("list index out of range") != std::string::npos);
    }

    {
        const auto result = run_file("tests/records/signed_zero.yukt");
        REQUIRE_MSG(result.ok, result.program_output);
        REQUIRE(result.program_output == "true\nfalse\n");
    }

    {
        const auto result = run_file("tests/records/nested_eq.yukt");
        REQUIRE_MSG(result.ok, result.program_output);
        REQUIRE(result.program_output == "true\nfalse\ntrue\n");
    }

    {
        const auto result = run_file("tests/records/sizes.yukt");
        REQUIRE_MSG(result.ok, result.program_output);
        REQUIRE(result.program_output ==
                "[Flag(true), Flag(false)]\n"
                "[Point(1, 2)]\n"
                "true\n"
                "2\n");
    }

    {
        const char* source =
            "record Flag:\n"
            "    on: boolean\n"
            "    n: integer\n"
            "\n"
            "create wrap(on, n):\n"
            "    give Flag(on, n)\n"
            "\n"
            "create main:\n"
            "    bits = [true, false]\n"
            "    flags = [wrap(bits[0], 1), wrap(bits[1], 0)]\n"
            "    say flags[0].on\n"
            "    say flags[1].on\n"
            "    say flags[0].n\n";
        auto frontend = yukt::analyze_source("<test>", source);
        yukt::analyze_types(frontend);
        yukt::CompileRequest request;
        request.action = yukt::CompileAction::EmitLlvm;
        request.input_path = "<test>";
        const auto llvm = yukt::compile_program(frontend, request, *frontend.diagnostics);
        REQUIRE_MSG(llvm.ok, llvm.llvm_ir);
        REQUIRE_MSG(llvm.llvm_ir.find("{ i8, i64 }") != std::string::npos ||
                        llvm.llvm_ir.find("{i8, i64}") != std::string::npos,
                    llvm.llvm_ir);
        REQUIRE_MSG(llvm.llvm_ir.find("zext i1") != std::string::npos ||
                        llvm.llvm_ir.find("and i8") != std::string::npos,
                    llvm.llvm_ir);
        const auto run = run_source(source);
        REQUIRE_MSG(run.ok, run.program_output);
        REQUIRE(run.program_output == "true\nfalse\n1\n");
    }

    return yukt::test::summary("records");
}
