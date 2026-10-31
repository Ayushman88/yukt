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

}  // namespace

int main() {
    {
        const auto result = run_file("tests/foreign/strlen.yukt");
        REQUIRE_MSG(result.ok, result.program_output);
        REQUIRE(result.program_output == "0\n5\n4\n");
    }

    {
        const auto result = run_file("tests/foreign/abs.yukt");
        REQUIRE_MSG(result.ok, result.program_output);
        REQUIRE(result.program_output == "7\n4\n");
    }

    {
        const auto result = run_file("tests/foreign/point.yukt");
        REQUIRE_MSG(result.ok, result.program_output);
        REQUIRE(result.program_output == "7\n14\n");
    }

    {
        const auto result = run_file("tests/foreign/bool.yukt");
        REQUIRE_MSG(result.ok, result.program_output);
        REQUIRE(result.program_output == "1\n0\n");
    }

    {
        auto frontend = yukt::analyze_file(source_path("tests/foreign/strlen.yukt"));
        yukt::analyze_types(frontend);
        REQUIRE(!frontend.diagnostics->has_errors());
        bool found = false;
        for (const auto& function : frontend.semantic.functions) {
            if (function.name == "strlen") {
                REQUIRE(function.parameters.size() == 1);
                REQUIRE(function.parameters[0].type == "ref string");
                REQUIRE(function.return_type == "integer");
                found = true;
            }
            if (function.name == "main") {
                for (const auto& param : function.parameters) {
                    REQUIRE(param.type.find("ptr") == std::string::npos);
                    REQUIRE(param.type.find("*") == std::string::npos);
                }
            }
        }
        REQUIRE(found);
    }

    {
        const auto result = check_source(
            "foreign strlen(s: ref string): integer\n"
            "\n"
            "create main:\n"
            "    say strlen(3)\n");
        REQUIRE(has_title(result, "Type mismatch"));
    }

    {
        const auto result = check_source(
            "foreign bad(s: string): integer\n"
            "\n"
            "create main:\n"
            "    say 1\n");
        REQUIRE(has_title(result, "Type mismatch"));
    }

    return yukt::test::summary("foreign");
}
