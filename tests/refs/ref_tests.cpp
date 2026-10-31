#include "backend/pipeline.hpp"
#include "driver/driver.hpp"
#include "test_harness.hpp"

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
        const auto result = run_file("tests/refs/increment.yukt");
        REQUIRE_MSG(result.ok, result.program_output);
        REQUIRE(result.program_output == "2\n");
    }

    {
        const auto result = run_file("tests/refs/pass_along.yukt");
        REQUIRE_MSG(result.ok, result.program_output);
        REQUIRE(result.program_output == "3\n");
    }

    {
        const auto result = run_file("tests/refs/field.yukt");
        REQUIRE_MSG(result.ok, result.program_output);
        REQUIRE(result.program_output == "2\n2\n");
    }

    {
        const auto result = check_file("tests/refs/overlap.yukt");
        REQUIRE(has_title(result, "Overlapping borrow"));
    }

    {
        const auto result = check_file("tests/refs/field_overlap.yukt");
        REQUIRE(has_title(result, "Overlapping borrow"));
    }

    {
        const auto result = check_file("tests/refs/give_ref.yukt");
        REQUIRE(has_title(result, "Cannot give a ref"));
    }

    {
        const auto result = check_file("tests/refs/store_ref.yukt");
        REQUIRE(has_title(result, "Cannot store a ref"));
    }

    {
        const auto result = check_file("tests/refs/temporary.yukt");
        REQUIRE(has_title(result, "Cannot borrow a temporary"));
    }

    {
        const auto result = check_source(
            "create steal(xs: ref list):\n"
            "    give xs\n"
            "\n"
            "create main:\n"
            "    xs = zeros(2)\n"
            "    say count(steal(xs))\n");
        REQUIRE(has_title(result, "Cannot give a ref"));
    }

    {
        const auto result = check_source(
            "create steal(xs: ref list):\n"
            "    ys = xs\n"
            "    say count(ys)\n"
            "\n"
            "create main:\n"
            "    xs = zeros(2)\n"
            "    steal(xs)\n"
            "    say count(xs)\n");
        REQUIRE(has_title(result, "Cannot move out of a ref"));
    }

    {
        const auto result = run_source(
            "create add_one(value: ref float):\n"
            "    value = value + 1.0\n"
            "\n"
            "create main:\n"
            "    x = 1.5\n"
            "    add_one(x)\n"
            "    say x\n");
        REQUIRE_MSG(result.ok, result.program_output);
        REQUIRE(result.program_output == "2.5\n");
    }

    {
        const auto result = run_file("tests/refs/suite.yukt");
        REQUIRE_MSG(result.ok, result.program_output);
        REQUIRE(result.program_output == "50025000\n1\n2\n");
    }

    {
        auto frontend = yukt::analyze_file(source_path("tests/refs/increment.yukt"));
        yukt::analyze_types(frontend);
        REQUIRE(!frontend.diagnostics->has_errors());
        bool found_ref = false;
        for (const auto& function : frontend.semantic.functions) {
            if (function.name != "increment") {
                continue;
            }
            REQUIRE(function.parameters.size() == 1);
            REQUIRE(function.parameters[0].type == "ref integer");
            found_ref = true;
        }
        REQUIRE(found_ref);
    }

    return yukt::test::summary("refs");
}
